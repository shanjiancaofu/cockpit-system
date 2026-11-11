#!/usr/bin/env python3
"""Opt-in Jetson 720P media/restart acceptance. No VLC, sudo or downloads. Camera SIGKILL requires --kill-camera-child.

Requires PyYAML, a WebRTC-enabled build and explicitly supplied browser drivers.
Creates isolated config/evidence; controls only services/clients spawned by this script.
"""
import argparse
import datetime
import hashlib
import base64
import functools
import http.server
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import threading
import time
import types
import urllib.parse
import urllib.request

import yaml

ROOT = Path(__file__).resolve().parents[2]


def verify_movie(movie, driver, output):
    """Visible Chrome native MP4 player, served only on local loopback."""
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(movie.parent))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    snapshot = """const v=document.querySelector('video'); return v ? {
      width:v.videoWidth,height:v.videoHeight,time:v.currentTime,duration:v.duration,
      frames:v.getVideoPlaybackQuality().totalVideoFrames,error:v.error?.code||null,
      paused:v.paused} : null;"""
    try:
        driver.command('POST', '/url', {'url': 'http://127.0.0.1:' + str(server.server_port) +
                                       '/' + urllib.parse.quote(movie.name)})
        deadline = time.monotonic() + 15
        first = None
        while time.monotonic() < deadline:
            first = driver.script(snapshot)
            if first and first['width'] == 1280 and first['height'] == 720:
                break
            time.sleep(.1)
        if not first or first['width'] != 1280:
            raise RuntimeError('MP4 player failed to load 720P')
        driver.script("const v=document.querySelector('video'); v.muted=true; v.currentTime=0; "
                      "const done=arguments[arguments.length-1];v.play().then(()=>done(true),e=>done(String(e)));", True)
        first = driver.script(snapshot)
        time.sleep(2)
        last = driver.script(snapshot)
        if last['error'] or last['time'] <= first['time'] + 1 or last['frames'] <= first['frames']:
            raise RuntimeError('MP4 visible playback did not advance')
        output.with_suffix('.png').write_bytes(base64.b64decode(driver.command('GET', '/screenshot')))
        output.write_text(json.dumps(dict(movie=str(movie), first=first, last=last, passed=True), indent=2) + '\n')
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--base-url', required=True, help='Trusted LAN Jetson origin, e.g. http://192.168.0.30:8080')
    parser.add_argument('--chromedriver', type=Path, required=True)
    parser.add_argument('--chrome-binary', type=Path, required=True)
    parser.add_argument('--kill-camera-child', action='store_true',
                        help='Explicit fault injection: SIGKILL only the Camera module spawned by this script')
    parser.add_argument('--geckodriver', type=Path)
    parser.add_argument('--firefox-openh264-dir', type=Path)
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    binaries = build / 'bin'
    origin = args.base_url.rstrip('/')
    url = urllib.parse.urlparse(origin)
    if url.scheme != 'http' or not url.hostname:
        parser.error('base-url must be a trusted LAN HTTP origin')
    spec = importlib.util.spec_from_file_location('browser', ROOT / 'scripts/tests/webrtc-browser-acceptance.py')
    browser = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(browser)
    config = yaml.safe_load((ROOT / 'configs/development.yaml').read_text())
    config['paths'] = {key: str(out / value) for key, value in
                       [('data_dir', 'data'), ('run_dir', 'run'), ('log_dir', 'logs')]}
    config['services']['camera'].update(capture_pipeline='native_argus', shared_memory_name='/cockpit_recovery_' + str(os.getpid()))
    config['services']['transfer'].update(webrtc_enabled=True, webrtc_address=url.hostname,
                                        webrtc_http_port=url.port or 80, rtsp_enabled=True)
    path = out / 'device.yaml'
    path.write_text(yaml.safe_dump(config, sort_keys=False))
    report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), tests=[],
                  base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  git_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
                  loaded_module=Path('/sys/module/nv_imx219/version').read_text().strip(),
                  standard_vlc='out_of_scope_by_request', firefox='requested' if args.geckodriver else 'not_requested', sigkill='owned_camera_child' if args.kill_camera_child else 'not_part_of_normal_acceptance', passed=False)
    children = []
    observer_stop = threading.Event()
    staging = {}
    driver = None

    def state():
        return subprocess.check_output(['systemctl', 'show', 'nvargus-daemon', '-p', 'MainPID',
                                        '-p', 'NRestarts', '-p', 'InvocationID', '-p', 'ActiveState'], text=True)

    def status():
        with urllib.request.urlopen(origin + '/api/v1/webrtc/status', timeout=3) as response:
            return json.load(response)

    def save(name, data):
        (out / (name + '.json')).write_text(json.dumps(data, indent=2) + '\n')

    def wait(predicate, seconds, name):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.1)
        raise RuntimeError('timeout: ' + name)

    def start(name, argv, env=None):
        log = (out / (name + '.log')).open('w')
        process = subprocess.Popen(list(map(str, argv)), cwd=ROOT, stdout=log,
                                   stderr=subprocess.STDOUT, env=env, start_new_session=True)
        children.append((process, log))
        return process

    def run(name, argv, timeout=60):
        print('START', name, flush=True)
        with (out / (name + '.log')).open('w') as log:
            result = subprocess.run(list(map(str, argv)), cwd=ROOT, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        report['tests'].append(dict(name=name, returncode=result.returncode))
        if result.returncode:
            raise RuntimeError(name + ' failed; inspect log')

    def ctl(*options):
        return [binaries / 'camera-ctl', '--config', path, *options]

    def rec(*options):
        return [binaries / 'recording-ctl', '--config', path, *options]

    def camera_start(name):
        run(name, ctl('--start', '--device', 'nvargus://0', '--width', '1280', '--height', '720',
                      '--fps', '30', '--h264-width', '1280', '--h264-height', '720',
                      '--h264-bitrate', '2000000', '--h264-gop', '30', '--h264-udp-host',
                      '127.0.0.1', '--h264-udp-port', '8556', '--recording-udp-port', '8557'))
        wait(lambda: status().get('video_available'), 20, 'native video ready')

    def rtsp(name, count=None):
        command = ['gst-launch-1.0', '-v', 'rtspsrc', 'location=rtsp://127.0.0.1:8554/camera',
                   'protocols=tcp', 'latency=100', '!', 'rtph264depay', '!', 'h264parse', '!',
                   'identity', 'silent=false', '!', 'fakesink', 'sync=false']
        if count:
            command += ['num-buffers=' + str(count)]
        return start(name, command)

    def count_buffers(name):
        return (out / (name + '.log')).read_text(errors='replace').count('last-message = chain')

    def watch():
        while not observer_stop.wait(.02):
            for entry in (out / 'data').rglob('event_video_*'):
                if entry.is_file():
                    staging.setdefault(str(entry), time.time())

    observer = threading.Thread(target=watch)
    report['daemon_before'] = state()
    try:
        report['binary_sha256'] = {str(p.relative_to(build)): hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in [binaries / 'camera-ctl', binaries / 'cockpit-navigator',
                                            *(build / 'lib/cockpit/modules' / ('lib' + m + '.so')
                                              for m in ['camera_driver', 'recording', 'transfer'])]}
        camera_child = None
        for module in ['transfer', 'camera_driver', 'recording']:
            process = start(module, [binaries / 'cockpit-navigator', '--module-child', '--module', module,
                           '--library', build / 'lib/cockpit/modules' / ('lib' + module + '.so'),
                           '--module-config', path])
            if module == 'camera_driver':
                camera_child = process
        time.sleep(2)
        camera_start('camera-start')
        observer.start()
        run('recording-start', rec('--start', '--trigger', 'correctness-recovery'))
        run('event-1-trigger', rec('--trigger-video', '--event-id', 'event_1', '--event-type', 'manual'))
        slow = rtsp('rtsp-slow')
        wait(lambda: count_buffers('rtsp-slow') >= 15, 20, 'slow RTSP client initial frames')
        os.killpg(slow.pid, signal.SIGSTOP)  # Owned RTSP client only: actual stopped socket reader.
        report['slow_client'] = dict(pid=slow.pid, observed_before_stop=count_buffers('rtsp-slow'),
                                     fault='SIGSTOP owned RTSP TCP receiver; no Camera/Argus signal')
        fast = rtsp('rtsp-fast', 1200)
        env = os.environ.copy()
        env.update(DISPLAY=':0', XAUTHORITY=str(Path.home() / '.Xauthority'))
        start('chromedriver', [args.chromedriver.resolve(), '--port=9515'], env)
        if args.geckodriver:
            start('geckodriver', [args.geckodriver.absolute(), '--host', '127.0.0.1', '--port', '4444'])
        time.sleep(2)
        common = ['python3', ROOT / 'scripts/tests/webrtc-browser-acceptance.py', '--base-url', origin,
                  '--width', '1280', '--height', '720', '--seconds', '10', '--timeout', '20']
        run('chrome', common + ['--browser', 'chrome', '--browser-binary', args.chrome_binary.resolve(),
                               '--output', out / 'chrome.json'], 150)
        if fast.wait(timeout=30) != 0:
            raise RuntimeError('healthy RTSP failed while other TCP client was stopped')
        report['tests'].append(dict(name='rtsp-slow-client-isolation', passed=True,
                                   healthy_buffers=count_buffers('rtsp-fast')))
        os.killpg(slow.pid, signal.SIGCONT)
        slow.terminate()
        slow.wait(timeout=15)
        run('event-2-trigger', rec('--trigger-video', '--event-id', 'event_2', '--event-type', 'manual'))
        if args.geckodriver and args.firefox_openh264_dir:
            run('firefox', common + ['--browser', 'firefox', '--webdriver-url', 'http://127.0.0.1:4444',
                                    '--headless', '--firefox-openh264-dir', args.firefox_openh264_dir.resolve(),
                                    '--output', out / 'firefox.json'], 150)
        wait(lambda: len(list((out / 'data').rglob('*.mp4'))) >= 2, 30, 'two completed events')
        # Keep one browser and an RTSP connection alive across Camera Session stop/start.
        driver = browser.Driver('http://127.0.0.1:9515')
        driver.start('chrome', False, browser_binary=str(args.chrome_binary.resolve()))
        driver.command('POST', '/url', {'url': origin + '/'})
        playback_args = types.SimpleNamespace(width=1280, height=720, seconds=5, timeout=20,
                                               headless=False, output=out / 'restart-browser.json')
        recovery = {'checks': []}
        old_id = browser.playback(driver, playback_args, 'before-stop', recovery)
        before = status()
        persistent = rtsp('rtsp-persistent')
        wait(lambda: count_buffers('rtsp-persistent') >= 30, 20, 'persistent RTSP initial frames')
        if args.kill_camera_child:
            if camera_child.poll() is not None:
                raise RuntimeError('Camera child exited before fault injection')
            os.killpg(camera_child.pid, signal.SIGKILL)
            if camera_child.wait(timeout=5) != -signal.SIGKILL:
                raise RuntimeError('Camera SIGKILL was not confirmed')
            report['tests'].append(dict(name='owned-camera-child-sigkill', passed=True,
                                        pid=camera_child.pid, returncode=-signal.SIGKILL))
        else:
            run('camera-stop', ctl('--stop'))
        wait(lambda: not status()['video_available'] and not status()['sessions'], 15,
             'stalled input and old Peer retirement')
        save('stopped-status', status())
        before_restart_buffers = count_buffers('rtsp-persistent')
        if args.kill_camera_child:
            camera_child = start('camera_driver-restarted', [binaries / 'cockpit-navigator',
                '--module-child', '--module', 'camera_driver', '--library',
                build / 'lib/cockpit/modules/libcamera_driver.so', '--module-config', path])
            time.sleep(2)
        camera_start('camera-restart')
        after = status()
        if after['generation'] <= before['generation']:
            raise RuntimeError('Camera restart reused old generation')
        new_id = browser.playback(driver, playback_args, 'after-restart', recovery)
        if new_id == old_id:
            raise RuntimeError('Camera restart reused old Peer')
        wait(lambda: count_buffers('rtsp-persistent') > before_restart_buffers + 30, 20,
             'existing RTSP transport resumes after Camera restart')
        recovery.update(old_id=old_id, new_id=new_id, generation_before=before['generation'],
                        generation_after=after['generation'], rtsp_buffers_before_restart=before_restart_buffers,
                        rtsp_buffers_after=count_buffers('rtsp-persistent'))
        save('restart-browser', recovery)
        persistent.terminate()
        persistent.wait(timeout=15)
        run('event-3-after-restart', rec('--trigger-video', '--event-id', 'after_restart', '--event-type', 'manual'))
        time.sleep(3)
        run('recording-stop', rec('--stop'))
        files = sorted((out / 'data').rglob('*.mp4'))
        if len(files) != 3 or list((out / 'data').rglob('*.incomplete')):
            raise RuntimeError('wrong artifact count or leftover incomplete file')
        for i, movie in enumerate(files):
            # Session directory gets renamed on Stop: compare artifact basenames.
            seen = [name for name in staging if Path(name).name == movie.name + '.incomplete']
            if not seen:
                raise RuntimeError('staging file was not observed: ' + movie.name)
            run('mp4-discover-' + str(i), ['gst-discoverer-1.0', movie], 30)
            run('mp4-decode-' + str(i), ['gst-launch-1.0', '-q', 'filesrc', 'location=' + str(movie), '!',
                                       'qtdemux', '!', 'h264parse', '!', 'nvv4l2decoder', '!',
                                       'fakesink', 'sync=false'], 40)
        verify_movie(files[-1], driver, out / 'mp4-player.json')
        run('recording-verify', rec('--verify-all', '--output', 'json'))
        run('camera-status', ctl('--status', '--output', 'json'))
        report.update(passed=True, mp4_files=list(map(str, files)), staging_first_seen=staging)
    except Exception as error:
        report['error'] = str(error)
        print('ERROR', error, flush=True)
    finally:
        observer_stop.set()
        if observer.is_alive():
            observer.join(timeout=5)
        if driver:
            try:
                driver.close()
            except Exception as error:
                report['browser_cleanup_error'] = str(error)
        for name, command in [('cleanup-recording', rec('--stop')), ('cleanup-camera', ctl('--stop'))]:
            try:
                run(name, command, 30)
            except Exception as error:
                report[name] = str(error)
                report['passed'] = False
        for process, log in reversed(children):
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGCONT)
                process.terminate()
        for process, log in reversed(children):
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                report.setdefault('forced_cleanup', []).append(process.args)
                report['passed'] = False
            log.close()
        report['daemon_after'] = state()
        report['daemon_stable'] = report['daemon_before'] == report['daemon_after']
        report['passed'] = report['passed'] and report['daemon_stable']
        report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        since = '@' + str(int(datetime.datetime.fromisoformat(report['started_utc']).timestamp()))
        for name, selectors in [('kernel', ['-k']), ('argus', ['-u', 'nvargus-daemon'])]:
            journal = subprocess.run(['journalctl', '--since', since, '--no-pager', *selectors],
                                     capture_output=True, text=True, timeout=15)
            (out / (name + '.journal.txt')).write_text(journal.stdout + journal.stderr)
        save('acceptance', report)
    print('PASSED', report['passed'], 'daemon_stable', report['daemon_stable'], flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
