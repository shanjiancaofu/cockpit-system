#!/usr/bin/env python3
"""Freeze the existing BGRx Hardware ISP path, with inspectable raw evidence."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time


ROOT = Path(__file__).resolve().parents[2]


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def command(argv, timeout=15):
    try:
        result = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout, check=False)
        return dict(argv=[str(x) for x in argv], returncode=result.returncode,
                    output=result.stdout)
    except (OSError, subprocess.TimeoutExpired) as error:
        return dict(argv=[str(x) for x in argv], returncode=None, error=str(error))


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stop(process):
    if process is not None and process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / '_output/build/arm64-debug')
    parser.add_argument('--output-dir', type=Path, default=ROOT / '_output/runtime/camera-v1-baseline')
    parser.add_argument('--frames', type=int, default=300)
    parser.add_argument('--warmup-frames', type=int, default=60)
    parser.add_argument('--timeout-ms', type=int, default=30000)
    args = parser.parse_args()
    if args.frames < 2 or args.timeout_ms <= 0 or args.warmup_frames < 0:
        parser.error('frames must be >= 2, warmup-frames >= 0 and timeout-ms > 0')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    probe = args.build_dir.resolve() / 'bin/camera-preview-probe'
    config = ROOT / 'configs/development.yaml'
    report = dict(schema_version=1, started_utc=utc_now(), backend='argus',
                  pipeline='V1 nvarguscamerasrc -> BGRx CPU frame',
                  git=command(['git', 'rev-parse', 'HEAD']),
                  worktree=command(['git', 'status', '--porcelain']),
                  probe_sha256=sha256(probe), config_sha256=sha256(config),
                  fps_method='steady clock callback arrival after warmup; includes V1 delivery behavior, not sensor timestamps',
                  resource_method='process lifetime CPU/RSS including initialization, warmup, measurement and stop',
                  sequence_gaps=None, buffer_errors=None,
                  unavailable_metrics_reason='V1 preview API does not expose capture sequence or buffer flags',
                  environment={}, runs=[])
    for name, argv in {
        'bsp': ['cat', '/etc/nv_tegra_release'], 'kernel': ['uname', '-a'],
        'devices': ['v4l2-ctl', '--list-devices'],
        'video0': ['v4l2-ctl', '--all', '-d', '/dev/video0'],
        'modes': ['v4l2-ctl', '--list-formats-ext', '-d', '/dev/video0'],
        'media': ['media-ctl', '-p'], 'power_mode': ['nvpmodel', '-q'],
        'packages': ['dpkg-query', '-W', 'nvidia-l4t-core', 'nvidia-l4t-camera', 'nvidia-jetpack'],
    }.items():
        report['environment'][name] = command(argv)
    for width, height in [(1280, 720), (1920, 1080)]:
        name = f'{width}x{height}'
        argv = [str(probe), '--backend', 'argus', '--device', 'nvargus://0',
                '--width', str(width), '--height', str(height), '--fps', '30',
                '--frames', str(args.frames), '--timeout-ms', str(args.timeout_ms),
                '--warmup-frames', str(args.warmup_frames),
                '--config', str(config)]
        run = dict(width=width, height=height, requested_fps=30, requested_frames=args.frames,
                   warmup_frames=args.warmup_frames, argv=argv, started_utc=utc_now(), process_samples=[])
        start = time.time()
        telemetry = None
        with (args.output_dir / f'{name}.tegrastats.txt').open('w') as telemetry_log, \
             (args.output_dir / f'{name}.probe.txt').open('w') as probe_log:
            try:
                telemetry = subprocess.Popen(['tegrastats', '--interval', '500'],
                                             stdout=telemetry_log, stderr=subprocess.STDOUT,
                                             start_new_session=True)
                run['tegrastats_status'] = 'started'
            except OSError as error:
                run['tegrastats_status'] = str(error)
            process = None
            try:
                process = subprocess.Popen(argv, cwd=ROOT, stdout=probe_log,
                                           stderr=subprocess.STDOUT, start_new_session=True)
                deadline = time.monotonic() + args.timeout_ms / 1000 + 15
                while process.poll() is None:
                    try:
                        status = Path(f'/proc/{process.pid}/status').read_text()
                        stat = Path(f'/proc/{process.pid}/stat').read_text().rsplit(')', 1)[1].split()
                        rss = re.search(r'^VmRSS:\s+(\d+)', status, re.M)
                        run['process_samples'].append(dict(
                            elapsed_s=time.time() - start, rss_kib=int(rss[1]) if rss else None,
                            cpu_ticks=int(stat[11]) + int(stat[12]),
                            clock_ticks_per_second=os.sysconf('SC_CLK_TCK')))
                    except (OSError, ValueError):
                        pass
                    if time.monotonic() > deadline:
                        run['watchdog_timeout'] = True
                        stop(process)
                        break
                    time.sleep(0.25)
                run['returncode'] = process.returncode
            except OSError as error:
                run.update(returncode=None, error=str(error))
            finally:
                stop(process)
                stop(telemetry)
        output = (args.output_dir / f'{name}.probe.txt').read_text()
        metrics = dict(re.findall(r'\b(fps|steady_fps|process_cpu_ms|process_wall_ms|process_cpu_percent|max_rss_kib)=([0-9.]+)', output))
        run['metrics'] = {key: float(value) for key, value in metrics.items()}
        captured = re.search(r'captured (\d+) frame\(s\).*size=(\d+)x(\d+)', output)
        run['captured_frames'] = int(captured[1]) if captured else 0
        run['actual_size'] = [int(captured[2]), int(captured[3])] if captured else None
        run['passed'] = (run['returncode'] == 0 and run['captured_frames'] >= args.frames
                         and run['actual_size'] == [width, height]
                         and 28.5 <= run['metrics'].get('steady_fps', 0) <= 31.5)
        run['logs'] = dict(probe=f'{name}.probe.txt', tegrastats=f'{name}.tegrastats.txt')
        run['tegrastats_returncode'] = telemetry.returncode if telemetry else None
        run['kernel_log'] = command(['journalctl', '-k', '--no-pager', '-o', 'short-iso',
                                     '--since', f'@{start:.6f}', '--until', f'@{time.time():.6f}'])
        run['argus_log'] = command(['journalctl', '-u', 'nvargus-daemon', '--no-pager',
                                    '-o', 'short-iso', '--since', f'@{start:.6f}',
                                    '--until', f'@{time.time():.6f}'])
        run['finished_utc'] = utc_now()
        report['runs'].append(run)
    report['finished_utc'] = utc_now()
    report['passed'] = all(run['passed'] for run in report['runs'])
    path = args.output_dir / 'camera_v1_baseline.json'
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(path)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
