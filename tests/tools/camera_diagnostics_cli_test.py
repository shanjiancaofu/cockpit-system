import json
from pathlib import Path
import subprocess
import sys
import tempfile


def run(binary, *args):
    return subprocess.run([binary, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, check=False, timeout=30)


for binary in sys.argv[1:]:
    assert run(binary, '--help').returncode == 0
    for value in ('0', '-1', '30junk', '99999999999999999'):
        assert run(binary, '--frames', value).returncode == 2
    assert run(binary, '--frames').returncode == 2
    assert run(binary, '--unknown', '1').returncode == 2
    with tempfile.TemporaryDirectory() as temporary:
        report = str(Path(temporary) / 'failure.json')
        result = run(binary, '--device', '/dev/cockpit-no-such-camera', '--cycles', '1',
                     '--frames', '2', '--output', report)
        assert result.returncode == 1, result.stderr
        data = json.loads(Path(report).read_text())
        assert data['healthy'] is False and data['status'] == 'failed'
        assert data['errors']
        assert data['capture']['cycles'][0]['frames'] == 0
        assert data['capture']['cycles'][0]['started'] is False
        assert Path(report + '.errors.log').read_text()
        assert run(binary, '--device', '/dev/cockpit-no-such-camera',
                   '--output', str(Path(temporary) / 'missing/report.json')).returncode == 2
