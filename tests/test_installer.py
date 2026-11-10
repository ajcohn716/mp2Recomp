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
        self.assertEqual(local_drop_path([QUrl.fromLocalFile(str(self.rom))]),str(self.rom))
        self.assertIsNone(local_drop_path([QUrl('https://example.invalid/rom')]))
        self.assertIsNone(local_drop_path([]))

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
