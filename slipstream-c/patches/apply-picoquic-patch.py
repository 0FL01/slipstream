"""Verify already-vendored fixes without modifying dependency source."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent
target = root.parent / "subprojects" / "picoquic"
def check(patch):
    return subprocess.run(["patch", "--dry-run", "--batch", "--reverse", "-p1", "-i", str(patch)], cwd=target,
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

for name in ("picoquic-path-identity.patch", "picoquic-app-credit.patch"):
    patch = root / name
    if check(patch).returncode == 0:
        continue
    sys.exit("vendored picoquic is missing effective patch " + name)
