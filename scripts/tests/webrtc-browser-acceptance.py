#!/usr/bin/env python3
"""Small W3C WebDriver acceptance runner. No Selenium/pip/browser downloads.

Run on the Chrome/Edge workstation; point --base-url at the Jetson LAN address.
This is opt-in hardware acceptance, never an automatically passing CTest.
"""
import argparse
import base64
import datetime
import json
import io
import zipfile
from pathlib import Path
import time
import urllib.error
import urllib.parse
import urllib.request


class Driver:
    def __init__(self, url):
        self.url = url.rstrip('/')
        self.session = None

    def request(self, method, path, data=None):
        payload = None if data is None else json.dumps(data).encode()
        request = urllib.request.Request(self.url + path, data=payload, method=method,
                                         headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request, timeout=35) as response:
                result = json.load(response).get('value')
        except urllib.error.HTTPError as error:
            raise RuntimeError(f'WebDriver HTTP {error.code}: {error.read().decode()}') from error
        # A successful execute-script may legitimately return {error: ...}
        # from the page. W3C protocol errors use non-2xx HTTP responses above.
        return result

    def start(self, browser, headless, openh264_dir=None, browser_binary=None):
        if browser == 'firefox':
            caps = {'browserName': 'firefox', 'moz:firefoxOptions': {
                'args': ['-headless'] if headless else [],
                'prefs': {'media.autoplay.default': 0}}}
            if openh264_dir:
                # Seed ONLY already-installed codec assets, never a user's profile,
                # cookies or credentials. GeckoDriver extracts this into its fresh profile.
                source = Path(openh264_dir)
                names = ['libgmpopenh264.so', 'gmpopenh264.dll', 'libgmpopenh264.dylib', 'gmpopenh264.info']
                assets = [source / name for name in names if (source / name).is_file()]
                if not (source / 'gmpopenh264.info').is_file() or len(assets) < 2:
                    raise RuntimeError('--firefox-openh264-dir needs codec library and gmpopenh264.info')
                archive = io.BytesIO()
                with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zipped:
                    for asset in assets:
                        zipped.write(asset, f'gmp-gmpopenh264/{source.name}/{asset.name}')
                caps['moz:firefoxOptions']['profile'] = base64.b64encode(archive.getvalue()).decode('ascii')
                caps['moz:firefoxOptions']['prefs'].update({
                    'media.gmp-gmpopenh264.version': source.name,
                    'media.gmp-gmpopenh264.enabled': True})

        else:
            options = {'args': ['--autoplay-policy=no-user-gesture-required']}
            if headless:
                options['args'].append('--headless=new')
            caps = {'browserName': 'chrome' if browser == 'chrome' else 'MicrosoftEdge',
                    'goog:chromeOptions' if browser == 'chrome' else 'ms:edgeOptions': options}
        if browser_binary:
            option_key = {'chrome': 'goog:chromeOptions', 'edge': 'ms:edgeOptions', 'firefox': 'moz:firefoxOptions'}[browser]
            caps[option_key]['binary'] = str(browser_binary)
        result = self.request('POST', '/session', {'capabilities': {'alwaysMatch': caps}})
        self.session = result['sessionId']
        actual = result['capabilities'].get('browserName', '').lower()
        expected = {'chrome': {'chrome'}, 'edge': {'msedge', 'microsoftedge'}, 'firefox': {'firefox'}}
        if actual not in expected[browser]:
            raise RuntimeError(f'requested {browser}, driver actually returned {actual!r}')
        self.command('POST', '/timeouts', {'script': 10000, 'pageLoad': 30000, 'implicit': 0})
        return result['capabilities']

    def command(self, method, path, data=None):
        return self.request(method, '/session/' + self.session + path, data)

    def script(self, script, asynchronous=False):
        return self.command('POST', '/execute/async' if asynchronous else '/execute/sync',
                            {'script': script, 'args': []})

    def click(self, element_id):
        element = self.command('POST', '/element', {'using': 'css selector', 'value': '#' + element_id})
        key = 'element-6066-11e4-a52e-4f735466cecf'
        self.command('POST', '/element/' + element[key] + '/click', {})

    def close(self):
        if self.session:
            try:
                self.command('DELETE', '')
            finally:
                self.session = None


