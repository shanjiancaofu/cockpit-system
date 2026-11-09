#!/usr/bin/env python3
"""Unit tests for evidence validation only; never browser acceptance results."""
import importlib.util
from pathlib import Path
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('browser_acceptance', ROOT / 'scripts/tests/webrtc-browser-acceptance.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def sample(frames=10, timestamp=1):
    return {'connection': 'connected', 'paused': False, 'videoError': None,
            'width': 1280, 'height': 720, 'currentTime': timestamp,
            'totalVideoFrames': frames, 'id': 'test-session',
            'inbound': [{'framesDecoded': frames, 'bytesReceived': frames * 100}]}


class EvidenceTest(unittest.TestCase):
    def test_real_frame_progress_required(self):
        MODULE.validate_progress(sample(), sample(20, 2), 1280, 720)
        with self.assertRaisesRegex(RuntimeError, 'did not advance'):
            MODULE.validate_progress(sample(), sample(), 1280, 720)

    def test_connected_black_video_and_wrong_resolution_fail(self):
        for change in ({'width': 0, 'height': 0}, {'width': 1920, 'height': 1080},
                       {'paused': True}, {'videoError': 'decode failed'},
                       {'connection': 'disconnected'}, {'id': 'different'},
                       {'inbound': [{'bytesReceived': 20000, 'framesDecoded': 10}]}):
            last = sample(20, 2)
            last.update(change)
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                MODULE.validate_progress(sample(), last, 1280, 720)

    def test_headless_requires_real_decode_but_not_rendered_frame_counter(self):
        first, last = sample(), sample(20, 2)
        first['totalVideoFrames'] = last['totalVideoFrames'] = 0
        MODULE.validate_progress(first, last, 1280, 720, require_rendered_frames=False)
        with self.assertRaisesRegex(RuntimeError, 'displayed video'):
            MODULE.validate_progress(first, last, 1280, 720)
        last['inbound'][0]['framesDecoded'] = 10
        with self.assertRaisesRegex(RuntimeError, 'framesDecoded'):
            MODULE.validate_progress(first, last, 1280, 720, require_rendered_frames=False)

    def test_driver_does_not_accept_wrong_browser(self):
        driver = MODULE.Driver('http://unused')
        driver.request = mock.Mock(return_value={'sessionId': 'abc', 'capabilities': {'browserName': 'firefox'}})
        with self.assertRaisesRegex(RuntimeError, 'actually returned'):
            driver.start('chrome', False)
        self.assertEqual(driver.session, 'abc')  # still available for finally cleanup

    def test_firefox_capabilities(self):
        driver = MODULE.Driver('http://unused')
        driver.request = mock.Mock(return_value={'sessionId': 'ff', 'capabilities': {'browserName': 'firefox'}})
        driver.start('firefox', True)
        caps = driver.request.call_args_list[0].args[2]['capabilities']['alwaysMatch']
        self.assertEqual(caps['browserName'], 'firefox')
        self.assertEqual(caps['moz:firefoxOptions']['args'], ['-headless'])
        self.assertNotIn('goog:chromeOptions', caps)

    def test_firefox_codec_seed_contains_no_profile_data(self):
        import base64
        import io
        import zipfile
        with tempfile.TemporaryDirectory() as directory:
            codec = Path(directory) / '2.6.0'
            codec.mkdir()
            (codec / 'libgmpopenh264.so').write_bytes(b'test codec')
            (codec / 'gmpopenh264.info').write_text('Name: test')
            (codec / 'cookies.sqlite').write_bytes(b'must not be copied')
            driver = MODULE.Driver('http://unused')
            driver.request = mock.Mock(return_value={'sessionId': 'ff', 'capabilities': {'browserName': 'firefox'}})
            driver.start('firefox', True, codec)
            options = driver.request.call_args_list[0].args[2]['capabilities']['alwaysMatch']['moz:firefoxOptions']
            with zipfile.ZipFile(io.BytesIO(base64.b64decode(options['profile']))) as archive:
                self.assertEqual(set(archive.namelist()), {
                    'gmp-gmpopenh264/2.6.0/libgmpopenh264.so',
                    'gmp-gmpopenh264/2.6.0/gmpopenh264.info'})
            self.assertEqual(options['prefs']['media.gmp-gmpopenh264.version'], '2.6.0')

    def test_explicit_chrome_binary_does_not_disable_sandbox(self):
        driver = MODULE.Driver('http://unused')
        driver.request = mock.Mock(return_value={'sessionId': 'chrome', 'capabilities': {'browserName': 'chrome'}})
        driver.start('chrome', False, browser_binary='/opt/test-chrome/chrome')
        caps = driver.request.call_args_list[0].args[2]['capabilities']['alwaysMatch']
        self.assertEqual(caps['goog:chromeOptions']['binary'], '/opt/test-chrome/chrome')
        self.assertNotIn('--no-sandbox', caps['goog:chromeOptions']['args'])

    def test_failed_start_saves_failed_not_passed_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            args = types.SimpleNamespace(base_url='http://jetson:8080', browser='edge', width=1280,
                                         height=720, webdriver_url='http://unused', headless=False,
                                         output=Path(directory) / 'result.json')
            with mock.patch.object(MODULE.Driver, 'start', side_effect=RuntimeError('no Edge driver')):
                self.assertEqual(MODULE.run(args), 1)
            import json
            report = json.loads(args.output.read_text())
            self.assertEqual(report['result'], 'failed')
            self.assertEqual(report['checks'], [])
            self.assertIn('no Edge driver', report['error'])
            self.assertIn('MP4 playback', report['not_covered'])

    def test_every_phase_and_cleanup_required(self):
        with tempfile.TemporaryDirectory() as directory:
            args = types.SimpleNamespace(base_url='http://jetson:8080', browser='chrome', width=1280,
                                         height=720, webdriver_url='http://unused', headless=False,
                                         timeout=1, output=Path(directory) / 'result.json')
            driver = mock.Mock(session='test-driver')
            driver.start.return_value = {'browserName': 'chrome'}
            driver.script.side_effect = lambda script, *a: ([{'mimeType': 'video/H264'}] if 'getCapabilities' in script else {'connection': None, 'sessions': []})
            with mock.patch.object(MODULE, 'Driver', return_value=driver), \
                 mock.patch.object(MODULE, 'playback', side_effect=['first', 'second', 'third']) as playback, \
                 mock.patch.object(MODULE.time, 'sleep'):
                self.assertEqual(MODULE.run(args), 0)
                self.assertEqual(playback.call_count, 3)
                driver.command.assert_any_call('POST', '/refresh', {})
                driver.close.assert_called_once()
            with mock.patch.object(MODULE, 'Driver', return_value=driver), \
                 mock.patch.object(MODULE, 'playback', side_effect=['first', RuntimeError('reconnect failed')]):
                self.assertEqual(MODULE.run(args), 1)


if __name__ == '__main__':
    unittest.main()
