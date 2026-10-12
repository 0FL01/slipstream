"""Apply tracked fixes on configure; reject an unexpected dependency revision."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent
target = root.parent / "subprojects" / "picoquic"
def git(patch, *args):
    return subprocess.run(["git", "-C", str(target), "apply", *args, str(patch)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

for name in ("picoquic-path-identity.patch", "picoquic-app-credit.patch",
             "picoquic-poll-ack.patch", "picoquic-path-ack-context.patch",
             "picoquic-pto-ack-anchor.patch", "picoquic-stream-ack-range.patch",
             "picoquic-open-stream-repeat.patch"):
    patch = root / name
    if git(patch, "--reverse", "--check").returncode == 0:
        continue
    if git(patch, "--check").returncode != 0:
        sys.exit("picoquic source does not match tracked patch " + name)
    if git(patch).returncode:
        sys.exit("unable to apply tracked patch " + name)