SNAPSHOT = r"""
const done = arguments[arguments.length - 1];
(async () => {
  const v = document.getElementById('video');
  // Gecko WebDriver uses a sandbox: resolve the page's lexical binding in its
  // own realm, not the driver's sandbox. No product-side test API is needed.
  const session = window.eval('typeof current === \"undefined\" ? null : current');
  const pc = session && session.pc;
  const quality = v && v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : null;
  const statusText = document.getElementById('status')?.textContent || '';
  const inbound = [];
  if (pc) for (const r of (await pc.getStats()).values()) {
    if (r.type === 'inbound-rtp' && (r.kind === 'video' || r.mediaType === 'video')) {
      inbound.push({ bytesReceived: r.bytesReceived, framesDecoded: r.framesDecoded,
                     frameWidth: r.frameWidth, frameHeight: r.frameHeight,
                     packetsLost: r.packetsLost, framesDropped: r.framesDropped });
    }
  }
  return { width: v ? v.videoWidth : 0, height: v ? v.videoHeight : 0,
           currentTime: v ? v.currentTime : 0, paused: v ? v.paused : true,
           videoError: v && v.error ? v.error.message : null,
           totalVideoFrames: quality ? quality.totalVideoFrames : 0,
           connection: pc ? pc.connectionState : null,
           ice: pc ? pc.iceConnectionState : null, id: session ? session.id : null,
           status: statusText, error: statusText.startsWith('连接失败：') ? statusText : null, inbound };
})().then(done, e => done({error: String(e)}));
"""


NEGOTIATION = r"""
return window.eval(`(() => {
  const s = typeof current === 'undefined' ? null : current;
  const last = typeof negotiation === 'undefined' ? null : negotiation;
  return { offer: (s && s.pc.localDescription && s.pc.localDescription.sdp) || (last && last.offer),
           answer: (s && s.pc.remoteDescription && s.pc.remoteDescription.sdp) || (last && last.answer) };
})()`);
"""


def save_negotiation(driver, args, phase):
    values = driver.script(NEGOTIATION)
    paths = {}
    for kind in ('offer', 'answer'):
        raw = values.get(kind) if isinstance(values, dict) else None
        if raw:
            destination = args.output.parent / f'{args.output.stem}-{phase}-{kind}.sdp'
            with destination.open('w', encoding='utf-8', newline='') as output:
                output.write(raw)
            paths[kind] = str(destination)
    return paths


def wait_for(fetch, predicate, seconds, description):
    deadline = time.monotonic() + seconds
    last = None
    while time.monotonic() < deadline:
        last = fetch()
        if isinstance(last, dict) and last.get('error'):
            raise RuntimeError(str(last['error']))
        if predicate(last):
            return last
        time.sleep(0.5)
    raise RuntimeError(f'{description} timed out; last observation: {json.dumps(last)}')


def validate_progress(first, last, width, height, require_rendered_frames=True):
    for value in (first, last):
        if value.get('connection') != 'connected' or value.get('paused') or value.get('videoError'):
            raise RuntimeError('peer is not actively playing: ' + json.dumps(value))
        if (value.get('width'), value.get('height')) != (width, height):
            raise RuntimeError(f'expected {width}x{height}, got {value.get("width")}x{value.get("height")}')
    if last['currentTime'] <= first['currentTime']:
        raise RuntimeError('ICE is connected but video time did not advance')
    if require_rendered_frames and last['totalVideoFrames'] <= first['totalVideoFrames']:
        raise RuntimeError('displayed video frames did not advance')
    before = sum(item.get('framesDecoded', 0) or 0 for item in first['inbound'])
    after = sum(item.get('framesDecoded', 0) or 0 for item in last['inbound'])
    if after <= before:
        raise RuntimeError('WebRTC inbound framesDecoded did not advance')
    if first.get('id') != last.get('id'):
        raise RuntimeError('session changed during the continuous-playback observation')


