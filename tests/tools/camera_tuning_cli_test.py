import json
from pathlib import Path
import tempfile
import subprocess
import sys

binary = sys.argv[1]
help_result = subprocess.run([binary, 'tuning', '--help'], capture_output=True,
                             text=True, timeout=10)
assert help_result.returncode == 0
assert 'ISOLATED' in help_result.stdout
for arguments in (
        ['tuning'],
        ['tuning', 'get'],
        ['tuning', 'set', '--native'],
        ['tuning', 'get', '--native', '--frames', 'bad'],
        ['tuning', 'get', '--native', '--frames', '10suffix'],
        ['tuning', 'get', '--native', '--width', '-1'],
        ['tuning', 'get', '--native', '--unexpected', '1'],
        ['tuning', 'get', '--native', '--gain', '2'],
        ['tuning', 'set', '--native', '--exposure-us', '8000'],
        ['tuning', 'set', '--native', '--exposure-us', 'nan', '--gain', '2'],
        ['tuning', 'set', '--native', '--wb-gains', '1,1,1'],
        ['tuning', 'set', '--native', '--wb-gains', '1,1,1,1,'],
        ['tuning', 'set', '--native', '--gain-range', '1,2',
         '--gain', '2'],
        ['tuning', 'set', '--native', '--ae-lock', 'invalid'],
        ['tuning', 'set', '--native', '--awb', 'invalid'],
        ['tuning', 'set', '--native', '--ev', '1e100'],
        ['tuning', 'save', '--native'],
        ['tuning', 'load', '--native'],
        ['tuning', 'get', '--native', '--profile', 'unused.yaml'],
        ['tuning', 'load', '--native', '--profile', '/no/such/profile.yaml'],
        ['tuning', 'save', '--native', '--profile', 'unused.yaml',
         '--profile-name', ''],
):
    result = subprocess.run([binary, *arguments], capture_output=True,
                            text=True, timeout=10)
    assert result.returncode == 2, (arguments, result.stdout, result.stderr)
    assert json.loads(result.stderr)['error']
    # Argument rejection must precede SDK access.
    assert 'NvArgus' not in result.stderr

example = (Path(__file__).resolve().parents[2] /
           'configs/camera/imx219_tuning.yaml').read_text()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'profile.yaml'
    for text in (
            '',
            example + '\nschema_version: 1\n',
            example.replace('schema_version: 1', 'schema_version: 2'),
            example.replace('width: 1920', 'width: 1280'),
            example.replace('antibanding: auto', 'denoise: 0.5'),
            example + '\n---\nname: extra\n',
            'x' * 65537,
    ):
        path.write_text(text)
        result = subprocess.run(
            [binary, 'tuning', 'load', '--native', '--profile', str(path)],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 2, (result.stdout, result.stderr)
        assert json.loads(result.stderr)['error']
        assert 'NvArgus' not in result.stderr
        assert path.read_text() == text

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'valid.yaml'
    path.write_text(example)
    link = Path(directory) / 'report-link.json'
    link.symlink_to(path)
    hardlink = Path(directory) / 'report-hardlink.json'
    hardlink.hardlink_to(path)
    for action in ('save', 'load'):
        for report in (path, link, hardlink):
            result = subprocess.run(
                [binary, 'tuning', action, '--native', '--profile', str(path),
                 '--report', str(report)], capture_output=True, text=True,
                timeout=10)
            assert result.returncode == 2, (result.stdout, result.stderr)
            error = json.loads(result.stderr)['error']
            assert 'overwrite' in error
            assert path.read_text() == example
    fresh = Path(directory) / 'fresh.yaml'
    dangling = Path(directory) / 'dangling.json'
    dangling.symlink_to(fresh)
    result = subprocess.run(
        [binary, 'tuning', 'save', '--native', '--profile', str(fresh),
         '--report', str(dangling)], capture_output=True, text=True,
        timeout=10)
    assert result.returncode == 2
    assert not fresh.exists()
