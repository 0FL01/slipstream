"""Aggregate immutable balanced pairs, retaining failures and paired uncertainty.

Bootstrap intervals describe these small samples, not carrier-wide guarantees.
No adoption threshold is imposed: inspect failure phases and each workload first.
"""
import argparse
import json
import math
from pathlib import Path
import random
import statistics


def summary(values):
    if not values:
        return None
    ordered = sorted(values)
    return dict(n=len(values), p50=statistics.median(values),
                p95=ordered[math.ceil(.95 * len(values)) - 1], maximum=max(values))


def paired(values):
    values = [v for v in values if v is not None]
    if not values:
        return dict(n=0)
    rng = random.Random(0)
    boot = sorted(statistics.median(rng.choices(values, k=len(values))) for _ in range(10000))
    return dict(n=len(values), wins=sum(v > 0 for v in values), median_percent=statistics.median(values),
                range_percent=[min(values), max(values)], bootstrap95_percent=[boot[249], boot[9749]])


def read_window(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    manifests = [r for r in rows if r['kind'] == 'manifest']
    exits = [r for r in rows if r['kind'] == 'host_exit']
    cleanup = [r for r in rows if r['kind'] == 'cleanup']
    windows = [r for r in rows if r['kind'] == 'window']
    if (len(manifests) != 1 or len(exits) != 1 or len(windows) != 1 or len(cleanup) != 1
            or cleanup[0]['child_alive'] or cleanup[0]['workers_alive']):
        raise ValueError('Incomplete/harness-failed window: ' + path.name)
    requests = {(r['kind'], r['condition'], r['requested']): r for r in rows
                 if r['kind'] in {'up', 'down'} and r['condition'] != 'background'}
    startup_failed = not requests and not windows[0]['ok']
    expected = 7 if manifests[0].get('workload') == 'short-parallel' else 10
    if not startup_failed and len(requests) != expected:
        raise ValueError('Incomplete workload: ' + path.name)
    if any(r.get('fixture_version', 2) != 2 for r in requests.values()):
        raise ValueError('Fixture version mismatch')
    return manifests[0], requests, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--pairs', type=int, required=True)
    args = parser.parse_args()
    if Path(args.label).name != args.label or args.pairs < 1:
        parser.error('Invalid label/pairs')
    all_rows, ratios, manifests = {'control': [], 'candidate': []}, {}, {}
    for number in range(1, args.pairs + 1):
        current = {}
        for variant in all_rows:
            path = args.directory / f'{args.label}-p{number}-{variant}.jsonl'
            manifest, requests, rows = read_window(path)
            if variant in manifests and manifest != manifests[variant]:
                raise ValueError('Manifest drift within variant')
            manifests[variant] = manifest
            current[variant] = requests
            all_rows[variant].extend(rows)
        a, b = current['control'], current['candidate']
        for key in a.keys() | b.keys():
            control = a.get(key, {}).get('ack_Bps', 0)
            candidate = b.get(key, {}).get('ack_Bps', 0)
            ratios.setdefault('-'.join(map(str, key)), []).append(100 * (candidate / control - 1) if control else None)
    frozen = [{k: v for k, v in m.items() if k not in {'native_sha256', 'order', 'topology', 'path_layout', 'domain_layout', 'connections', 'partition'}}
              for m in manifests.values()]
    if frozen[0] != frozen[1]:
        raise ValueError('Control/candidate workload, adapter or fixture drift')
    result = dict(label=args.label, pairs=args.pairs, paired={k: paired(v) for k, v in sorted(ratios.items())}, variants={})
    for variant, rows in all_rows.items():
        requests = [r for r in rows if r['kind'] in {'up', 'down'} and r['condition'] != 'background']
        ups = [r for r in requests if r['kind'] == 'up']
        groups = sorted({(r['condition'], r['requested']) for r in ups})
        result['variants'][variant] = dict(
            startup=summary([r['seconds'] for r in rows if r['kind'] == 'startup']),
            failed_windows=sum(not r['ok'] for r in rows if r['kind'] == 'window'),
            background_failures=[{k: r[k] for k in ('phase', 'seconds', 'error')}
                                 for r in rows if r.get('condition') == 'background'
                                 and not r['ok'] and r.get('error') != 'Cancelled'],
            parallel_group_seconds=summary([r['seconds'] for r in rows if r['kind'] == 'parallel_group']),
            failed_requests=[{k: r[k] for k in ('kind', 'condition', 'requested', 'phase', 'seconds', 'ack_Bps', 'submitted')}
                             for r in requests if not r['ok']],
            upload_seconds={f'{c}-{n}': summary([r['seconds'] for r in ups if (r['condition'], r['requested']) == (c, n)])
                            for c, n in groups},
            tls_seconds=summary([r['tls_s'] - r['connect_s'] for r in ups if r['tls_s'] is not None]),
            max_drain_seconds=summary([r['max_drain_s'] for r in ups]))
    print(json.dumps(result, allow_nan=False))


if __name__ == '__main__':
    main()