def wait_retired(driver, session_id, timeout):
    if not session_id:
        raise RuntimeError('connected browser did not expose its server session id')
    script = """
      const done = arguments[arguments.length - 1];
      fetch('/api/v1/webrtc/status', {cache: 'no-store'})
        .then(r => { if (!r.ok) throw new Error('status HTTP ' + r.status); return r.json(); })
        .then(done, e => done({error: String(e)}));
    """
    wait_for(lambda: driver.script(script, True),
             lambda status: isinstance(status.get('sessions'), list) and
             all(peer.get('id') != session_id for peer in status['sessions']),
             timeout, 'server-side peer retirement')


def playback(driver, args, phase, report):
    driver.click('connect')
    first = wait_for(lambda: driver.script(SNAPSHOT, True),
                     lambda s: s.get('connection') == 'connected' and s.get('width', 0) > 0 and
                     sum(item.get('framesDecoded', 0) or 0 for item in s.get('inbound', [])) > 0,
                     args.timeout, phase + ' first decoded frame')
    entry = {'phase': phase, 'result': 'running', 'first': first, 'samples': []}
    report['checks'].append(entry)
    entry['raw_sdp'] = save_negotiation(driver, args, phase)
    if 'offer' not in entry['raw_sdp'] or 'answer' not in entry['raw_sdp']:
        raise RuntimeError('raw browser offer/answer evidence missing')
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        time.sleep(min(1, max(0, deadline - time.monotonic())))
        value = driver.script(SNAPSHOT, True)
        validate_progress(first, value, args.width, args.height, not args.headless)
        # A persistent freeze must not hide behind progress made earlier in the phase.
        entry['samples'].append(value)
        if len(entry['samples']) >= 4:
            validate_progress(entry['samples'][-4], value, args.width, args.height, not args.headless)
    entry['last'] = entry['samples'][-1]
    image = driver.command('GET', '/screenshot')
    screenshot = args.output.parent / (args.output.stem + '-' + phase + '.png')
    screenshot.write_bytes(base64.b64decode(image, validate=True))
    entry['screenshot'] = str(screenshot)
    entry['result'] = 'passed'
    print(f"PASS {phase}: {args.width}x{args.height}; decoded video continuously advances", flush=True)
    return first['id']


