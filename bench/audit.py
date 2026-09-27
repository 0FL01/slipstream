"""Print aggregate benchmark failures and idle evidence without secret material."""
import json
import sys

for name in sys.argv[1:]:
    print(name)
    with open(name) as source:
        for line in source:
            row = json.loads(line)
            if row.get('kind') in ('window', 'idle', 'startup') or (row.get('kind') == 'request' and not row.get('ok')):
                fields = ('kind', 'campaign', 'server', 'pair', 'position', 'arm', 'sample', 'ok',
                          'error', 'condition', 'requested', 'seconds', 'failures', 'timeout', 'queries',
                          'cpu_seconds', 'name', 'adapter_delta', 'resource_delta')
                print(json.dumps({key: row[key] for key in fields if key in row}))
