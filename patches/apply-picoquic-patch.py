"""Apply tracked fixes on configure; reject an unexpected dependency revision."""
import pathlib
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent
target = root.parent / "subprojects" / "picoquic"
def git(patch, *args):
    return subprocess.run(["git", "-C", str(target), "apply", *args, str(patch)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

names = ("picoquic-path-identity.patch", "picoquic-app-credit.patch",
             "picoquic-poll-ack.patch", "picoquic-path-ack-context.patch",
             "picoquic-pto-ack-anchor.patch", "picoquic-stream-ack-range.patch",
             "picoquic-open-stream-repeat.patch", "picoquic-rtt-epoch-context.patch",
             "picoquic-low-rtt-repeat-window.patch", "picoquic-ack-slot-repeat.patch",
             "picoquic-ack-repeat-cursor.patch", "picoquic-poll-response-ack.patch",
             "picoquic-preemptive-copy-bound.patch", "picoquic-preemptive-data-credit.patch",
             "picoquic-preemptive-padding.patch")

def complete_stack_applied():
    # Later overlays may replace earlier hunk context (or added lines). Probe
    # the entire stack in reverse order on a copy, never by undoing live files.
    paths = {line[6:] for name in names for line in (root / name).read_text().splitlines()
             if line.startswith("+++ b/")}
    with tempfile.TemporaryDirectory(prefix=".picoquic-patch-check-", dir=target.parent) as directory:
        probe = pathlib.Path(directory)
        for path in paths:
            dest = probe / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(target / path, dest)
        subprocess.run(["git", "init", "--quiet", "--template=", str(probe)], check=True)
        for name in reversed(names):
            result = subprocess.run(["git", "-C", str(probe), "apply", "--reverse", str(root / name)],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            if result.returncode != 0:
                return False
    return True

if complete_stack_applied():
    sys.exit(0)

for name in names:
    patch = root / name
    if git(patch, "--reverse", "--check").returncode == 0:
        continue
    if git(patch, "--check").returncode != 0:
        sys.exit("picoquic source does not match tracked patch " + name)
    if git(patch).returncode:
        sys.exit("unable to apply tracked patch " + name)