def run(args):
    report = {'timestamp': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'base_url': args.base_url, 'requested_browser': args.browser,
              'expected_size': [args.width, args.height], 'result': 'failed', 'checks': [],
              'not_covered': ['Native Camera authenticity', 'Camera stop/restart',
                              'VLC viewing', 'MP4 playback']}
    report['rendering_check'] = 'headless_decode_only' if args.headless else 'visible_browser_frame_counter'
    if args.headless:
        report['not_covered'].append('visible browser rendering / screenshot content')
    driver = Driver(args.webdriver_url)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        report['capabilities'] = driver.start(args.browser, args.headless, getattr(args, 'firefox_openh264_dir', None), getattr(args, 'browser_binary', None))
        if getattr(args, 'window_width', None) and getattr(args, 'window_height', None):
            report['window'] = driver.command('POST', '/window/rect', {
                'width': args.window_width, 'height': args.window_height})
        driver.command('POST', '/url', {'url': args.base_url.rstrip('/') + '/'})
        report['source_status'] = driver.script(
            "const done=arguments[arguments.length-1]; fetch('/api/v1/webrtc/status', {cache:'no-store'})"
            ".then(r=>r.json()).then(done,e=>done({error:String(e)}));", True)
        report['video_capabilities'] = driver.script(
            "return RTCRtpReceiver.getCapabilities('video')?.codecs || [];")
        if not any(codec.get('mimeType', '').lower() == 'video/h264' for codec in report['video_capabilities']):
            raise RuntimeError('browser profile does not advertise WebRTC H264; check OpenH264/codec availability before signaling')

        first_id = playback(driver, args, 'initial', report)
        driver.click('disconnect')
        wait_for(lambda: driver.script(SNAPSHOT, True), lambda s: s.get('connection') is None,
                 args.timeout, 'disconnect')
        wait_retired(driver, first_id, args.timeout)
        second_id = playback(driver, args, 'reconnect', report)
        if not first_id or not second_id or first_id == second_id:
            raise RuntimeError('reconnect did not create a fresh session')
        # Refresh a *connected* page to exercise pagehide, not a page already disconnected.
        driver.command('POST', '/refresh', {})
        wait_retired(driver, second_id, args.timeout)
        third_id = playback(driver, args, 'refresh', report)
        if not third_id or third_id in (first_id, second_id):
            raise RuntimeError('refresh did not create a fresh session')
        report['console_ui'] = driver.script(
            "return {viewportWidth:innerWidth, documentWidth:document.documentElement.scrollWidth,"
            "metrics:Object.fromEntries(['metric-resolution','metric-fps','metric-bitrate','metric-loss',"
            "'metric-dropped','metric-generation'].map(id=>[id,document.getElementById(id)?.textContent || null]))};")
        if report['console_ui'].get('documentWidth', 0) > report['console_ui'].get('viewportWidth', 0) + 1:
            raise RuntimeError('console has horizontal overflow')
        driver.click('disconnect')
        wait_retired(driver, third_id, args.timeout)
        report['result'] = 'passed'
    except Exception as error:
        report['error'] = str(error)
        print('FAIL: ' + str(error), flush=True)
        if driver.session:
            try:
                report['failure_snapshot'] = driver.script(SNAPSHOT, True)
                report['failure_raw_sdp'] = save_negotiation(driver, args, 'failure')
            except Exception as diagnostic_error:
                report['snapshot_error'] = str(diagnostic_error)
    finally:
        if driver.session:
            try:
                driver.click('disconnect')
            except Exception as error:
                report['disconnect_warning'] = str(error)
            try:
                driver.close()
            except Exception as error:
                report['result'] = 'failed'
                report['cleanup_error'] = str(error)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print('Evidence: ' + str(args.output), flush=True)
    return 0 if report['result'] == 'passed' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-url', required=True, help='Jetson HTTP origin, e.g. http://10.0.0.2:8080')
    parser.add_argument('--webdriver-url', default='http://127.0.0.1:9515')
    parser.add_argument('--browser', choices=['chrome', 'edge', 'firefox'], required=True)
    parser.add_argument('--width', type=int, required=True)
    parser.add_argument('--height', type=int, required=True)
    parser.add_argument('--seconds', type=float, default=10, help='continuous playback seconds per phase')
    parser.add_argument('--timeout', type=float, default=20)
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--window-width', type=int, help='optional WebDriver window width (actual viewport recorded)')
    parser.add_argument('--window-height', type=int, help='optional WebDriver window height')
    parser.add_argument('--browser-binary', help='optional browser executable path as seen by the WebDriver host')
    parser.add_argument('--firefox-openh264-dir', type=Path, help='optional installed codec-version directory; seeds only codec assets into a clean profile')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if bool(args.window_width) != bool(args.window_height) or (args.window_width is not None and min(args.window_width, args.window_height) < 320):
        parser.error('--window-width and --window-height must both be supplied, at least 320')
    if args.firefox_openh264_dir and args.browser != 'firefox':
        parser.error('--firefox-openh264-dir only applies to Firefox')
    origin = urllib.parse.urlsplit(args.base_url)
    if origin.scheme not in ('http', 'https') or not origin.hostname or origin.path not in ('', '/') or origin.query or origin.fragment or origin.username:
        parser.error('--base-url must be an HTTP(S) origin, not a path or credentialed URL')
    if min(args.width, args.height) <= 0 or args.seconds < 3 or args.timeout <= 0:
        parser.error('dimensions/timeout must be positive, --seconds must be at least 3')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
