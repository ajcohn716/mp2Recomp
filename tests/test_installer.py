"""Tests use synthetic bytes and fake compiler output, never a game ROM."""
import hashlib
import importlib.util
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import install

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.data=bytes.fromhex('80371240')+b'fixture-data'
        self.rom=self.root/'my rom.z64';self.rom.write_bytes(self.data)
        self.patches=[patch.object(install,'ROM_SIZE',len(self.data)),patch.object(install,'ROM_SHA1',hashlib.sha1(self.data).hexdigest())]
        for p in self.patches:p.start()
    def tearDown(self):
        for p in self.patches:p.stop()
        self.temp.cleanup()
    def test_rom_formats(self):
        for width in (1,2,4):
            data=b''.join(self.data[i:i+width][::-1] for i in range(0,len(self.data),width))
            self.rom.write_bytes(data);self.assertEqual(install.validate_rom(self.rom),self.data)
    def test_reject_bad_rom(self):
        for data in (b'',b'x'*len(self.data)):
            self.rom.write_bytes(data)
            with self.assertRaises(install.InstallError):install.validate_rom(self.rom)
    def test_install_and_failed_upgrade(self):
        project=self.root/'project';project.mkdir();(project/'install.py').write_text('# authored source')
        (project/'forbidden.bin').write_bytes(b'not for copying')
        prefix=self.root/'install with spaces';build=self.root/'build'
        prefix.mkdir();(prefix/'userdata').mkdir();(prefix/'userdata/save').write_text('keep me')
        commands=[]
        def run(command,cwd,env=None):
            commands.append(command)
            if command[:2]==['cmake','--build']:
                binary=cwd/('build/native/mp2recomp.exe' if os.name=='nt' else 'build/native/mp2recomp');binary.parent.mkdir(parents=True);binary.write_text('#!/bin/sh\nexit 0\n')
        with patch.object(install,'ROOT',project),patch.object(install,'source_files',return_value=['install.py']),patch.object(install,'system_problems',return_value=[]),patch.object(install,'source_digest',return_value='abcdef'),patch.dict(os.environ,{'XDG_DATA_HOME':str(self.root/'desktop')}):
            engine=install.Installer(lambda _:None,lambda _:None);engine.run=run
            launcher=engine.install(self.rom,prefix,build,2,source_build=True)
            self.assertTrue(os.access(launcher,os.X_OK))
            self.assertEqual((prefix/install.ROM_NAME).read_bytes(),self.data)
            self.assertEqual((prefix/'userdata/save').read_text(),'keep me')
            self.assertFalse((build/'source/forbidden.bin').exists())
            self.assertIn('-j',commands[-1])
            if os.name!='nt':self.assertIn(str(launcher),(self.root/'desktop/applications/mp2recomp.desktop').read_text())
            before=(prefix/('mp2recomp.exe' if os.name=='nt' else 'mp2recomp')).read_bytes()
            engine.run=lambda *a,**k:(_ for _ in ()).throw(install.InstallError('fake build failure'))
            with self.assertRaises(install.InstallError):engine.install(self.rom,prefix,build,2,source_build=True)
            self.assertEqual((prefix/('mp2recomp.exe' if os.name=='nt' else 'mp2recomp')).read_bytes(),before)
    def test_default_install_directories(self):
        # Exercise the default install/cache layout through the overlap guard.
        env={'LOCALAPPDATA':str(self.root/'local'), 'XDG_DATA_HOME':str(self.root/'data'),
             'XDG_CACHE_HOME':str(self.root/'cache')}
        def run(command,cwd,env=None):
            if command[:2]==['cmake','--build']:
                binary=cwd/'build/native'/('mp2recomp.exe' if os.name=='nt' else 'mp2recomp')
                binary.parent.mkdir(parents=True);binary.write_text('synthetic executable')
        with patch.dict(os.environ,env),patch.object(install,'source_files',return_value=['install.py']),patch.object(install,'system_problems',return_value=[]):
            engine=install.Installer(lambda _:None,lambda _:None);engine.run=run
            launcher=engine.install(self.rom,install.default_prefix(),jobs=1,desktop=False,source_build=True)
            self.assertTrue(launcher.is_file())
            self.assertEqual((launcher.parent/install.ROM_NAME).read_bytes(),self.data)

    def test_cancel_before_install(self):
        engine=install.Installer(lambda _:None,lambda _:None);engine.cancel()
        with self.assertRaises(install.Cancelled):engine.install(self.rom,self.root/'install')
        self.assertFalse((self.root/'install').exists())
    def test_cli_validation(self):
        self.assertEqual(install.main(['--rom',str(self.rom),'--validate-rom']),0)
    def test_gui_drop_local_uri(self):
        try:
            from tools.installer_gui import local_drop_path
            from PySide6.QtCore import QUrl
        except ImportError:self.skipTest('PySide6 not available')
        self.assertEqual(Path(local_drop_path([QUrl.fromLocalFile(str(self.rom))])),self.rom)
        self.assertIsNone(local_drop_path([QUrl('https://example.invalid/rom')]))
        self.assertIsNone(local_drop_path([]))

    def test_gui_contrast_under_dark_and_light_host_palettes(self):
        from tools.installer_gui import apply_theme,Window
        from PySide6.QtWidgets import QApplication,QGroupBox
        from PySide6.QtGui import QPalette,QColor
        app=QApplication.instance() or QApplication([])
        type(self).qt_app=app
        original=QPalette(app.palette())
        def luminance(color):
            values=[v/12.92 if v<=0.04045 else ((v+0.055)/1.055)**2.4
                for v in (color.redF(),color.greenF(),color.blueF())]
            return sum(v*w for v,w in zip(values,(0.2126,0.7152,0.0722)))
        def check(foreground,background):
            values=sorted((luminance(foreground),luminance(background)))
            self.assertGreaterEqual((values[1]+0.05)/(values[0]+0.05),4.5)
        try:
            for dark in (False,True):
                host=QPalette()
                host.setColor(QPalette.ColorRole.Window,QColor('#121212' if dark else '#ffffff'))
                host.setColor(QPalette.ColorRole.Text,QColor('#ffffff' if dark else '#000000'))
                host.setColor(QPalette.ColorRole.WindowText,QColor('#ffffff' if dark else '#000000'))
                host.setColor(QPalette.ColorRole.Base,QColor('#222222' if dark else '#ffffff'))
                app.setPalette(host);apply_theme(app)
                args=SimpleNamespace(rom=None,prefix=self.root,build_dir=None,dependency_pack=None,
                    jobs=2,no_desktop=True,source_build=False,sdk_dir=None)
                window=Window(args);window.findChild(QGroupBox).setChecked(True)
                window.show();app.processEvents()
                for enabled in (True,False):
                    for field in (window.rom,window.prefix,window.log,window.jobs):
                        field.setEnabled(enabled);app.processEvents();palette=field.palette()
                        check(palette.color(QPalette.ColorRole.Text),palette.color(QPalette.ColorRole.Base))
                palette=window.status.palette()
                check(palette.color(QPalette.ColorRole.WindowText),palette.color(QPalette.ColorRole.Window))
                window.close();window.deleteLater();app.processEvents()
        finally:app.setPalette(original)

    @unittest.skipIf(os.name=='nt','Linux runtime data')
    def test_runtime_data_survives_relocation(self):
        import shutil
        sdk=self.root/'sdk';toolchain=sdk/'toolchain'
        xkb=toolchain/'share/xkeyboard-config-2/rules';xkb.mkdir(parents=True)
        (xkb/'evdev').write_text('synthetic keymap rules')
        locale=toolchain/'share/X11/locale';locale.mkdir(parents=True)
        (locale/'compose.dir').write_text('synthetic Compose directory')
        conf=toolchain/'etc/fonts/conf.d';conf.mkdir(parents=True)
        rules=toolchain/'share/fontconfig/conf.avail';rules.mkdir(parents=True)
        (rules/'10-test.conf').write_text('<fontconfig/>')
        (conf/'10-test.conf').symlink_to('../../../share/fontconfig/conf.avail/10-test.conf')
        (conf.parent/'fonts.conf').write_text('/home/runner/should-not-be-copied')
        library=toolchain/'lib';library.mkdir()
        (library/'libwayland-client.so.0').write_text('Wayland client runtime')
        (library/'libvulkan.so.1').write_text('use system graphics loader')
        plugins=library/'libdecor/plugins-1';plugins.mkdir(parents=True)
        (plugins/'libdecor-gtk.so').write_text('Wayland decoration plugin')
        prefix=self.root/'installed';install.install_runtime_libraries(sdk,self.root/'source',prefix)
        moved=self.root/'moved with spaces';prefix.rename(moved);shutil.rmtree(sdk)
        self.assertEqual((moved/'share/X11/xkb/rules/evdev').read_text(),'synthetic keymap rules')
        self.assertEqual((moved/'etc/fonts/conf.d/10-test.conf').read_text(),'<fontconfig/>')
        self.assertNotIn('/home/runner',(moved/'etc/fonts/fonts.conf').read_text())
        self.assertEqual((moved/'lib/libwayland-client.so.0').read_text(),'Wayland client runtime')
        self.assertEqual((moved/'lib/libdecor/plugins-1/libdecor-gtk.so').read_text(),'Wayland decoration plugin')
        self.assertFalse((moved/'lib/libvulkan.so.1').exists())
        self.assertEqual((moved/'share/X11/locale/compose.dir').read_text(),'synthetic Compose directory')

    def test_verified_pack_and_cache(self):
        import io,json,tarfile
        from tools.dependency_pack import ensure_sdk,platform_key
        sdk=self.root/'synthetic-sdk';sdk.mkdir()
        tools={name:'tool-'+name for name in ('python','cmake','ninja','cc','cxx','recompiler','rsp')}
        for name in tools.values():(sdk/name).write_text('synthetic test tool')
        (sdk/'sdk.json').write_text(json.dumps({'format':1,'platform':platform_key(),'tools':tools}))
        archive=self.root/'pack.tar.gz'
        with tarfile.open(archive,'w:gz') as tar:
            for path in sdk.iterdir():tar.add(path,arcname=path.name)
        digest=hashlib.sha256(archive.read_bytes()).hexdigest()
        catalog={'packs':{platform_key():{'filename':archive.name,'sha256':digest,'url':'https://example.invalid/pack'}}}
        (self.root/'release-catalog.json').write_text(json.dumps(catalog))
        engine=install.Installer(lambda _:None,lambda _:None)
        root=ensure_sdk(self.root/'cache',self.root,engine.run,engine.status,engine.check_cancelled,archive)
        self.assertTrue((root/'.ready').is_file())
        self.assertEqual(root,ensure_sdk(self.root/'cache',self.root,engine.run,engine.status,engine.check_cancelled,archive))
        catalog['packs'][platform_key()]['sha256']='0'*64
        (self.root/'release-catalog.json').write_text(json.dumps(catalog))
        with self.assertRaisesRegex(RuntimeError,'checksum mismatch'):
            ensure_sdk(self.root/'badcache',self.root,engine.run,engine.status,engine.check_cancelled,archive)
    def test_archive_traversal_rejected(self):
        import zipfile
        from tools.dependency_pack import extract_archive
        archive=self.root/'bad.zip'
        with zipfile.ZipFile(archive,'w') as z:z.writestr('../outside','not allowed')
        destination=self.root/'extracted';destination.mkdir()
        with self.assertRaisesRegex(RuntimeError,'Unsafe path'):extract_archive(archive,destination)
        self.assertFalse((self.root/'outside').exists())

if __name__=='__main__':unittest.main()
