#!/usr/bin/env python3
"""Compare owned-process SIGKILL behavior for NativeArgusCapture and nvarguscamerasrc."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def run(argv, timeout=10):
    try:
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=timeout, check=False)
        return dict(argv=list(map(str, argv)), returncode=result.returncode, output=result.stdout + result.stderr)
    except (OSError, subprocess.TimeoutExpired) as error:
        return dict(argv=list(map(str, argv)), returncode=None, error=str(error))


def daemon():
    result = run(['systemctl', 'show', 'nvargus-daemon', '-p', 'NRestarts', '-p', 'MainPID',
                  '-p', 'ActiveState', '-p', 'InvocationID'])
    result['properties'] = dict(line.split('=', 1) for line in result.get('output', '').splitlines() if '=' in line)
    return result


def wait_active(timeout=15):
    deadline = time.monotonic() + timeout
    previous = None
    stable_since = None
    current = {}
    while time.monotonic() < deadline:
        current = daemon()
        properties = current['properties']
        identity = properties.get('InvocationID')
        active = current['returncode'] == 0 and properties.get('ActiveState') == 'active' and identity
        if active and identity == previous:
            if stable_since is not None and time.monotonic() - stable_since >= 1:
                return current
        else:
            stable_since = time.monotonic() if active else None
            previous = identity if active else None
        time.sleep(0.2)
    return current


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / '_output/build/arm64-debug')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--native-only', action='store_true', help='Only test the production NativeArgusCapture path')
    parser.add_argument('--cycles', type=int, default=1, help='Number of kill/immediate/followup cycles per backend')
    args = parser.parse_args()
    if args.cycles < 1 or args.cycles > 20:
        parser.error('cycles must be between 1 and 20')
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binaries = args.build_dir.resolve() / 'bin'
    native = binaries / 'libargus-capture-probe'
    preview = binaries / 'camera-preview-probe'
    report = dict(schema_version=1, base_commit=run(['git', 'rev-parse', 'HEAD']),
                  environment=dict(bsp=run(['cat', '/etc/nv_tegra_release']), kernel=run(['uname', '-a']),
                                   packages=run(['dpkg-query', '-W', 'nvidia-l4t-camera', 'nvidia-l4t-core']),
                                   core_pattern=run(['cat', '/proc/sys/kernel/core_pattern'])),
                  binary_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in ((native,) if args.native_only else (native, preview))},
                  started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), cases=[],
                  note='No manual daemon restart, supervisor, driver patch or failure suppression. Only owned child groups receive SIGKILL.')

    def verify(name, backend):
        if backend == 'native':
            argv = [str(native), '--frames', '60', '--output', str(out / f'{name}.json')]
        else:
            argv = [str(preview), '--backend', 'argus', '--device', 'nvargus://0', '--width', '1920',
                    '--height', '1080', '--fps', '30', '--frames', '60', '--timeout-ms', '15000',
                    '--config', str(ROOT / 'configs/development.yaml')]
        result = run(argv, timeout=40)
        (out / f'{name}.stdout.txt').write_text(result.get('output', result.get('error', '')))
        result['captured'] = (result['returncode'] == 0 and
                              ('passed=1' in result.get('output', '') if backend == 'native' else
                               'captured 60 frame(s)' in result.get('output', '')))
        return result

    backends = ('native',) if args.native_only else ('native', 'gstreamer')
    for sequence, backend in enumerate(backends * args.cycles):
        cycle = sequence // len(backends) + 1
        name = backend + (f'-{cycle}' if args.cycles > 1 else '')
        state = wait_active()
        if state.get('properties', {}).get('ActiveState') != 'active':
            report['cases'].append(dict(backend=backend, error='daemon unavailable; comparison skipped'))
            break
        started = time.time()
        abrupt = out / f'{name}-abrupt.json'
        if backend == 'native':
            argv = [str(native), '--frames', '9000', '--output', str(abrupt)]
        else:
            argv = ['gst-launch-1.0', '-v', 'nvarguscamerasrc', 'sensor-id=0', '!',
                    'video/x-raw(memory:NVMM),format=NV12,width=1920,height=1080,framerate=30/1', '!',
                    'identity', 'silent=false', 'signal-handoffs=false', '!', 'fakesink', 'sync=false']
        log_path = out / f'{name}-abrupt.stdout.txt'
        observed = 0
        with log_path.open('w') as log:
            process = subprocess.Popen(argv, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 25
                while process.poll() is None and time.monotonic() < deadline:
                    if backend == 'native':
                        metadata = Path(str(abrupt) + '.frames.tsv')
                        observed = max(0, len(metadata.read_text().splitlines()) - 1) if metadata.exists() else 0
                    else:
                        observed = log_path.read_text(errors='replace').count('last-message = chain')
                    if observed >= 30 and time.time() - started >= 8:
                        break
                    time.sleep(0.1)
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGKILL)
                code = process.wait(timeout=5)
        case = dict(backend=backend, cycle=cycle, argv=argv, daemon_before=state, observed_frames=observed,
                    seconds_before_kill=time.time() - started, returncode=code,
                    fault_injected=observed >= 30 and code == -signal.SIGKILL)
        case['immediate'] = verify(f'{name}-immediate', backend)
        case['daemon_after_immediate'] = daemon()
        case['daemon_stable_before_followup'] = wait_active()
        case['followup'] = verify(f'{name}-followup', backend)
        case['daemon_after'] = daemon()
        before_id = state['properties'].get('InvocationID')
        after_id = case['daemon_after']['properties'].get('InvocationID')
        comparable = state['returncode'] == 0 and case['daemon_after']['returncode'] == 0 and before_id and after_id
        case['daemon_restarted'] = bool(before_id != after_id) if comparable else None
        case['immediate_recovery_passed'] = bool(case['fault_injected'] and case['immediate']['captured'] and comparable and before_id == after_id)
        for source, selector in [('argus', ['-u', 'nvargus-daemon']), ('kernel', ['-k'])]:
            result = run(['journalctl', '--no-pager', '-o', 'short-iso', '--since', f'@{started:.6f}', *selector])
            (out / f'{name}.{source}.journal.txt').write_text(result.get('output', result.get('error', '')))
        if backend == 'native':
            evidence = out / f'{name}-followup.json'
            if evidence.exists():
                case['native_stream_type'] = json.loads(evidence.read_text()).get('stream_type')
        report['cases'].append(case)
        (out / f'{name}.exit.json').write_text(json.dumps(case, indent=2) + '\n')
        print(f'{name}: immediate={case["immediate_recovery_passed"]} daemon_restarted={case["daemon_restarted"]} followup={case["followup"]["captured"]}', flush=True)
    report['comparison_completed'] = len(report['cases']) == len(backends) * args.cycles and all(c.get('fault_injected') for c in report['cases'])
    report['production_switch_ready'] = report['comparison_completed'] and all(c.get('immediate_recovery_passed') and c.get('followup', {}).get('captured') for c in report['cases'])
    report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / 'camera_exit_comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['production_switch_ready'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
