"""Aggregate paired evidence. Failures are zero throughput; failed controls have no ratio."""
import argparse
import json
from collections import defaultdict
from pathlib import Path
from statistics import median

from run import percentile


def summarize(rows, primary):
    condition, opcode, size = primary.split(':')
    size = int(size)
    window_failures = {(r['campaign'], r['phase'], r['carrier'], r['backend'], r['pair'],
                        r['arm'], r['variant'], r['server'], r['sample'])
                       for r in rows if r['kind'] == 'window' and not r['ok']}
    selected = [r for r in rows if r['kind'] == 'request' and
                (r['condition'], r['opcode'], r['requested']) == (condition, opcode, size)]
    windows = defaultdict(list)
    for row in selected:
        key = (row['campaign'], row['phase'], row['carrier'], row['backend'],
               row['pair'], row['arm'], row['variant'], row['server'], row['sample'])
        windows[key].append(row)
    groups = defaultdict(list)
    for key, values in windows.items():
        # Median per-window handles the fixed two-flow repetition without
        # pretending two correlated streams are separate confirmation pairs.
        groups[key[:4] + key[6:8]].append(dict(pair=key[4], arm=key[5], sample=key[8],
            ack_Bps=median(r['ack_Bps'] for r in values), ok=all(r['ok'] for r in values),
            failures=sum(not r['ok'] for r in values), timeouts=sum(r.get('timeout', False) for r in values)))
    result = []
    for key, values in sorted(groups.items()):
        result.append(dict(campaign=key[0], phase=key[1], carrier=key[2], backend=key[3],
                           variant=key[4], server=key[5], samples=len(values),
                           median_ack_Bps=median(r['ack_Bps'] for r in values),
                           failures=sum(r['failures'] for r in values),
                           timeouts=sum(r['timeouts'] for r in values)))
    pairs = defaultdict(dict)
    for key, values in windows.items():
        pair_key = key[:5]
        arm = key[5]
        if arm in pairs[pair_key]:
            # Duplicate sample/arm labels make evidence ambiguous, not selectable.
            pairs[pair_key][arm] = None
        else:
            pairs[pair_key][arm] = dict(variant=key[6], server=key[7],
                rate=median(r['ack_Bps'] for r in values),
                ok=all(r['ok'] for r in values) and key not in window_failures)
    paired = []
    for key, arms in sorted(pairs.items()):
        if key[1] == 'aa':
            continue
        a, b = arms.get('A'), arms.get('B')
        ratio = b['rate'] / a['rate'] if a and b and a['ok'] and a['rate'] > 0 else None
        paired.append(dict(campaign=key[0], phase=key[1], carrier=key[2], backend=key[3],
                           pair=key[4], ratio_B_over_A=ratio,
                           win=ratio is not None and ratio > 1,
                           control_valid=bool(a and a['ok'] and a['rate'] > 0),
                           candidate_ok=bool(b and b['ok'])))
    confirmation = defaultdict(list)
    for row in paired:
        if row['phase'] == 'confirm':
            confirmation[(row['campaign'], row['carrier'], row['backend'])].append(row)
    decisions = []
    for key, values in sorted(confirmation.items()):
        ratios = [r['ratio_B_over_A'] for r in values if r['ratio_B_over_A'] is not None]
        decisions.append(dict(campaign=key[0], carrier=key[1], backend=key[2], pairs=len(values),
                              median_ratio=median(ratios) if ratios else None,
                              wins=sum(r['win'] for r in values),
                              throughput_gate=len(values) == 6 and len(ratios) == 6 and
                              all(r['candidate_ok'] for r in values) and median(ratios) >= 1.1 and
                              sum(r['win'] for r in values) >= 5))
    loaded = defaultdict(list)
    for row in rows:
        if row['kind'] == 'request' and row['condition'] == 'loaded' and row['ok']:
            loaded[(row['campaign'], row['variant'], row['carrier'], row['phase'])].append(row['seconds'])
    calibration = []
    for key, values in sorted(groups.items()):
        if key[1] == 'aa':
            ordered = sorted(values, key=lambda r: r['sample'])
            rates = [r['ack_Bps'] for r in ordered]
            calibration.append(dict(campaign=key[0], carrier=key[2], backend=key[3], variant=key[4],
                                    samples=len(rates), rates_Bps=rates,
                                    sample2_over_sample1=rates[1] / rates[0]
                                    if len(rates) == 2 and rates[0] > 0 and ordered[0]['ok'] else None))
    counts = defaultdict(lambda: dict(requests=0, failures=0, timeouts=0, background_cancelled=0))
    for row in rows:
        if row['kind'] == 'request':
            count = counts[(row['campaign'], row['phase'], row['carrier'], row['backend'], row['variant'])]
            count['requests'] += 1
            if row.get('error') == 'CancelledError' and row['condition'] == 'background':
                count['background_cancelled'] += 1
            else:
                count['failures'] += not row['ok']
            count['timeouts'] += row.get('timeout', False)
    return dict(primary=primary, calibration=calibration, variants=result, pairs=paired, confirmation=decisions,
                totals=[dict(campaign=k[0], phase=k[1], carrier=k[2], backend=k[3], variant=k[4], **v)
                        for k, v in sorted(counts.items())],
                idle=[{k: v for k, v in row.items() if k in ('campaign', 'phase', 'carrier', 'backend',
                      'variant', 'pair', 'arm', 'sample', 'condition', 'seconds', 'queries_per_s',
                      'adapter_delta', 'resource_delta', 'ok', 'error')}
                      for row in rows if row['kind'] == 'idle'],
                resources=[{k: v for k, v in row.items() if k in ('campaign', 'phase', 'carrier', 'backend',
                           'variant', 'pair', 'arm', 'sample', 'resource_delta', 'resource_peaks')}
                           for row in rows if row['kind'] == 'window'],
                loaded_latency=[dict(campaign=k[0], variant=k[1], carrier=k[2], phase=k[3],
                                     p50_s=percentile(v, .5), p95_s=percentile(v, .95), max_s=max(v))
                                for k, v in sorted(loaded.items())],
                acceptance_note='Throughput gate only. Also require correctness, loaded p95 <=10% regression, and resource/idle review; C1/C0 and C1/R are separate campaigns.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', nargs='+')
    parser.add_argument('--primary', default='sequential:D:1048576',
                        help='Preselected condition:opcode:bytes; do not select after seeing results')
    args = parser.parse_args()
    rows = [json.loads(line) for path in args.results for line in Path(path).read_text().splitlines() if line]
    print(json.dumps(summarize(rows, args.primary), allow_nan=False, indent=2))
