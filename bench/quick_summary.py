"""Print only aggregate checkpoint evidence."""
import json
import sys
from summarize import summarize
for path in sys.argv[1:]:
    with open(path) as source:
        result = summarize([json.loads(line) for line in source], 'sequential:D:1048576')
    print(json.dumps({key: result[key] for key in ('calibration', 'variants', 'confirmation', 'loaded_latency')}, indent=2))
