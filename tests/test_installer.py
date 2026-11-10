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
                binary=cwd/'build/native/mp2recomp';binary.parent.mkdir(parents=True);binary.write_text('#!/bin/sh\nexit 0\n')
        with patch.object(install,'ROOT',project),patch.object(install,'source_files',return_value=['install.py']),patch.object(install,'system_problems',return_value=[]),patch.object(install,'source_digest',return_value='abcdef'),patch.dict(os.environ,{'XDG_DATA_HOME':str(self.root/'desktop')}):
            engine=install.Installer(lambda _:None,lambda _:None);engine.run=run
            launcher=engine.install(self.rom,prefix,build,2)
            self.assertTrue(os.access(launcher,os.X_OK))
            self.assertEqual((prefix/install.ROM_NAME).read_bytes(),self.data)
            self.assertEqual((prefix/'userdata/save').read_text(),'keep me')
            self.assertFalse((build/'source/forbidden.bin').exists())
            self.assertIn('-j',commands[-1])
            self.assertIn(str(launcher),(self.root/'desktop/applications/mp2recomp.desktop').read_text())
            before=(prefix/'mp2recomp').read_bytes()
            engine.run=lambda *a,**k:(_ for _ in ()).throw(install.InstallError('fake build failure'))
            with self.assertRaises(install.InstallError):engine.install(self.rom,prefix,build,2)
            self.assertEqual((prefix/'mp2recomp').read_bytes(),before)
    def test_cancel_before_install(self):
        engine=install.Installer(lambda _:None,lambda _:None);engine.cancel()
        with self.assertRaises(install.Cancelled):engine.install(self.rom,self.root/'install')
        self.assertFalse((self.root/'install').exists())
    def test_cli_validation(self):
        self.assertEqual(install.main(['--rom',str(self.rom),'--validate-rom']),0)
    def test_gui_drop_local_uri(self):
        try:from tools.installer_gui import Window
        except ImportError:self.skipTest('GTK3 bindings not available')
        values=[];finished=[]
        fake=SimpleNamespace(running=False,rom=SimpleNamespace(set_text=values.append),status=SimpleNamespace(set_text=values.append))
        context=SimpleNamespace(finish=lambda *args:finished.append(args))
        Window.dropped(fake,None,context,0,0,SimpleNamespace(get_uris=lambda:[self.rom.as_uri()]),0,0)
        self.assertEqual(values[-1],str(self.rom));self.assertTrue(finished[-1][0])
        Window.dropped(fake,None,context,0,0,SimpleNamespace(get_uris=lambda:['https://example.invalid/rom']),0,0)
        self.assertFalse(finished[-1][0])

if __name__=='__main__':unittest.main()
