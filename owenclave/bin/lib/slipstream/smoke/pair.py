"""Balanced native-only paired screen. Failures remain in immutable JSONL windows."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import sys
import time
from report import read_window
from device import TOPOLOGIES

HERE = Path(__file__).resolve().parent


def metrics(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    requests = [r for r in rows if r.get('condition') in {'sequential', 'loaded', 'recovery'}]
    up = [r for r in requests if r['kind'] == 'up']
    return dict(ok=any(r.get('kind') == 'window' and r.get('ok') for r in rows),
                failures=sum(not r['ok'] for r in requests),
                startup=next((r['seconds'] for r in rows if r['kind'] == 'startup'), None),
                up={r['condition'] + '-' + str(r['requested']): r['ack_Bps'] for r in up},
                up_seconds={r['condition'] + '-' + str(r['requested']): r['seconds'] for r in up},
                down=next((r['ack_Bps'] for r in requests if r['kind']=='down' and r['condition']=='sequential'), 0),
                tls_max=max((r.get('tls_s') or 0 for r in up), default=0),
                drops=[p['dropped'] for r in rows if r['kind']=='cleanup' for p in r['paths']])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--control', required=True)
    p.add_argument('--candidate', required=True)
    p.add_argument('--candidate-native', action='store_true', help='Candidate path is ELF, not APK')
    p.add_argument('--candidate-order', default='accepted', choices=['accepted', 'reverse'])
    p.add_argument('--candidate-topology', default='accepted', choices=TOPOLOGIES)
    p.add_argument('--pair', type=int, required=True)
    p.add_argument('--label', required=True)
    p.add_argument('--secret-file', required=True)
    p.add_argument('--cert', required=True)
    p.add_argument('--fixture-cert', required=True)
    p.add_argument('--fixture-host', required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    args = p.parse_args()
    if Path(args.label).name != args.label or args.pair < 1:
        p.error('Invalid label/pair')
    result = {}
    for variant in (['control', 'candidate'] if args.pair % 2 else ['candidate', 'control']):
        time.sleep(10)
        path = args.output_dir / f'{args.label}-p{args.pair}-{variant}.jsonl'
        source_option = '--native' if variant == 'candidate' and args.candidate_native else '--apk'
        command = [sys.executable, str(HERE / 'host.py'), '--mode', 'native', '--serial', args.serial,
                   source_option, getattr(args, variant), '--order', args.candidate_order if variant=='candidate' else 'accepted',
                   '--topology', args.candidate_topology if variant == 'candidate' else 'accepted',
                   '--secret-file', args.secret_file, '--cert', args.cert, '--fixture-cert', args.fixture_cert,
                   '--fixture-host', args.fixture_host, '--output', str(path)]
        proc = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        value = metrics(path)
        result[variant] = value
        print(json.dumps(dict(label=args.label, pair=args.pair, variant=variant, rc=proc.returncode, **value)), flush=True)
        if proc.returncode not in (0, 1):
            raise RuntimeError('Harness precondition/runtime failure; not a carrier comparison')
        read_window(path)
    control, candidate = result['control'], result['candidate']
    ratios = {k: 100*(candidate['up'].get(k, 0)/v-1) if v else None for k,v in control['up'].items()}
    finite = [v for v in ratios.values() if v is not None]
    print(json.dumps(dict(label=args.label, pair=args.pair, upload_percent=ratios,
                          median_workload_percent=statistics.median(finite) if finite else None,
                          down_percent=100*(candidate['down']/control['down']-1) if control['down'] else None)), flush=True)


if __name__ == '__main__':
    main()
