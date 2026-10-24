#!/usr/bin/env python3
"""Run hardware-only Camera V2 evidence collection, including owned-process SIGKILL recovery."""
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


def service_state():
    result = subprocess.run(['systemctl', 'show', 'nvargus-daemon', '-p', 'NRestarts',
                             '-p', 'MainPID', '-p', 'ActiveState', '-p', 'ExecMainStatus',
                             '-p', 'InvocationID'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, check=False, timeout=5)
    return dict(returncode=result.returncode, properties=dict(
        line.split('=', 1) for line in result.stdout.splitlines() if '=' in line))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / '_output/build/arm64-debug')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--continuous-frames', type=int, default=3000)
    parser.add_argument('--cycles', type=int, default=10)
    parser.add_argument('--normal-only', action='store_true',
                        help='Validate normal lifecycle/timeout recovery without SIGKILL injection')
    args = parser.parse_args()
    if args.continuous_frames < 300 or args.cycles < 2:
        parser.error('continuous-frames >= 300 and cycles >= 2 are required')
    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    binaries = args.build_dir.resolve() / 'bin'
    start_time = time.time()
    report = dict(schema_version=2, started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  tests=[], physical_disconnect_recovery='not_tested',
                  note='No daemon restart or driver patch. SIGKILL targets only the probe started by this script.')
    report['binary_sha256'] = {name: hashlib.sha256((binaries / name).read_bytes()).hexdigest()
                               for name in ('camera-diag', 'camera-stress', 'libargus-capture-probe',
                                            'native_argus_capture_hardware_test')}
    report['daemon_before'] = service_state()

    def execute(name, binary, *options, expect_success=True):
        path = args.output_dir / f'{name}.json'
        argv = [str(binaries / binary), *map(str, options), '--output', str(path)]
        with (args.output_dir / f'{name}.stdout.txt').open('w') as log:
            try:
                process = subprocess.Popen(argv, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                try:
                    code = process.wait(timeout=max(60, args.continuous_frames / 30 * 2 + 60))
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                    code = 124
            except OSError as error:
                log.write(str(error))
                code = 127
        result = json.loads(path.read_text()) if path.exists() else {}
        success = code == 0 and (result.get('passed') is True or result.get('healthy') is True)
        failure_is_timeout = (result.get('passed') is False and result.get('resources_balanced') is True
                              and any(run.get('timeouts', 0) > 0 for run in result.get('runs', [])))
        item = dict(name=name, argv=argv, returncode=code, report=path.name,
                    expected_success=expect_success,
                    passed=success if expect_success else code == 1 and failure_is_timeout)
        report['tests'].append(item)
        print(f'{name}: passed={item["passed"]}', flush=True)
        return item['passed']

    execute('diagnostics', 'camera-diag', '--frames', 300)
    execute('raw-restart', 'camera-stress', '--cycles', args.cycles, '--frames', 60)
    execute('native-runtime-lifecycle', 'native_argus_capture_hardware_test')
    execute('native-continuous', 'libargus-capture-probe', '--frames', args.continuous_frames)
    execute('native-restart', 'libargus-capture-probe', '--cycles', args.cycles, '--frames', 60)
    execute('native-timeout', 'libargus-capture-probe', '--frames', 60, '--timeout-ms', 1,
            expect_success=False)
    execute('after-timeout', 'libargus-capture-probe', '--frames', 300)
    execute('diagnostics-after-native', 'camera-diag', '--frames', 300)
    normal_daemon = service_state()
    daemon_stable = (normal_daemon['returncode'] == 0 and
                     normal_daemon['properties'].get('ActiveState') == 'active' and
                     bool(report['daemon_before']['properties'].get('InvocationID')) and
                     normal_daemon['properties'].get('InvocationID') ==
                     report['daemon_before']['properties']['InvocationID'])
    report['tests'].append(dict(name='normal-daemon-stability', passed=daemon_stable))
    normal_count = len(report['tests'])
    report['normal_path_passed'] = all(item['passed'] for item in report['tests'])
    normal = dict(schema_version=2, scope='PR-03 Native Runtime normal lifecycle and timeout recovery',
                  passed=report['normal_path_passed'], tests=list(report['tests']),
                  binary_sha256=report['binary_sha256'], daemon_before=report['daemon_before'],
                  daemon_after=normal_daemon, started_utc=report['started_utc'],
                  finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  abnormal_exit='reported separately; not part of normal-path acceptance')
    (args.output_dir / 'camera_native_runtime_gate.json').write_text(json.dumps(normal, indent=2) + '\n')

    # Wait for evidence of actual frame production, then kill only our process group.
    # This is a separate acceptance scope; a daemon restart never counts as immediate recovery.
    if not args.normal_only:
        abrupt_report = args.output_dir / 'abrupt.json'
        abrupt_metadata = Path(str(abrupt_report) + '.frames.tsv')
        argv = [str(binaries / 'libargus-capture-probe'), '--frames', '9000', '--output', str(abrupt_report)]
        with (args.output_dir / 'abrupt.stdout.txt').open('w') as log:
            process = subprocess.Popen(argv, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            producing = False
            deadline = time.monotonic() + 20
            while process.poll() is None and time.monotonic() < deadline:
                if abrupt_metadata.exists() and len(abrupt_metadata.read_text().splitlines()) > 30:
                    producing = True
                    break
                time.sleep(0.1)
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
            code = process.wait(timeout=5)
        report['tests'].append(dict(name='abrupt-exit', passed=producing and code == -signal.SIGKILL,
                                    observed_frame_production=producing, returncode=code))
        immediate = execute('after-sigkill', 'libargus-capture-probe', '--frames', 300)
        report['daemon_after_sigkill'] = service_state()
        eventual = execute('after-sigkill-followup', 'libargus-capture-probe', '--frames', 300)
        report['daemon_after'] = service_state()
        daemon_comparable = (normal_daemon['returncode'] == 0 and report['daemon_after']['returncode'] == 0
                             and bool(normal_daemon['properties'].get('InvocationID'))
                             and bool(report['daemon_after']['properties'].get('InvocationID')))
        restarted = (daemon_comparable and normal_daemon['properties']['InvocationID'] !=
                     report['daemon_after']['properties']['InvocationID'])
        report['abrupt_recovery'] = ('unverified_daemon_state' if not daemon_comparable else
                                    'requires_daemon_restart' if restarted and eventual else
                                    'immediate' if immediate and eventual else 'unrecovered')
        report['tests'].append(dict(name='daemon-stability', passed=daemon_comparable and not restarted))
        abnormal_tests = report['tests'][normal_count:]
        abnormal = dict(schema_version=2, scope='SIGKILL abnormal exit recovery',
                        passed=all(item['passed'] for item in abnormal_tests),
                        tests=abnormal_tests, recovery=report['abrupt_recovery'],
                        daemon_before=normal_daemon, daemon_after=report['daemon_after'])
        report['abnormal_exit_passed'] = abnormal['passed']
        (args.output_dir / 'camera_abnormal_exit_gate.json').write_text(json.dumps(abnormal, indent=2) + '\n')
    else:
        report['abnormal_exit_passed'] = None
        report['abrupt_recovery'] = 'not_run'
    report['passed'] = all(item['passed'] for item in report['tests'])
    for source, selector in [('kernel', ['-k']), ('argus', ['-u', 'nvargus-daemon'])]:
        result = subprocess.run(['journalctl', '--no-pager', '-o', 'short-iso',
                                 '--since', f'@{start_time:.6f}', *selector],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, check=False, timeout=15)
        (args.output_dir / f'{source}.journal.txt').write_text(result.stdout)
        report[f'{source}_journal_returncode'] = result.returncode
    report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (args.output_dir / 'camera_v2_gate.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
