#!/usr/bin/env python3
"""Post-reboot BSP verification; default is read-only. Capture is explicit."""
import argparse
import array
import json
from pathlib import Path
import re
import subprocess
import sys
import time

from bsp import DEFAULT_OUT, LOCK, modinfo, run, sha, snapshot, strings, target_check, write_json
from deployment import idle


def command(argv, log, timeout=30):
    try:
        result = subprocess.run([str(a) for a in argv], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        Path(log).write_text(str(error))
        raise RuntimeError('capture timed out; inspect kernel/Argus logs, no restart attempted') from error
    Path(log).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError('verification command failed: ' + str(log))
    return result.stdout


def raw_frame(path, width, height, stride):
    data = Path(path).read_bytes()
    frame_size = stride * height
    if not frame_size or len(data) < frame_size or len(data) % frame_size:
        raise RuntimeError('RAW capture size does not match actual V4L2 bytesperline/height')
    pixels = array.array('H')
    frame = data[-frame_size:]
    for row in range(height):
        pixels.frombytes(frame[row * stride:row * stride + width * 2])
    if sys.byteorder != 'little':
        pixels.byteswap()
    return pixels


def analyse(black, white, bars, width, height, output):
    # Tegra RG10 is a 16-bit little-endian Bayer container. Preserve raw data;
    # normalize only the preview, not the acceptance measurements.
    maximum = max(white)
    minimum = min(black)
    scale = 64 if maximum > 1023 else 1
    black_mean = sum(black) / len(black)
    white_mean = sum(white) / len(white)
    if (white_mean - black_mean) / scale < 128:
        raise RuntimeError('solid-white / solid-black did not change actual RAW pixels')
    # Color bars should be invariant down the image for the same Bayer row phase.
    matches = total = 0
    for y in range(16, min(height - 2, 150), 2):
        for x in range(16, width - 16, 8):
            total += 1
            matches += abs(bars[y * width + x] - bars[(y + 2) * width + x]) <= scale * 2
    if not total or matches / total < 0.95 or (max(bars) - min(bars)) / scale < 128:
        raise RuntimeError('RAW color bars lack expected row-invariant spatial pattern')
    preview = bytes(min(255, value * 255 // max(1, max(bars))) for value in bars)
    Path(output).write_bytes(f'P5\n{width} {height}\n255\n'.encode() + preview)
    return {'black_mean': black_mean, 'white_mean': white_mean, 'container_scale': scale,
            'bars_row_invariance': matches / total, 'raw_pixels_changed': True,
            'bayer_mosaic_preview': str(output), 'human_pattern_viewing': 'pending'}


def capture(args, report):
    # idle() also requires Argus stopped and scans all camera FDs. No automatic
    # service stop/start and no simultaneous V4L2/Argus ownership.
    idle()
    capture_started = str(int(time.time()))
    device = '/dev/video0'
    controls = command(['v4l2-ctl', '-d', device, '--list-ctrls-menus'], args.out / 'controls-before.txt')
    required = ['test_pattern', 'test_pattern_red', 'test_pattern_greenr', 'test_pattern_blue', 'test_pattern_greenb']
    if any(name not in controls for name in required):
        raise RuntimeError('required BSP V4L2 Test Pattern controls are not exposed')
    report['raw'] = []
    try:
        for mode, width, height, fps in [(4, 1280, 720, 60000000), (2, 1920, 1080, 30000000)]:
            images = {}
            for pattern, menu, colour in [('black', 2, 0), ('white', 2, 1023), ('bars', 1, 0), ('disabled', 0, 0)]:
                destination = args.out / f'{width}x{height}-{pattern}.raw'
                settings = (f'sensor_mode={mode},bypass_mode=0,override_enable=1,gain=16,exposure=1000,'
                            f'frame_rate={fps},test_pattern={menu},test_pattern_red={colour},'
                            f'test_pattern_greenr={colour},test_pattern_blue={colour},test_pattern_greenb={colour}')
                command(['v4l2-ctl', '-d', device, f'--set-fmt-video=width={width},height={height},pixelformat=RG10',
                         '--set-ctrl=' + settings, '--stream-mmap=4', '--stream-skip=3', '--stream-count=1',
                         '--stream-to=' + str(destination)], args.out / f'{width}-{pattern}.txt')
                actual = command(['v4l2-ctl', '-d', device, '--get-fmt-video'], args.out / f'{width}-format.txt')
                stride_match = re.search(r'Bytes per Line\s*:\s*(\d+)', actual)
                if not stride_match or "'RG10'" not in actual:
                    raise RuntimeError('cannot confirm unpacked RG10 bytesperline')
                images[pattern] = raw_frame(destination, width, height, int(stride_match[1]))
            result = analyse(images['black'], images['white'], images['bars'], width, height,
                             args.out / f'{width}x{height}-bars.pgm')
            result.update(width=width, height=height, sensor_mode=mode,
                          controls_roundtrip=command(['v4l2-ctl', '-d', device, '--get-ctrl=gain,exposure,frame_rate,test_pattern'],
                                                     args.out / f'{width}-readback.txt'))
            report['raw'].append(result)
        if args.stress:
            for width, height, fps in [(1280, 720, 60), (1920, 1080, 30)]:
                destination = args.out / f'stress-{width}.json'
                command([args.stress, '--device', device, '--width', str(width), '--height', str(height),
                         '--fps', str(fps), '--frames', '120', '--cycles', '3', '--output', destination],
                        args.out / f'stress-{width}.txt', timeout=90)
                result = json.loads(destination.read_text())
                if not result.get('passed', result.get('healthy', False)):
                    raise RuntimeError('existing RAW stress tool did not report success')
        report['capture_passed'] = True
    finally:
        result = run(['v4l2-ctl', '-d', device, '--set-ctrl=test_pattern=0'], check=False)
        (args.out / 'pattern-reset.txt').write_text(result.stdout + result.stderr)
        if result.returncode:
            report['pattern_reset_failed'] = True
            report['capture_passed'] = False
    # Health requires available logs. Do not treat permission-denied as clean.
    journal = run(['journalctl', '-k', '-b', '--since', '@' + capture_started, '--no-pager'], check=False)
    (args.out / 'kernel-after.txt').write_text(journal.stdout + journal.stderr)
    if journal.returncode or re.search('permission denied|not seeing messages|No journal files', journal.stderr, re.I):
        raise RuntimeError('kernel log access unavailable; capture health cannot be confirmed')
    anomalies = [line for line in journal.stdout.splitlines()
                 if re.search('imx219|tegra.*(vi|csi|camrtc)', line, re.I) and
                 re.search('error|timeout|timed out|uncorr|CHANSEL_.*(FAULT|SHORT|NOMATCH)|failed', line, re.I)]
    report['kernel_anomalies'] = anomalies
    if anomalies:
        raise RuntimeError('kernel camera anomalies observed during RAW verification')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT / 'verify')
    parser.add_argument('--capture', action='store_true', help='write controls/stream RAW; root, idle camera and stopped Argus required')
    parser.add_argument('--stress', type=Path, help='optional existing camera-stress executable for sequential RAW cycles')
    parser.add_argument('--argus-probe', type=Path, help='separate run only: native probe after operator starts Argus')
    args = parser.parse_args()
    if args.capture and args.argus_probe:
        parser.error('RAW and Argus runs must be separate; never share a Sensor Session')
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    report = {'verification_scope': 'raw_test_pattern' if args.capture else 'native_argus_nv12' if args.argus_probe else 'load_overlay_controls_only',
              'read_only': not args.capture and not args.argus_probe, 'passed': False,
              'hardware_load_verified': False, 'capture_passed': False,
              'argus_passed': False, 'sigkill_recovery': 'not_tested',
              'ae_awb_controls': 'requires_explicit_native_tuning_acceptance',
              'rtsp_webrtc_regression': 'not_tested', 'qt': 'out_of_scope'}
    try:
        target_check()
        snapshot(args.out / 'preflight')
        loaded = Path('/sys/module/nv_imx219/version')
        if not loaded.exists() or loaded.read_text().strip() != LOCK['module_version']:
            raise RuntimeError('enhanced nv_imx219 is not loaded; disk modinfo alone is not load evidence')
        if strings(Path('/proc/device-tree') / LOCK['sensor_path'].lstrip('/') / 'cockpit,bsp-id') != ['imx219-a-r36.5.2-v1']:
            raise RuntimeError('BSP DTBO marker missing in live device tree; reboot/overlay application not verified')
        controls = run(['v4l2-ctl', '-d', '/dev/video0', '--list-ctrls-menus']).stdout
        (args.out / 'controls.txt').write_text(controls)
        if 'test_pattern' not in controls:
            raise RuntimeError('Test Pattern control absent on video node')
        report['hardware_load_verified'] = True
        if args.capture:
            capture(args, report)
        if args.argus_probe:
            if run(['systemctl', 'is-active', '--quiet', 'nvargus-daemon'], check=False).returncode:
                raise RuntimeError('start Argus explicitly before this separate probe run')
            destination = args.out / 'native-argus.json'
            command([args.argus_probe, '--device', '0', '--width', '1920', '--height', '1080',
                     '--fps', '30', '--frames', '120', '--cycles', '2', '--output', destination],
                    args.out / 'native-argus.txt', timeout=90)
            result = json.loads(destination.read_text())
            report['argus_passed'] = result.get('passed') is True
            if not report['argus_passed']:
                raise RuntimeError('Native libargus report did not pass')
        report['passed'] = not args.capture or report['capture_passed']
    except (OSError, RuntimeError, ValueError) as error:
        report['error'] = str(error)
    write_json(args.out / 'verify.json', report)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
