"""Secret-free aggregate of the frozen sixteen-window screen; no native changes."""
import json
import argparse
from collections import Counter
from pathlib import Path
from statistics import median

from run import percentile

BASE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--resumed', action='store_true')
args = parser.parse_args()
files = ('corrected-warp.jsonl', 'confirmation.jsonl') if args.resumed else ('final-screen.jsonl', 'final-warp.jsonl')
rows = [json.loads(line) for name in files
        for line in (BASE / name).read_text().splitlines() if line]
campaigns = ('corrected-warp-c2-c0', 'corrected-warp-c2-r', 'confirm-c2-c0') if args.resumed else ('final-c2-c0', 'final-c2-r', 'warp-final-c2-c0', 'warp-final-c2-r')
report = {}
for campaign in campaigns:
    data = [r for r in rows if r['campaign'] == campaign]
    arms = {}
    for arm in ('A', 'B'):
        part = [r for r in data if r['arm'] == arm]
        windows = [r for r in part if r['kind'] == 'window']
        primary = [r for r in part if r['kind'] == 'request' and r['condition'] == 'sequential'
                   and r['opcode'] == 'D' and r['requested'] == 1048576]
        requests = [r for r in part if r['kind'] == 'request']
        failures = [r for r in requests if not r['ok'] and r.get('error') != 'CancelledError']
        loaded = [r['seconds'] for r in requests if r['condition'] == 'loaded']
        uploads = [r['seconds'] for r in requests if r['opcode'] == 'U']
        idle = {}
        for condition in ('empty', 'quiet-open', 'settle'):
            samples = [r for r in part if r['kind'] == 'idle' and r['condition'] == condition]
            idle[condition] = dict(queries_per_s=[r.get('queries_per_s') for r in samples],
                                  cpu_s=[(r.get('resource_delta') or {}).get('cpu_s') for r in samples])
        arms[arm] = dict(variant=part[0]['variant'], primary_Bps=[r['ack_Bps'] for r in primary],
                        primary_median_Bps=median(r['ack_Bps'] for r in primary),
                        full_window_passes=sum(r['ok'] for r in windows), windows=len(windows),
                        loaded_p95_including_failures_s=percentile(loaded, .95),
                        loaded_p95_successful_only_s=percentile([r['seconds'] for r in requests if r['condition']=='loaded' and r['ok']], .95),
                        upload_p95_including_failures_s=percentile(uploads, .95),
                        payload_failures=len(failures),
                        failure_counts=dict(Counter(r['condition']+':'+r.get('error','unknown') for r in failures)),
                        failure_details=[{k:r.get(k) for k in ('pair','condition','opcode','requested','error','seconds','timeout')} for r in failures],
                        intentional_background_cancellations=sum(r.get('error') == 'CancelledError' for r in requests),
                        startup_s=[r['seconds'] for r in part if r['kind'] == 'startup'], idle=idle,
                        resources=[dict(pair=r['pair'], seconds=r.get('seconds'), delta=r.get('resource_delta'),
                                        peaks=r.get('resource_peaks')) for r in windows],
                        client_sha256=sorted({r['client_sha256'] for r in part if r['kind'] == 'manifest'}))
    paired=[]
    for pair in range(6 if campaign == 'confirm-c2-c0' else 2):
        part=[r for r in data if r['pair'] == pair]
        starts=[(r['position'],r['arm']) for r in part if r['kind']=='manifest']
        assert starts == ([(0,'A'),(1,'B')] if pair % 2 == 0 else [(0,'B'),(1,'A')]), starts
        prim={r['arm']:r for r in part if r['kind']=='request' and r['condition']=='sequential'
              and r['opcode']=='D' and r['requested']==1048576}
        control_valid=prim['A']['ok'] and prim['A']['ack_Bps'] > 0
        ratio=prim['B']['ack_Bps']/prim['A']['ack_Bps'] if control_valid else None
        paired.append(dict(pair=pair, actual_order=starts, primary_ratio=ratio,
                           control_primary_valid=control_valid,
                           full_window_valid=all(r['ok'] for r in part if r['kind']=='window')))
    ratios=[p['primary_ratio'] for p in paired if p['primary_ratio'] is not None]
    startup_valid=all(r['ok'] for r in data if r['kind']=='startup')
    latency_ratio=arms['B']['loaded_p95_including_failures_s']/arms['A']['loaded_p95_including_failures_s'] if startup_valid else None
    report[campaign]=dict(arms=arms,pairs=paired,primary_paired_median_ratio=median(ratios) if ratios else None,
                          primary_wins=sum(x>1 for x in ratios),loaded_p95_ratio=latency_ratio,
                          loaded_latency_gate=latency_ratio<=1.10 if latency_ratio is not None else None,
                          throughput_confirmation_gate=len(ratios)==6 and median(ratios)>=1.10 and sum(x>1 for x in ratios)>=5 and arms['B']['payload_failures']==0,
                          loaded_successful_only_ratio=arms['B']['loaded_p95_successful_only_s']/arms['A']['loaded_p95_successful_only_s'] if arms['A']['loaded_p95_successful_only_s'] and arms['B']['loaded_p95_successful_only_s'] else None)
assert sum(v['arms'][a]['windows'] for v in report.values() for a in ('A','B'))==(20 if args.resumed else 16)
(BASE/('resumed-summary.json' if args.resumed else 'final-screen-summary.json')).write_text(json.dumps(report,indent=2)+'\n')
for campaign, value in report.items():
    print(campaign, json.dumps({k:v for k,v in value.items() if k!='arms'}))
    if args.resumed:
        for arm, metrics in value['arms'].items():
            print(campaign, arm, json.dumps({k:v for k,v in metrics.items() if k not in ('failure_details', 'idle', 'resources', 'client_sha256')}))
            print(campaign, arm, 'resources', json.dumps(metrics['resources']))
            print(campaign, arm, 'idle', json.dumps(metrics['idle']))
