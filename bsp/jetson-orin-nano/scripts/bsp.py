#!/usr/bin/env python3
"""Pinned native Jetson camera BSP build/inspection utilities. No auto-download."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

BSP = Path(__file__).resolve().parents[1]
ROOT = BSP.parents[1]
LOCK = json.loads((BSP / 'baseline.lock.json').read_text())
DEFAULT_OUT = ROOT / '_output/bsp/r36.5.2/bsp01'


def run(args, *, cwd=None, check=True):
    proc = subprocess.run([str(a) for a in args], cwd=cwd, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and proc.returncode:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(map(str, args))}\n{proc.stdout}\n{proc.stderr}")
    return proc


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as file:
        for block in iter(lambda: file.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def header_digest(directory):
    directory = Path(directory)
    digest = hashlib.sha256()
    for header in sorted((directory / 'include').rglob('*.h')):
        digest.update(str(header.relative_to(directory)).encode() + b'\0')
        digest.update(bytes.fromhex(sha(header)))
    return digest.hexdigest()


def strings(path):
    try:
        return Path(path).read_bytes().rstrip(b'\0').decode().split('\0')
    except (OSError, UnicodeError):
        return []


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, 'w') as output:
            output.write(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        directory_fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        temporary.unlink(missing_ok=True)


def checked_sources(path, hashes, commit):
    path = Path(path).resolve(strict=True)
    actual = run(['git', '-C', path, 'rev-parse', 'HEAD']).stdout.strip()
    if actual != commit:
        raise RuntimeError(f'wrong source commit: {path}: {actual}, expected {commit}')
    if run(['git', '-C', path, 'diff', '--quiet', 'HEAD', '--'], check=False).returncode:
        raise RuntimeError('pinned source worktree has tracked edits: ' + str(path))
    for name, expected in hashes.items():
        if sha(path / name) != expected:
            raise RuntimeError(f'pinned source has changed: {path / name}')
    return path


def require_equal(expected, actual, description):
    if Path(expected).read_bytes() != Path(actual).read_bytes():
        raise RuntimeError(description + ': complete source and vendor+patch disagree; update the patch explicitly')


def crc_table(*files, symbols=None):
    table = {}
    for path in files:
        for line in Path(path).read_text().splitlines():
            fields = line.split()
            if len(fields) >= 2:
                crc, symbol = fields[:2]
                if symbols is not None and symbol not in symbols:
                    continue
                if symbol in table and table[symbol] != crc.lower():
                    raise RuntimeError('conflicting symbol CRC: ' + symbol)
                table[symbol] = crc.lower()
    return table


def module_versions(module):
    result = {}
    for line in run(['modprobe', '--show-modversions', module]).stdout.splitlines():
        fields = line.split()
        if len(fields) == 2:
            result[fields[1]] = fields[0].lower()
    if not result or 'module_layout' not in result:
        raise RuntimeError('module lacks expected CONFIG_MODVERSIONS imports')
    return result


def check_crcs(module, kernel, oot):
    imports = module_versions(module)
    expected = crc_table(kernel / 'Module.symvers', oot / 'Module.symvers', symbols=set(imports))
    mismatches = {name: {'module': crc, 'target': expected.get(name)}
                  for name, crc in imports.items() if expected.get(name) != crc}
    if mismatches:
        raise RuntimeError('module symbol CRC mismatch: ' + json.dumps(mismatches))
    return imports


def modinfo(module, field):
    return run(['modinfo', '-F', field, module]).stdout.strip()


def target_check():
    if platform.machine() != 'aarch64' or platform.release() != LOCK['kernel_release']:
        raise RuntimeError('this native build/deploy profile requires ' + LOCK['kernel_release'] + ' / aarch64')
    release = Path('/etc/nv_tegra_release').read_text()
    if not re.search(r'R36\s+\(release\).*REVISION:\s*5\.2(?:,|\s)', release):
        raise RuntimeError('target Jetson Linux release is not R36.5.2')
    if LOCK['board_compatible'] not in strings('/proc/device-tree/compatible'):
        raise RuntimeError('target board incompatible with pinned P3768/P3767 Super profile')
    node = Path('/sys/bus/i2c/devices') / LOCK['sensor_i2c'] / 'of_node'
    if not node.exists() or not str(node.resolve()).endswith(LOCK['sensor_path']):
        raise RuntimeError('bound sensor differs from the pinned IMX219-A node')
    if strings(node / 'compatible') != ['sony,imx219']:
        raise RuntimeError('unexpected sensor compatible')
    for mode in ('mode2', 'mode4'):
        for key, expected in {'num_lanes': '2', 'tegra_sinterface': 'serial_b', 'lane_polarity': '6',
                              'mclk_khz': '24000', 'pix_clk_hz': '182400000', 'line_length': '3448'}.items():
            if strings(node / mode / key) != [expected]:
                raise RuntimeError(f'live topology differs: {mode}/{key}')


def fdt_value(blob, node, prop, kind='s'):
    return run(['fdtget', '-t', kind, blob, node, prop]).stdout.strip()


def check_tree(blob):
    sensor = LOCK['sensor_path']
    if fdt_value(blob, sensor, 'cockpit,bsp-id') != 'imx219-a-r36.5.2-v1':
        raise RuntimeError('BSP device-tree marker missing')
    for mode, width, height in [('mode2', '1920', '1080'), ('mode4', '1280', '720')]:
        for key, value in {'active_w': width, 'active_h': height, 'num_lanes': '2',
                           'tegra_sinterface': 'serial_b', 'lane_polarity': '6',
                           'mclk_khz': '24000', 'pix_clk_hz': '182400000', 'line_length': '3448'}.items():
            if fdt_value(blob, sensor + '/' + mode, key) != value:
                raise RuntimeError(f'merged tree has wrong {mode}/{key}')
    # Verify all enabled sensor -> CSI -> VI links are reciprocal, not just that
    # strings containing lane_polarity appear somewhere in an overlay dump.
    text = run(['dtc', '-I', 'dtb', '-O', 'dts', blob]).stdout
    if 'sony,imx219' not in text:
        raise RuntimeError('missing IMX219 sensor')
    endpoints = []
    def walk(node):
        children = run(['fdtget', '-l', blob, node]).stdout.splitlines()
        for child in children:
            path = node.rstrip('/') + '/' + child
            if child.startswith('endpoint'):
                remote = run(['fdtget', '-t', 'x', blob, path, 'remote-endpoint'], check=False)
                phandle = run(['fdtget', '-t', 'x', blob, path, 'phandle'], check=False)
                if not remote.returncode and not phandle.returncode:
                    endpoints.append((path, int(phandle.stdout.strip(), 16), int(remote.stdout.strip(), 16)))
            walk(path)
    # Limit traversal to the relevant sensor, NVCSI and VI subtrees.
    for path in [sensor, '/bus@0/host1x@13e00000/nvcsi@15a00000', '/tegra-capture-vi']:
        walk(path)
    links = {own: (path, remote) for path, own, remote in endpoints}
    starts = [(own, remote) for path, own, remote in endpoints if path.startswith(sensor + '/')]
    if len(starts) != 1:
        raise RuntimeError('expected one IMX219 source endpoint')
    for _, own, remote in endpoints:
        if remote not in links or links[remote][1] != own:
            raise RuntimeError('CSI/VI endpoint link not reciprocal')
    for own, remote in starts:
        if remote not in links or links[remote][1] != own:
            raise RuntimeError('sensor/CSI endpoint link not reciprocal')
        if fdt_value(blob, links[own][0], 'port-index', 'i') != '1':
            raise RuntimeError('sensor port-index must be 1')
    if len(endpoints) < 4:
        raise RuntimeError('incomplete sensor/CSI/VI endpoint graph')
    return endpoints


def snapshot(out):
    report = {'timestamp': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'uname': list(platform.uname()), 'read_only': True, 'commands': {}}
    try:
        target_check()
        report['target_match'] = True
    except Exception as error:
        report['target_match'] = False
        report['target_error'] = str(error)
    for name, command in {
        'release': ['cat', '/etc/nv_tegra_release'], 'module': ['modinfo', 'nv_imx219'],
        'devices': ['v4l2-ctl', '--list-devices'], 'graph': ['media-ctl', '-p', '-d', '/dev/media0'],
        'controls': ['v4l2-ctl', '-d', '/dev/video0', '--list-ctrls-menus'],
        'formats': ['v4l2-ctl', '-d', '/dev/video0', '--list-formats-ext'],
        'argus': ['systemctl', 'show', 'nvargus-daemon', '-p', 'ActiveState', '-p', 'MainPID', '-p', 'NRestarts'],
        'kernel_log': ['journalctl', '-k', '-b', '-n', '200', '--no-pager'],
    }.items():
        proc = run(command, check=False)
        report['commands'][name] = {'exit_code': proc.returncode, 'stdout': proc.stdout, 'stderr': proc.stderr}
    report['extlinux'] = Path('/boot/extlinux/extlinux.conf').read_text()
    report['secure_boot'] = secure_boot()
    write_json(out / 'preflight.json', report)
    print(json.dumps({'report': str(out / 'preflight.json'), 'target_match': report['target_match'],
                      'secure_boot': report['secure_boot']}, indent=2))
    return 0 if report['target_match'] else 1


def secure_boot():
    values = list(Path('/sys/firmware/efi/efivars').glob('SecureBoot-*'))
    if len(values) != 1:
        return 'unknown'
    try:
        data = values[0].read_bytes()
        return 'enabled' if len(data) == 5 and data[4] == 1 else 'disabled' if len(data) == 5 and data[4] == 0 else 'unknown'
    except OSError:
        return 'unknown'


def build(args):
    target_check()
    oot = checked_sources(args.oot, LOCK['source_sha256'], LOCK['oot_commit'])
    dts = checked_sources(args.dts, LOCK['dts_sha256'], LOCK['dts_commit'])
    kernel = Path(args.kernel).resolve(strict=True)
    headers = Path(args.oot_headers).resolve(strict=True)
    for header in sorted((headers / 'include').rglob('*.h')):
        relative = header.relative_to(headers)
        if not (oot / relative).is_file() or sha(header) != sha(oot / relative):
            raise RuntimeError('installed OOT headers differ from pinned source: ' + str(relative))
    if run(['make', '-sC', kernel, 'kernelrelease']).stdout.strip() != LOCK['kernel_release']:
        raise RuntimeError('kernel headers do not match running release')
    if not (kernel / 'Module.symvers').is_file() or not (headers / 'Module.symvers').is_file():
        raise RuntimeError('matching kernel AND OOT Module.symvers are required')
    out = Path(args.out).resolve()
    if out == ROOT or ROOT not in out.parents or '_output' not in out.relative_to(ROOT).parts:
        raise RuntimeError('build output must be under repository _output/, never /boot or sources')
    out.mkdir(parents=True, exist_ok=True)
    manifest_file = out / 'manifest.json'
    manifest_file.unlink(missing_ok=True)  # Failed builds must not leave an apparently valid old bundle.
    stage = out / 'module'
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir()
    vendor = oot / 'drivers/media/i2c'
    (stage / 'nv_imx219.c').write_bytes((vendor / 'nv_imx219.c').read_bytes())
    patch = BSP / 'patches/0001-imx219-test-pattern-lifecycle.patch'
    # Apply in a minimal vendor-shaped staging tree, never touch downloaded sources.
    patch_tree = out / 'patch-tree/drivers/media/i2c'
    patch_tree.mkdir(parents=True, exist_ok=True)
    shutil.copy2(vendor / 'nv_imx219.c', patch_tree)
    run(['git', 'apply', '--check', patch], cwd=out / 'patch-tree')
    run(['git', 'apply', patch], cwd=out / 'patch-tree')
    local = BSP / 'drivers/media/i2c/imx219'
    require_equal(patch_tree / 'nv_imx219.c', local / 'nv_imx219.c', 'nv_imx219.c')
    require_equal(vendor / 'imx219_mode_tbls.h', local / 'imx219_mode_tbls.h', 'pinned mode table')
    shutil.copy2(local / 'nv_imx219.c', stage)
    shutil.copy2(local / 'imx219_mode_tbls.h', stage)
    for name in ('Kbuild', 'cockpit_imx219_test_pattern.h'):
        shutil.copy2(local / name, stage)
    # Run official NVIDIA API conftests. Their script changes cwd to itself, so
    # its preamble must explicitly find the output's generated conftest/headers.h.
    conf = out / 'conftest'
    conf.mkdir(exist_ok=True)
    command = ['make', '-B', '-f', oot / 'scripts/conftest/Makefile', 'all',
               f'src={oot / "scripts/conftest"}', f'obj={conf}', f'NV_KERNEL_SOURCES={kernel}',
               f'NV_KERNEL_OUTPUT={kernel}', 'ARCH=arm64', f'CC={args.cc}', f'EXTRA_CFLAGS=-I{conf}',
               'NV_CONFTEST_FUNCTION_COMPILE_TESTS=i2c_driver_struct_probe_without_i2c_device_id_arg i2c_driver_struct_remove_return_type_int']
    proc = run(command, cwd=conf)
    (out / 'conftest.log').write_text(proc.stdout + proc.stderr)
    (conf / 'nvidia').mkdir(exist_ok=True)
    shutil.copy2(oot / 'scripts/conftest/conftest.h', conf / 'nvidia/conftest.h')
    if '#define NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT' not in (conf / 'conftest/functions.h').read_text():
        raise RuntimeError('NVIDIA conftest failed to identify target I2C ABI; do not hardcode around it')
    proc = run(['make', '-C', kernel, f'M={stage}', f'NVIDIA_OOT_HEADERS={headers}',
                f'NV_CONFTEST_OUTPUT={conf}', f'KBUILD_EXTRA_SYMBOLS={headers / "Module.symvers"}',
                'CONFIG_MODULE_SIG_ALL=n', f'CC={args.cc}', f'-j{args.jobs}', 'modules'])
    (out / 'module-build.log').write_text(proc.stdout + proc.stderr)
    module = stage / 'nv_imx219.ko'
    if modinfo(module, 'vermagic') != modinfo('nv_imx219', 'vermagic'):
        raise RuntimeError('built module vermagic differs from installed module')
    if modinfo(module, 'version') != LOCK['module_version']:
        raise RuntimeError('module BSP version missing')
    imported = check_crcs(module, kernel, headers)
    overlay = out / 'tegra234-cockpit-imx219-a.dtbo'
    preprocessed = run(['cpp', '-nostdinc', '-undef', '-x', 'assembler-with-cpp',
                        '-I', dts / 'overlay', '-I', dts / 'include/platforms',
                        '-I', dts / 'include/kernel', '-I', BSP / 'device-tree',
                        BSP / 'device-tree/overlays/tegra234-cockpit-imx219-a.dts']).stdout
    (out / 'overlay.pp.dts').write_text(preprocessed)
    proc = run(['dtc', '-@', '-I', 'dts', '-O', 'dtb', '-o', overlay, out / 'overlay.pp.dts'])
    (out / 'dtc.log').write_text(proc.stderr)
    merged = out / 'merged.dtb'
    run(['fdtoverlay', '-i', LOCK['base_dtb'], '-o', merged, overlay])
    endpoints = check_tree(merged)
    shutil.copy2(module, out / 'nv_imx219.ko')
    result = {'schema': 1, 'release': LOCK['release'], 'kernel_release': LOCK['kernel_release'],
              'module_version': LOCK['module_version'], 'board_compatible': LOCK['board_compatible'],
              'vermagic': modinfo(module, 'vermagic'), 'oot_commit': LOCK['oot_commit'],
              'dts_commit': LOCK['dts_commit'], 'compiler': run([args.cc, '--version']).stdout.splitlines()[0],
              'kernel_config_sha256': sha(kernel / '.config'), 'kernel_symvers_sha256': sha(kernel / 'Module.symvers'),
              'oot_symvers_sha256': sha(headers / 'Module.symvers'), 'oot_headers_sha256': header_digest(headers), 'base_dtb_sha256': sha(LOCK['base_dtb']),
              'stock_overlay_sha256': sha(LOCK['stock_overlay']), 'baseline_lock_sha256': sha(BSP / 'baseline.lock.json'),
              'patch_sha256': sha(patch), 'complete_driver_sha256': sha(local / 'nv_imx219.c'),
              'mode_table_sha256': sha(local / 'imx219_mode_tbls.h'),
              'extension_sha256': sha(local / 'cockpit_imx219_test_pattern.h'),
              'imported_symbol_crcs': imported, 'endpoints': endpoints,
              'artifacts': {name: sha(out / name) for name in ('nv_imx219.ko', overlay.name)},
              'hardware_load_tested': False, 'overlay_boot_tested': False}
    write_json(manifest_file, result)
    print(json.dumps({'artifacts': str(out), 'vermagic': result['vermagic'],
                      'imports_crc_checked': len(imported), 'hardware_load_tested': False}, indent=2))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    preflight = commands.add_parser('preflight', help='read-only target/driver/topology evidence')
    preflight.add_argument('--out', type=Path, default=DEFAULT_OUT / 'preflight')
    compile_cmd = commands.add_parser('build', help='offline, pinned, native Kbuild + overlay build')
    compile_cmd.add_argument('--oot', type=Path, required=True)
    compile_cmd.add_argument('--dts', type=Path, required=True)
    compile_cmd.add_argument('--kernel', type=Path, default=Path('/lib/modules') / platform.release() / 'build')
    compile_cmd.add_argument('--oot-headers', type=Path, default=Path('/usr/src/nvidia/nvidia-oot'))
    compile_cmd.add_argument('--out', type=Path, default=DEFAULT_OUT)
    compile_cmd.add_argument('--cc', default='gcc')
    compile_cmd.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.command == 'build' and args.jobs < 1:
        parser.error('--jobs must be positive')
    try:
        return snapshot(args.out) if args.command == 'preflight' else build(args)
    except (OSError, RuntimeError, ValueError) as error:
        print('ERROR: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
