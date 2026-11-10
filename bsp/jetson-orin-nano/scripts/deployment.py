#!/usr/bin/env python3
"""Explicit, reboot-only module/overlay installation. No live rmmod or daemon stop."""
import argparse
import datetime
import fcntl
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile

from bsp import BSP, LOCK, DEFAULT_OUT, check_crcs, check_tree, modinfo, run, secure_boot, sha, header_digest, target_check, write_json

BACKUP_ROOT = Path('/var/backups/cockpit-bsp01')
EXTLINUX = Path('/boot/extlinux/extlinux.conf')


def regular(path):
    path = Path(path)
    if not stat.S_ISREG(path.lstat().st_mode) or path.is_symlink():
        raise RuntimeError('expected regular, non-symlink file: ' + str(path))
    return path


def atomic_copy(source, destination):
    """Same-directory atomic replacement, fsync before rename and after directory update."""
    destination = Path(destination)
    regular(destination)
    mode = stat.S_IMODE(destination.stat().st_mode)
    fd, name = tempfile.mkstemp(prefix='.cockpit-bsp-', dir=destination.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, 'wb') as output, Path(source).open('rb') as input_file:
            shutil.copyfileobj(input_file, output)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, destination)
        fd = os.open(destination.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    finally:
        temporary.unlink(missing_ok=True)


def idle():
    if os.geteuid() != 0:
        raise RuntimeError('--apply requires root; use sudo explicitly, never run the build as root')
    daemon = run(['systemctl', 'show', 'nvargus-daemon', '-p', 'ActiveState', '--value'], check=False)
    if daemon.returncode or daemon.stdout.strip() not in ('inactive', 'failed'):
        raise RuntimeError('stop nvargus-daemon explicitly before apply; script never stops it automatically')
    # Check all V4L nodes, not just video0; root can inspect all process FDs.
    devices = {str(p.resolve()) for pattern in ('video*', 'v4l-subdev*', 'media*') for p in Path('/dev').glob(pattern)}
    holders = []
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            for descriptor in (process / 'fd').iterdir():
                try:
                    if str(descriptor.resolve(strict=True)) in devices:
                        holders.append(process.name)
                        break
                except FileNotFoundError:
                    continue
        except FileNotFoundError:
            continue
        except PermissionError as error:
            raise RuntimeError('cannot reliably check device occupancy') from error
    if holders:
        raise RuntimeError('camera device FDs still held by PIDs ' + ','.join(holders))
    refcount = Path('/sys/module/nv_imx219/refcnt')
    if refcount.exists() and int(refcount.read_text()) != 0:
        raise RuntimeError('nv_imx219 module is referenced')


def boot_profile(config):
    default = None
    sections = {}
    selected = None
    for raw in config.splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        fields = line.split(None, 1)
        key, value = fields[0].upper(), fields[1] if len(fields) > 1 else ''
        if key == 'DEFAULT':
            default = value
        elif key == 'LABEL':
            selected = value
            sections[selected] = {}
        elif selected:
            sections[selected][key] = value
    stanza = sections.get(default, {})
    if stanza.get('FDT') != LOCK['base_dtb'] or LOCK['stock_overlay'] not in stanza.get('OVERLAYS', '').split():
        raise RuntimeError('default extlinux stanza is not the pinned base/IMX219-A overlay')
    return default


def bundle(directory):
    directory = Path(directory).resolve(strict=True)
    regular(directory / 'manifest.json')
    manifest = json.loads((directory / 'manifest.json').read_text())
    if manifest.get('schema') != 1 or manifest.get('kernel_release') != LOCK['kernel_release'] or \
       manifest.get('baseline_lock_sha256') != sha(BSP / 'baseline.lock.json'):
        raise RuntimeError('bundle baseline mismatch; rebuild with this BSP checkout')
    names = {'nv_imx219.ko', 'tegra234-cockpit-imx219-a.dtbo'}
    if set(manifest.get('artifacts', {})) != names:
        raise RuntimeError('unexpected bundle artifact names')
    for name, expected in manifest['artifacts'].items():
        if sha(regular(directory / name)) != expected:
            raise RuntimeError('artifact hash mismatch: ' + name)
    kernel = Path('/lib/modules') / LOCK['kernel_release'] / 'build'
    headers = Path('/usr/src/nvidia/nvidia-oot')
    for field, path in [('kernel_config_sha256', kernel / '.config'),
                        ('kernel_symvers_sha256', kernel / 'Module.symvers'),
                        ('oot_symvers_sha256', headers / 'Module.symvers'),
                        ('base_dtb_sha256', Path(LOCK['base_dtb']))]:
        if manifest.get(field) != sha(path):
            raise RuntimeError('target inputs changed since build: ' + field)
    if manifest.get('oot_headers_sha256') != header_digest(headers):
        raise RuntimeError('installed OOT headers changed since build')
    module = directory / 'nv_imx219.ko'
    if modinfo(module, 'name') != 'nv_imx219' or modinfo(module, 'version') != LOCK['module_version'] or \
       modinfo(module, 'vermagic') != manifest['vermagic']:
        raise RuntimeError('module name/version/vermagic mismatch')
    check_crcs(module, kernel.resolve(), headers)
    installed = regular(Path(modinfo('nv_imx219', 'filename')))
    expected = Path('/lib/modules') / LOCK['kernel_release'] / 'updates/drivers/media/i2c/nv_imx219.ko'
    if installed != expected:
        raise RuntimeError('unexpected module resolution (DKMS/override?): ' + str(installed))
    if modinfo(installed, 'vermagic') != manifest['vermagic']:
        raise RuntimeError('installed module ABI differs')
    # Fail closed for Secure Boot / lockdown; no signing-key hacks or disabling enforcement.
    lockdown = Path('/sys/kernel/security/lockdown')
    if secure_boot() != 'disabled' or (lockdown.exists() and '[none]' not in lockdown.read_text()):
        raise RuntimeError('Secure Boot/lockdown is enabled or unknown; trusted module signing is required')
    if run(['lsinitramfs', '/boot/initrd']).stdout.find('nv_imx219.ko') >= 0:
        raise RuntimeError('initrd contains nv_imx219; automatic initrd replacement is deliberately unsupported')
    config = EXTLINUX.read_text()
    boot_profile(config)
    return directory, manifest, installed


def deploy(directory, apply=False):
    target_check()
    directory, manifest, module = bundle(directory)
    overlay = regular(Path(LOCK['stock_overlay']))
    if sha(overlay) != manifest['stock_overlay_sha256']:
        raise RuntimeError('stock overlay changed since build; rollback/rebuild first')
    with tempfile.TemporaryDirectory(prefix='cockpit-bsp-check-') as temp:
        merged = Path(temp) / 'merged.dtb'
        run(['fdtoverlay', '-i', LOCK['base_dtb'], '-o', merged,
             directory / 'tegra234-cockpit-imx219-a.dtbo'])
        check_tree(merged)
    plan = {'apply': apply, 'module': str(module), 'overlay': str(overlay),
            'reboot_required': True, 'extlinux_modified': False,
            'live_reload': False, 'bundle': str(directory)}
    if not apply:
        print(json.dumps(plan, indent=2))
        return 0
    idle()
    BACKUP_ROOT.mkdir(mode=0o700, parents=True, exist_ok=True)
    if BACKUP_ROOT.is_symlink() or BACKUP_ROOT.stat().st_uid != 0 or stat.S_IMODE(BACKUP_ROOT.stat().st_mode) & 0o022:
        raise RuntimeError('backup root must be a root-owned real directory')
    backup = BACKUP_ROOT / datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    backup.mkdir(mode=0o700)
    entries = []
    for source, name in [(module, 'nv_imx219.ko'), (overlay, 'stock.dtbo'), (EXTLINUX, 'extlinux.conf')]:
        regular(source)
        shutil.copy2(source, backup / name)
        entries.append({'target': str(source), 'backup': name, 'sha256': sha(source)})
    state = {'schema': 1, 'kernel_release': LOCK['kernel_release'], 'entries': entries,
             'status': 'backed_up', 'bundle': manifest,
             'installed_sha256': {str(module): sha(directory / 'nv_imx219.ko'),
                                  str(overlay): sha(directory / 'tegra234-cockpit-imx219-a.dtbo')}}
    write_json(backup / 'backup.json', state)
    # Flush backup media before either boot artifact is replaced. A power loss
    # between renames still needs operator rollback; this is not A/B boot magic.
    os.sync()
    try:
        atomic_copy(directory / 'nv_imx219.ko', module)
        atomic_copy(directory / 'tegra234-cockpit-imx219-a.dtbo', overlay)
        run(['depmod', '-a', LOCK['kernel_release']])
        if Path(modinfo('nv_imx219', 'filename')) != module:
            raise RuntimeError('depmod resolves a different module')
        state['status'] = 'installed_reboot_required'
        write_json(backup / 'backup.json', state)
        os.sync()
    except BaseException:
        restore_entries(backup, state)
        state['status'] = 'deploy_failed_restored'
        write_json(backup / 'backup.json', state)
        raise
    plan['backup'] = str(backup)
    print(json.dumps(plan, indent=2))
    print('Installed on disk only. Reboot manually, then run verify.sh. No daemon was restarted.')
    return 0


def validate_backup(backup):
    backup = Path(backup).resolve(strict=True)
    if backup.parent != BACKUP_ROOT or backup.stat().st_uid != 0 or stat.S_IMODE(backup.stat().st_mode) & 0o022:
        raise RuntimeError('rollback requires a root-owned backup beneath ' + str(BACKUP_ROOT))
    regular(backup / 'backup.json')
    state = json.loads((backup / 'backup.json').read_text())
    if state.get('schema') != 1 or state.get('kernel_release') != os.uname().release:
        raise RuntimeError('backup kernel differs from running kernel')
    allowed = {str(Path('/lib/modules') / LOCK['kernel_release'] / 'updates/drivers/media/i2c/nv_imx219.ko'): 'nv_imx219.ko',
               LOCK['stock_overlay']: 'stock.dtbo', str(EXTLINUX): 'extlinux.conf'}
    if {e['target']: e['backup'] for e in state['entries']} != allowed or len(state['entries']) != 3:
        raise RuntimeError('unexpected backup targets; refusing arbitrary writes')
    for entry in state['entries']:
        if sha(regular(backup / entry['backup'])) != entry['sha256']:
            raise RuntimeError('backup corruption: ' + entry['backup'])
        regular(entry['target'])
    return backup, state


def restore_entries(backup, state):
    errors = []
    for entry in state['entries']:
        try:
            if sha(regular(backup / entry['backup'])) != entry['sha256']:
                raise RuntimeError('backup hash mismatch')
            atomic_copy(backup / entry['backup'], entry['target'])
        except Exception as error:
            errors.append(str(error))
    if not errors:
        run(['depmod', '-a', LOCK['kernel_release']])
    os.sync()
    if errors:
        raise RuntimeError('MANUAL RECOVERY REQUIRED: ' + '; '.join(errors))


def rollback(path, apply=False):
    backup, state = validate_backup(path)
    # Do not overwrite a later kernel/boot update or an administrator's changes.
    for entry in state['entries']:
        expected = {entry['sha256'], state.get('installed_sha256', {}).get(entry['target'])}
        if sha(entry['target']) not in expected:
            raise RuntimeError('target changed outside this transaction: ' + entry['target'])
    if not apply:
        print(json.dumps({'apply': False, 'restore': str(backup), 'reboot_required': True}, indent=2))
        return 0
    idle()
    restore_entries(backup, state)
    state['status'] = 'rolled_back_reboot_required'
    write_json(backup / 'backup.json', state)
    os.sync()
    print('Original module/overlay/extlinux restored. Reboot manually; no live reload performed.')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['deploy', 'rollback'])
    parser.add_argument('--bundle', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--apply', action='store_true', help='explicit root-only disk writes; does NOT reboot')
    args = parser.parse_args()
    lock_fd = None
    try:
        if args.apply:
            if os.geteuid() != 0:
                raise RuntimeError('--apply requires root')
            lock_fd = os.open('/run/lock/cockpit-bsp01.lock',
                              os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
            metadata = os.fstat(lock_fd)
            if metadata.st_uid != 0 or not stat.S_ISREG(metadata.st_mode):
                raise RuntimeError('deployment lock is not root-owned regular file')
            try:
                fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise RuntimeError('another BSP deployment/rollback is running') from error
        if args.command == 'rollback':
            if not args.backup:
                parser.error('rollback requires --backup')
            return rollback(args.backup, args.apply)
        return deploy(args.bundle, args.apply)
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print('ERROR: ' + str(error), file=sys.stderr)
        return 1
    finally:
        if lock_fd is not None:
            os.close(lock_fd)


if __name__ == '__main__':
    raise SystemExit(main())
