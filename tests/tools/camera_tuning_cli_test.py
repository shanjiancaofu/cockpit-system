import json
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
):
    result = subprocess.run([binary, *arguments], capture_output=True,
                            text=True, timeout=10)
    assert result.returncode == 2, (arguments, result.stdout, result.stderr)
    assert json.loads(result.stderr)['error']
    # Argument rejection must precede SDK access.
    assert 'NvArgus' not in result.stderr
