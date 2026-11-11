"""Check that portable archives cannot include private build inputs."""
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

from install import InstallError
from tools.dependency_pack import sha256
from tools.package_game import archive_game, payload_files


class GamePackageTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.stage = self.root / 'game'
        self.stage.mkdir()
        self.windows = os.name == 'nt'
        self.files = {'LICENSE', 'THIRD_PARTY_NOTICES.md', 'README.txt'}
        if self.windows:
            self.files.update({'mp2recomp.exe', 'SDL2.dll'})
        else:
            self.files.update({'mp2recomp', 'play', 'lib/libSDL2.so.0'})
        for name in self.files:
            path = self.stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'synthetic package fixture')

    def test_archive_contains_only_game_files_and_matching_checksum(self):
        archive = archive_game(self.stage, self.root / 'artifacts', unsigned_test=self.windows)
        if self.windows:
            with zipfile.ZipFile(archive) as packed:
                members = packed.namelist()
        else:
            with tarfile.open(archive) as packed:
                members = packed.getnames()
        self.assertEqual({name.split('/', 1)[1] for name in members}, self.files)
        checksum = archive.with_name(archive.name + '.sha256')
        self.assertEqual(checksum.read_text().split()[0], sha256(archive))

    def test_reject_roms_generated_source_and_build_tools(self):
        for name in ('userdata/mp2.us.z64', 'src/aspMain.cpp', 'toolchain/bin/N64Recomp.exe'):
            with self.subTest(name=name):
                path = self.stage / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'private file')
                with self.assertRaisesRegex(InstallError, 'Unexpected file'):
                    list(payload_files(self.stage, self.windows))
                path.unlink()

    def test_reject_rom_under_allowed_runtime_filename(self):
        path = self.stage / ('SDL2.dll' if self.windows else 'lib/libSDL2.so.0')
        path.write_bytes(bytes.fromhex('80371240') + b'synthetic payload')
        with self.assertRaisesRegex(InstallError, 'ROM contents'):
            list(payload_files(self.stage, self.windows))


if __name__ == '__main__':
    unittest.main()
