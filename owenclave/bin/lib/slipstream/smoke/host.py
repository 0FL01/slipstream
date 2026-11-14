"""Single-owner ADB runner. Token values use pipes only; output files never overwrite.

Requires debuggable Termux with Python >=3.11. Does not modify profiles or VPN state:
native runs require stopped VPN; gvisor runs require one already-connected child.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import time
import zipfile
from device import TOPOLOGIES

HERE = Path(__file__).resolve().parent
PREFIX = '/data/data/com.termux/files/usr'
REMOTE = '/data/data/com.termux/files/home/owenclave-smoke'
APP = 'org.owenewans.owenclave'


def adb(args, *command, **kw):
    return subprocess.run(['adb', '-s', args.serial, *command], capture_output=True,
                          check=True, timeout=kw.pop('timeout', 30), **kw).stdout


def termux(command):
    return ['run-as', 'com.termux', '/system/bin/env',
            'PATH=' + PREFIX + '/bin:/system/bin', 'HOME=/data/data/com.termux/files/home',
            'TMPDIR=' + PREFIX + '/tmp', *command]


def install(args, name, data, mode=0o600):
    # Generated runtime copies, never credential files on the phone.
    code = ('import os,pathlib,sys; p=pathlib.Path(sys.argv[1]); '
            'p.parent.mkdir(mode=0o700,parents=True,exist_ok=True); '
            'p.write_bytes(sys.stdin.buffer.read(int(sys.argv[3]))); os.chmod(p,int(sys.argv[2]))')
    adb(args, 'shell', '-T', shlex.join(termux([PREFIX + '/bin/python', '-c', code,
                                           REMOTE + '/' + name, str(mode), str(len(data))])), input=data)


def preflight(args):
    wifi = adb(args, 'shell', 'settings', 'get', 'global', 'wifi_on').decode().strip()
    radio = adb(args, 'shell', 'getprop', 'gsm.network.type').decode().strip()
    names = adb(args, 'shell', 'ps', '-A', '-o', 'NAME').decode().splitlines()
    children = sum(n.strip() in {'libslipstream.so', 'libmasterdns.so', 'slipstream-client', 'native-client'} for n in names)
    connectivity = adb(args, 'shell', 'dumpsys', 'connectivity').decode()
    # Android network-agent summaries, not arbitrary mentions of the VPN capability.
    vpn = active_vpn(connectivity)
    battery = {}
    for line in adb(args, 'shell', 'dumpsys', 'battery').decode().splitlines():
        if ': ' in line:
            k, v = line.strip().split(': ', 1)
            if k in {'level', 'temperature', 'USB powered'}:
                battery[k] = v
    if wifi != '0' or 'LTE' not in radio or int(battery['level']) <= 20 or int(battery['temperature']) >= 420:
        raise RuntimeError('Requires cool charged phone, Wi-Fi off and LTE')
    if children != (1 if args.mode == 'gvisor' else 0) or vpn != (args.mode == 'gvisor'):
        raise RuntimeError('VPN/child ownership precondition failed')
    apk_path = adb(args, 'shell', 'pm', 'path', APP).decode().strip().removeprefix('package:')
    apk_hash = adb(args, 'shell', 'sha256sum', apk_path).decode().split()[0]
    return dict(kind='preflight', wifi=wifi, radio=radio, vpn=vpn, children=children,
                 battery=battery, installed_apk_sha256=apk_hash, utc=time.strftime('%FT%TZ', time.gmtime()))


def active_vpn(connectivity):
    for line in connectivity.splitlines():
        if 'NetworkAgentInfo{' in line and 'Transports: ' in line:
            transports = line.split('Transports: ', 1)[1].split()[0].split('|')
            if 'VPN' in transports:
                return True
    return False


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--mode', choices=['native', 'gvisor'], required=True)
    source = p.add_mutually_exclusive_group()
    source.add_argument('--apk', type=Path, help='Extract native only, does not install APK')
    source.add_argument('--native', type=Path)
    p.add_argument('--cert', type=Path, required=True)
    p.add_argument('--fixture-cert', type=Path, required=True)
    p.add_argument('--fixture-host', required=True)
    p.add_argument('--secret-file', type=Path, required=True, help='0600 JSON flow_token/fixture_token; never emitted')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--order', choices=['accepted', 'reverse'], default='accepted')
    p.add_argument('--topology', choices=TOPOLOGIES, default='accepted')
    p.add_argument('--connections', type=int, choices=[1, 2], default=1)
    p.add_argument('--partition', action='store_true')
    p.add_argument('--workload', choices=['full', 'short-parallel'], default='full')
    p.add_argument('--domains', choices=['legacy', 'old', 'new', 'split', 'split-reverse'], default='legacy')
    p.add_argument('--deadline', type=float, default=40)
    p.add_argument('--egress', choices=['direct', 'warp'], default='direct')
    args = p.parse_args()
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    if args.mode == 'native' and not (args.apk or args.native):
        p.error('Native mode requires --apk or --native')
    if args.secret_file.stat().st_mode & 0o077:
        p.error('Credential file must be private')
    secret = json.loads(args.secret_file.read_bytes())
    lock_path = Path('/tmp/owenclave-smoke-' + hashlib.sha256(args.serial.encode()).hexdigest()[:12] + '.lock')
    with lock_path.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with args.output.open('x') as output:
            def emit(row):
                value = json.dumps(row, allow_nan=False)
                output.write(value + '\n')
                output.flush()
                print(value, flush=True)
            try:
                emit(preflight(args))
                install(args, 'device.py', (HERE / 'device.py').read_bytes())
                install(args, 'carrier.crt', args.cert.read_bytes())
                install(args, 'fixture.crt', args.fixture_cert.read_bytes())
                if args.mode == 'native':
                    if args.apk:
                        with zipfile.ZipFile(args.apk) as archive:
                            native = archive.read('lib/arm64-v8a/libslipstream.so')
                    else:
                        native = args.native.read_bytes()
                    install(args, 'native-client', native, 0o700)
                    adb(args, 'shell', '-T', shlex.join(termux([REMOTE + '/native-client', '--help'])))
                    emit(dict(kind='native_install', sha256=hashlib.sha256(native).hexdigest()))
                command = termux([PREFIX + '/bin/python', REMOTE + '/device.py', '--mode', args.mode,
                    '--native', REMOTE + '/native-client', '--cert', REMOTE + '/carrier.crt',
                    '--fixture-cert', REMOTE + '/fixture.crt', '--fixture-host', args.fixture_host,
                    '--order', args.order, '--topology', args.topology, '--domains', args.domains,
                    '--deadline', str(args.deadline), '--egress', args.egress,
                    '--connections', str(args.connections), '--workload', args.workload] +
                    (['--partition'] if args.partition else []))
                proc = subprocess.Popen(['adb', '-s', args.serial, 'shell', '-T', shlex.join(command)],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                try:
                    proc.stdin.write(json.dumps(secret).encode() + b'\n')
                    proc.stdin.flush()
                    # Device runtime has per-request deadlines; no payload/config output is allowed.
                    for line in proc.stdout:
                        emit(json.loads(line))
                    code = proc.wait(timeout=10)
                finally:
                    proc.stdin.close()
                    if proc.poll() is None:
                        # EOF cancels the device owner. Do not race a second cancellation
                        # against its child/worker cleanup; TERM is only a fallback.
                        try:
                            proc.wait(timeout=8)
                        except subprocess.TimeoutExpired:
                            stop = 'import os,signal; from pathlib import Path; p=Path("' + REMOTE + '/owner.pid"); ' \
                                   'os.kill(int(p.read_text()),signal.SIGTERM) if p.exists() else None'
                            adb(args, 'shell', '-T', shlex.join(termux([PREFIX + '/bin/python', '-c', stop])))
                            try:
                                proc.wait(timeout=8)
                            except subprocess.TimeoutExpired:
                                proc.terminate()
                                proc.wait(timeout=5)
                emit(dict(kind='host_exit', rc=code, postflight=preflight(args)))
                return code
            except BaseException as error:
                emit(dict(kind='host_failure', error=type(error).__name__))
                raise


if __name__ == '__main__':
    sys.exit(main())
