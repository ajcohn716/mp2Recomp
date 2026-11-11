"""Signed release packaging tests use synthetic payloads, never game code."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from tools import windows_release as release
from tools.dependency_pack import sha256
from tools.prebuilt_runtime import recipe_digest, runtime_targets
from tools.steam_sdk import bundle_names, helper_recipe, pack_helper, validate_helper

ROOT = Path(__file__).resolve().parents[1]


class WindowsReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage = self.root / 'sdk-stage'
        self.stage.mkdir()
        tools = {
            'python': 'python/python.exe', 'recompiler': 'tools/N64Recomp.exe',
            'rsp': 'tools/RSPRecomp.exe', 'cmake': 'toolchain/bin/cmake.exe',
            'ninja': 'toolchain/bin/ninja.exe', 'cc': 'toolchain/bin/clang.exe',
            'cxx': 'toolchain/bin/clang++.exe',
        }
        for name in tools.values():
            path = self.stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'synthetic executable')
        (self.stage / 'sdk.json').write_text(json.dumps({
            'format': 1, 'platform': 'windows-x86_64', 'tools': tools,
            'steam_helper': 'steam', 'prebuilt_runtime': 'runtime',
        }))
        self.helper = self.stage / 'steam'
        self.helper.mkdir()
        for name in bundle_names('windows-x86_64'):
            (self.helper / name).write_bytes(b'synthetic helper')
        pack_helper(ROOT, self.helper, 'windows-x86_64')
        libraries = self.stage / 'runtime/libraries'
        libraries.mkdir(parents=True)
        hashes = {}
        for name in runtime_targets(ROOT):
            library = libraries / (name + '.a')
            library.write_bytes(b'synthetic open-source archive')
            hashes[name] = sha256(library)
        (libraries.parent / 'runtime.json').write_text(json.dumps({
            'format': 1, 'recipe': recipe_digest(ROOT), 'libraries': hashes,
        }))
        self.archive = self.root / 'mp2recomp-sdk-windows-x86_64.zip'
        shutil.make_archive(str(self.archive.with_suffix('')), 'zip', self.stage)
        self.metadata_path = self.archive.with_suffix('.json')
        self.original_metadata = {
            'platform': 'windows-x86_64', 'filename': self.archive.name,
            'sha256': sha256(self.archive), 'runtime_recipe': recipe_digest(ROOT),
            'steam_helper_recipe': helper_recipe(ROOT),
        }
        self.metadata_path.write_text(json.dumps(self.original_metadata))
        # Cryptographic trust is checked by PowerShell on Windows. Isolate that
        # boundary here so archive/checksum behavior is also tested on Linux.
        verifier = patch.object(release, 'verify_binaries')
        self.verify = verifier.start()
        self.addCleanup(verifier.stop)
        platform = patch('tools.dependency_pack.platform_key', return_value='windows-x86_64')
        platform.start()
        self.addCleanup(platform.stop)

    def test_signing_refreshes_helper_and_outer_checksums(self):
        helper = self.helper / 'mp2recomp-steam-helper.exe'
        helper.write_bytes(helper.read_bytes() + b'synthetic signature')
        release.finalize_sdk(self.stage, self.metadata_path)
        self.verify.assert_called_once_with(self.stage)
        metadata = json.loads(self.metadata_path.read_text())
        self.assertEqual(metadata['windows_signing'], 1)
        self.assertNotEqual(metadata['sha256'], self.original_metadata['sha256'])
        self.assertEqual(metadata['sha256'], sha256(self.archive))
        with zipfile.ZipFile(self.archive) as archive:
            self.assertEqual(archive.read('steam/mp2recomp-steam-helper.exe'), helper.read_bytes())
            manifest = json.loads(archive.read('steam/steam.json'))
            self.assertEqual(manifest['files'][helper.name], sha256(helper))
        validate_helper(self.helper, 'windows-x86_64', ROOT)
        release.verify_sdk(self.archive, self.metadata_path)
        self.assertEqual(self.verify.call_count, 2)

    def test_failed_verification_cannot_publish_or_reseal(self):
        before_manifest = (self.helper / 'steam.json').read_bytes()
        self.verify.side_effect = RuntimeError('unsigned N64Recomp.exe')
        with self.assertRaisesRegex(RuntimeError, 'unsigned N64Recomp'):
            release.finalize_sdk(self.stage, self.metadata_path)
        self.assertEqual(json.loads(self.metadata_path.read_text()), self.original_metadata)
        self.assertEqual(sha256(self.archive), self.original_metadata['sha256'])
        self.assertEqual((self.helper / 'steam.json').read_bytes(), before_manifest)

    def test_extra_helper_file_is_rejected(self):
        (self.helper / 'unreviewed.exe').write_bytes(b'not allowlisted')
        with self.assertRaisesRegex(RuntimeError, 'Unexpected Steam helper'):
            release.finalize_sdk(self.stage, self.metadata_path)
        self.assertEqual(sha256(self.archive), self.original_metadata['sha256'])

    def test_unsigned_sdk_reuse_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'predates Windows signing'):
            release.verify_sdk(self.archive, self.metadata_path)
        self.verify.assert_not_called()

    def test_signed_sdk_marker_does_not_replace_crypto_verification(self):
        metadata = dict(self.original_metadata, windows_signing=1)
        self.metadata_path.write_text(json.dumps(metadata))
        self.verify.side_effect = RuntimeError('signature missing')
        with self.assertRaisesRegex(RuntimeError, 'signature missing'):
            release.verify_sdk(self.archive, self.metadata_path)

    def test_corrupt_signed_archive_is_rejected_before_extraction(self):
        release.finalize_sdk(self.stage, self.metadata_path)
        self.verify.reset_mock()
        self.archive.write_bytes(b'corrupt zip')
        with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
            release.verify_sdk(self.archive, self.metadata_path)
        self.verify.assert_not_called()

    def test_installer_archive_and_checksum_contain_signed_bytes(self):
        folder = self.root / 'installer'
        folder.mkdir()
        payload = b'synthetic installer with signature'
        (folder / 'mp2recomp-installer.exe').write_bytes(payload)
        shutil.copy2(self.archive, folder / self.archive.name)
        archive = self.root / 'mp2recomp-installer-windows-x86_64.zip'
        release.finalize_installer(folder, archive)
        self.verify.assert_called_once_with(folder)
        with zipfile.ZipFile(archive) as zipped:
            self.assertEqual(zipped.read('mp2recomp-installer.exe'), payload)
            self.assertEqual(zipped.read(self.archive.name), self.archive.read_bytes())
        self.assertEqual(archive.with_name(archive.name + '.sha256').read_text(),
                         sha256(archive) + '  ' + archive.name + '\n')

    def test_missing_configuration_fails_with_actionable_names(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, 'AZURE_SIGNING_CLIENT_ID'):
                release.check_configuration()
        with patch.dict(os.environ, {key: 'configured' for key in release.SIGNING_VARIABLES}):
            release.check_configuration()


@unittest.skipUnless(shutil.which('pwsh'), 'PowerShell is required for signature inventory tests')
class SignatureInventoryTests(unittest.TestCase):
    def run_inventory(self, mode, statuses, success=True):
        with tempfile.TemporaryDirectory(prefix='signing with spaces ') as temporary:
            root = Path(temporary)
            payload = root / 'payload'
            payload.mkdir()
            for name in statuses:
                path = payload / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'synthetic binary')
            (payload / 'ignore.a').write_bytes(b'never sign static archives')
            catalog = root / 'catalog.txt'
            env = dict(os.environ, SIGNING_TEST_STATUSES=json.dumps(statuses),
                       SIGNING_TEST_ROOT=str(payload), SIGNING_TEST_CATALOG=str(catalog),
                       SIGNING_TEST_MODE=mode, SIGNING_TEST_SCRIPT=str(ROOT / 'tools/windows_signing.ps1'),
                       GITHUB_OUTPUT=str(root / 'output'))
            # Stub the OS trust API to exercise discovery, portable signature
            # selection, catalog paths and failures without any signing key.
            script = '''
function Get-AuthenticodeSignature {
    param($LiteralPath)
    $statuses = $env:SIGNING_TEST_STATUSES | ConvertFrom-Json -AsHashtable
    $relative = [IO.Path]::GetRelativePath($env:SIGNING_TEST_ROOT, $LiteralPath).Replace('\\', '/')
    $entry = $statuses[$relative]
    [pscustomobject]@{ Status = $entry[0]; SignatureType = $entry[1] }
}
& $env:SIGNING_TEST_SCRIPT -Mode $env:SIGNING_TEST_MODE -Root $env:SIGNING_TEST_ROOT -Catalog $env:SIGNING_TEST_CATALOG
'''
            result = subprocess.run(['pwsh', '-NoProfile', '-Command', script], env=env,
                                    capture_output=True, text=True)
            if success:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            else:
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            return ([Path(line).as_posix().replace('\\', '/') for line in catalog.read_text().splitlines()]
                    if catalog.exists() else [], result.stdout + result.stderr)

    def test_only_unsigned_and_machine_catalog_binaries_are_selected(self):
        catalog, _ = self.run_inventory('Prepare', {
            'N64Recomp.EXE': ['NotSigned', 'None'],
            'nested/extension.pyd': ['NotSigned', 'None'],
            'upstream.dll': ['Valid', 'Authenticode'],
            'catalog-only.exe': ['Valid', 'Catalog'],
        })
        self.assertEqual(set(catalog), {'payload/N64Recomp.EXE',
                                       'payload/nested/extension.pyd', 'payload/catalog-only.exe'})

    def test_invalid_existing_signature_stops_signing(self):
        _, output = self.run_inventory('Prepare', {'bad.exe': ['HashMismatch', 'Authenticode']}, False)
        self.assertIn('Invalid existing signature', output)

    def test_verify_rejects_unsigned_and_catalog_only_files(self):
        for status, kind in [('NotSigned', 'None'), ('Valid', 'Catalog')]:
            with self.subTest(status=status, kind=kind):
                _, output = self.run_inventory('Verify', {'tool.exe': [status, kind]}, False)
                self.assertIn('Missing valid embedded signature', output)

    def test_verify_accepts_embedded_valid_signature(self):
        self.run_inventory('Verify', {'tool.exe': ['Valid', 'Authenticode']})

    def test_no_binaries_is_an_error(self):
        _, output = self.run_inventory('Prepare', {}, False)
        self.assertIn('No Windows binaries', output)


if __name__ == '__main__':
    unittest.main()
