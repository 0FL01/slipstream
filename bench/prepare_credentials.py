"""Convert a transferred raw test token to an owner-only runner input, no output."""
import json
import os
from pathlib import Path

root = Path(__file__).parent
token = (root / '.token').read_bytes()
if len(token) != 16:
    raise ValueError('token length')
fd = os.open(root / '.credentials.json', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, 'w') as output:
    json.dump({'flow_token': token.hex()}, output)
os.chmod(root / '.token', 0o600)
