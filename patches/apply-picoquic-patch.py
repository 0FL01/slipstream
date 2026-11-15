"""Apply tracked fixes on configure; reject an unexpected dependency revision."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent
def git(target, patch, *args):
    return subprocess.run(["git", "-C", str(target), "apply", "--unidiff-zero", *args, str(patch)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

for dependency, name in (
    ("subprojects/picoquic", "picoquic-path-identity.patch"),
    ("subprojects/picoquic", "picoquic-app-credit.patch"),
    ("subprojects/picoquic", "picoquic-reset-credit.patch"),
    ("extern/SPCDNS", "spcdns-malformed-query.patch"),
):
    target = root.parent / dependency
    patch = root / name
    if git(target, patch, "--reverse", "--check").returncode == 0:
        continue
    if git(target, patch, "--check").returncode != 0:
        sys.exit(dependency + " source does not match tracked patch " + name)
    if git(target, patch).returncode:
        sys.exit("unable to apply tracked patch " + name)
