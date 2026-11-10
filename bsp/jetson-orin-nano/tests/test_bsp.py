#!/usr/bin/env python3
"""Host-only negative/transaction tests; do not change /boot, modules or services."""
import array
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

BSP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BSP / 'scripts'))
import bsp
import deployment
import verify


class SafetyTests(unittest.TestCase):
    def test_crc_conflict_is_checked_only_for_imported_symbols(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / 'kernel', Path(tmp) / 'oot'
            a.write_text('0x11111111 unused kernel\n0x22222222 needed kernel\n')
            b.write_text('0x33333333 unused oot\n')
            self.assertEqual(bsp.crc_table(a, b, symbols={'needed'}), {'needed': '0x22222222'})
            with self.assertRaisesRegex(RuntimeError, 'conflicting'):
                bsp.crc_table(a, b)

    def test_complete_source_and_patch_must_not_diverge(self):
        with tempfile.TemporaryDirectory() as tmp:
            expected, actual = Path(tmp)/'vendor-patched.c', Path(tmp)/'complete.c'
            expected.write_text('complete implementation'); actual.write_text('complete implementation')
            bsp.require_equal(expected,actual,'driver')
            actual.write_text('edited without updating patch')
            with self.assertRaisesRegex(RuntimeError,'complete source and vendor\+patch disagree'):
                bsp.require_equal(expected,actual,'driver')

    def test_complete_mode_table_matches_pinned_vendor(self):
        local=BSP/'drivers/media/i2c/imx219'
        self.assertEqual(bsp.sha(local/'imx219_mode_tbls.h'), bsp.LOCK['source_sha256']['drivers/media/i2c/imx219_mode_tbls.h'])
        source=(local/'nv_imx219.c').read_text()
        for symbol in ['imx219_probe', 'imx219_remove', 'imx219_set_gain', 'imx219_set_exposure',
                       'imx219_set_frame_rate', 'imx219_power_on', 'imx219_power_off',
                       'imx219_set_mode', 'imx219_start_streaming', 'imx219_stop_streaming']:
            self.assertIn(symbol, source)
        self.assertNotIn('../platform/', source)

    def test_checked_sources_rejects_wrong_revision(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(bsp, 'run', return_value=mock.Mock(stdout='wrong\n')):
            with self.assertRaisesRegex(RuntimeError, 'wrong source commit'):
                bsp.checked_sources(tmp, {}, 'pinned')

    def test_extlinux_uses_default_not_just_a_matching_unused_stanza(self):
        valid = ('DEFAULT JetsonIO\nLABEL JetsonIO\n\tFDT\t' + bsp.LOCK['base_dtb'] +
                 '\n\tOVERLAYS ' + bsp.LOCK['stock_overlay'] + '\n')
        self.assertEqual(deployment.boot_profile(valid), 'JetsonIO')
        with self.assertRaisesRegex(RuntimeError, 'default extlinux'):
            deployment.boot_profile('DEFAULT primary\nLABEL primary\nLINUX /boot/Image\n' + valid.replace('DEFAULT JetsonIO\n',''))

    def test_backup_json_update_is_atomic(self):
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'backup.json'
            bsp.write_json(target, {'status':'backed_up'})
            with mock.patch.object(os,'replace',side_effect=OSError('injected state rename failure')):
                with self.assertRaises(OSError):
                    bsp.write_json(target, {'status':'installed'})
            self.assertEqual(json.loads(target.read_text()), {'status':'backed_up'})
            self.assertFalse(list(Path(tmp).glob('.backup.json-*')))

    def test_atomic_copy_restores_content_and_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            original, replacement = Path(tmp) / 'boot.dtbo', Path(tmp) / 'source.dtbo'
            original.write_bytes(b'original');original.chmod(0o640);replacement.write_bytes(b'enhanced')
            deployment.atomic_copy(replacement, original)
            self.assertEqual(original.read_bytes(), b'enhanced')
            self.assertEqual(original.stat().st_mode & 0o777, 0o640)
            self.assertFalse(list(Path(tmp).glob('.cockpit-bsp-*')))

    def test_no_symlink_target_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target, link = [Path(tmp) / x for x in ('source','target','link')]
            source.write_bytes(b'new');target.write_bytes(b'old');link.symlink_to(target)
            with self.assertRaisesRegex(RuntimeError, 'non-symlink'):
                deployment.atomic_copy(source, link)
            self.assertEqual(target.read_bytes(), b'old')

    def test_refuses_tampered_bundle_before_target_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp);(path / 'manifest.json').write_text(json.dumps({
                'schema':1,'kernel_release':bsp.LOCK['kernel_release'],
                'baseline_lock_sha256':bsp.sha(BSP / 'baseline.lock.json'),
                'artifacts':{'nv_imx219.ko':'fake','tegra234-cockpit-imx219-a.dtbo':'fake'}}))
            (path / 'nv_imx219.ko').write_bytes(b'corrupt');(path / 'tegra234-cockpit-imx219-a.dtbo').write_bytes(b'corrupt')
            with self.assertRaisesRegex(RuntimeError, 'hash mismatch'):
                deployment.bundle(path)

    def test_apply_refuses_unprivileged(self):
        with mock.patch.object(os, 'geteuid', return_value=1000):
            with self.assertRaisesRegex(RuntimeError, 'requires root'):
                deployment.idle()

    def test_apply_refuses_running_argus(self):
        with mock.patch.object(os, 'geteuid', return_value=0), mock.patch.object(deployment, 'run', return_value=mock.Mock(returncode=0)):
            with self.assertRaisesRegex(RuntimeError, 'stop nvargus'):
                deployment.idle()

    def test_restore_restores_all_files(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(deployment, 'run'), mock.patch.object(os, 'sync'):
            folder=Path(tmp);entries=[]
            for name in ('module','overlay','extlinux'):
                source=folder/(name+'.backup');target=folder/name
                source.write_text('old '+name);target.write_text('new '+name)
                entries.append({'target':str(target),'backup':source.name,'sha256':bsp.sha(source)})
            deployment.restore_entries(folder, {'entries':entries})
            for name in ('module','overlay','extlinux'):
                self.assertEqual((folder/name).read_text(),'old '+name)

    def test_restore_does_not_use_corrupt_backup(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(deployment, 'run') as command, mock.patch.object(os, 'sync'):
            folder=Path(tmp);source=folder/'backup';target=folder/'target'
            source.write_text('corrupt');target.write_text('new')
            with self.assertRaisesRegex(RuntimeError,'MANUAL RECOVERY'):
                deployment.restore_entries(folder, {'entries':[{'target':str(target),'backup':'backup','sha256':'wrong'}]})
            self.assertEqual(target.read_text(),'new');command.assert_not_called()

    def test_failed_deploy_restores_previous_module_and_overlay(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bundle=root/'bundle';bundle.mkdir();backup=root/'backups';boot=root/'boot';boot.mkdir()
            module=boot/'module';overlay=boot/'overlay';config=boot/'extlinux';
            for p in (module,overlay,config):p.write_bytes(b'original')
            (bundle/'nv_imx219.ko').write_bytes(b'enhanced module')
            (bundle/'tegra234-cockpit-imx219-a.dtbo').write_bytes(b'enhanced overlay')
            manifest={'stock_overlay_sha256':bsp.sha(overlay)}
            calls=0;real_copy=deployment.atomic_copy
            def fail_second(source,destination):
                nonlocal calls
                calls+=1
                if calls==2:raise OSError('injected overlay rename failure')
                return real_copy(source,destination)
            lock={**bsp.LOCK,'stock_overlay':str(overlay)}
            with mock.patch.object(deployment,'target_check'), mock.patch.object(deployment,'bundle',return_value=(bundle,manifest,module)), \
                 mock.patch.object(deployment,'LOCK',lock), mock.patch.object(deployment,'EXTLINUX',config), \
                 mock.patch.object(deployment,'BACKUP_ROOT',backup), mock.patch.object(deployment,'idle'), \
                 mock.patch.object(deployment,'run'), mock.patch.object(deployment,'check_tree'), \
                 mock.patch.object(deployment.os,'sync'), mock.patch.object(deployment.os,'geteuid',return_value=0), \
                 mock.patch.object(deployment,'atomic_copy',side_effect=fail_second):
                # Fake root ownership only for the temporary backup guard.
                original_stat=Path.stat
                def stat_root(path,*a,**k):
                    st=original_stat(path,*a,**k)
                    if path==backup:
                        values=list(st);values[4]=0;return os.stat_result(values)
                    return st
                with mock.patch.object(Path,'stat',stat_root):
                    with self.assertRaisesRegex(OSError,'injected'):
                        deployment.deploy(bundle,True)
            for p in (module,overlay,config):self.assertEqual(p.read_bytes(),b'original')
            state=json.loads(next(backup.glob('*/backup.json')).read_text())
            self.assertEqual(state['status'],'deploy_failed_restored')

    def test_pattern_pixel_analysis_and_false_positive_rejection(self):
        width,height=64,64
        black=array.array('H',[0])*(width*height)
        white=array.array('H',[1023])*(width*height)
        bars=array.array('H',[(x//8)*128 for y in range(height) for x in range(width)])
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'bars.pgm'
            result=verify.analyse(black,white,bars,width,height,output)
            self.assertTrue(result['raw_pixels_changed']);self.assertTrue(output.read_bytes().startswith(b'P5'))
            with self.assertRaisesRegex(RuntimeError,'did not change'):
                verify.analyse(black,black,bars,width,height,output)

    def test_raw_bad_stride_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            raw=Path(tmp)/'frame.raw';raw.write_bytes(b'bad')
            with self.assertRaisesRegex(RuntimeError,'size'):
                verify.raw_frame(raw,1280,720,2560)

    def test_driver_local_fault_injection(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable=Path(tmp)/'pattern-test'
            subprocess.run(['cc','-std=c11','-pthread','-I',str(BSP/'drivers/media/i2c/imx219'),
                            str(BSP/'tests/test_pattern_test.c'),'-o',str(executable)],check=True)
            subprocess.run([str(executable)],check=True)


if __name__=='__main__':
    unittest.main()
