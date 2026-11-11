"""Exercise long SDK paths with synthetic files; optionally verify a release SDK."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

from tools.dependency_pack import ensure_sdk, platform_key
from tools.sdk_files import copy_sdk_sources, filesystem_path, windows_extended_path


REPORTED_FILE = ('overlapping-bindings.msl31.argument.argument-tier-1.'
                 'decoration-binding.device-argument-buffer.texture-buffer-native.comp')
REFERENCE = 'src/contrib/spirv-cross/reference/opt/shaders-msl/comp/' + REPORTED_FILE


class SdkFileTests(unittest.TestCase):
    def test_windows_drive_and_unc_paths(self):
        cases = {
            r'C:\Users\player\sdk': r'\\?\C:\Users\player\sdk',
            'C:/Users/player/sdk/../cache': r'\\?\C:\Users\player\cache',
            r'\\server\share\sdk': r'\\?\UNC\server\share\sdk',
            r'\\?\C:\sdk': r'\\?\C:\sdk',
            r'\\?\UNC\server\share\sdk': r'\\?\UNC\server\share\sdk',
        }
        for path, expected in cases.items():
            with self.subTest(path=path):
                self.assertEqual(windows_extended_path(path), expected)

    def test_long_archive_cache_and_source_copy(self):
        # A long profile name also exercises extraction, checksum-directory
        # publication, cache reuse, copying, and cleanup on default Windows.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / ('profile-' + 'x' * 60) / 'AppData/Local/mp2recomp-cache'
            archive = root / 'sdk.zip'
            tools = {name: 'tools/' + name for name in
                     ('python', 'cmake', 'ninja', 'cc', 'cxx', 'recompiler', 'rsp')}
            kept = 'src/' + '/'.join(['nested-' + 'x' * 60] * 3) + '/source.cpp'
            with zipfile.ZipFile(archive, 'w') as pack:
                pack.writestr('sdk.json', json.dumps({
                    'format': 1, 'platform': platform_key(), 'tools': tools}))
                for name in tools.values():
                    pack.writestr(name, 'synthetic tool')
                pack.writestr('sources/rt64/' + REFERENCE, 'unused reference')
                pack.writestr('sources/rt64/' + kept, 'needed source')
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            (root / 'release-catalog.json').write_text(json.dumps({'packs': {
                platform_key(): {'filename': archive.name, 'sha256': digest}}}))
            noop = lambda *args: None
            sdk = ensure_sdk(cache, root, noop, noop, noop, archive=archive)
            reference = sdk / 'sources/rt64' / REFERENCE
            self.assertGreater(len(str(reference)), 260)
            self.assertEqual(filesystem_path(reference).read_text(), 'unused reference')
            self.assertTrue(filesystem_path(sdk / '.ready').is_file())
            self.assertEqual(sdk, ensure_sdk(cache, root, noop, noop, noop, archive=archive))
            copied = root / 'build with spaces/source/rt64'
            copy_sdk_sources(sdk / 'sources/rt64', copied)
            self.assertEqual(filesystem_path(copied / kept).read_text(), 'needed source')
            self.assertFalse((copied / 'src/contrib/spirv-cross/reference').exists())
            # TemporaryDirectory's own cleanup needs the same explicit prefix.
            import shutil
            shutil.rmtree(filesystem_path(cache))
            shutil.rmtree(filesystem_path(copied))

    def test_pruning_preserves_sources_cmake_and_licenses(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'rt64'
            spirv = source / 'src/contrib/spirv-cross'
            files = ('reference/expected.comp', 'shaders/input.frag',
                     'shaders-msl-no-opt/input.frag', 'spirv_cross.cpp',
                     'CMakeLists.txt', 'cmake/gitversion.in.h', 'LICENSE',
                     'LICENSES/MIT.txt', 'tests-other/c_api_test.c',
                     '.git/config')
            for name in files:
                path = spirv / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('synthetic fixture')
            # Do not discard RT64's own runtime shaders or unrelated references.
            for name in ('src/shaders/runtime.hlsl', 'reference/needed.txt'):
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('needed')
            copied = root / 'copied'
            copy_sdk_sources(source, copied)
            actual = {p.relative_to(copied).as_posix() for p in copied.rglob('*') if p.is_file()}
            expected = {'src/contrib/spirv-cross/' + name for name in files
                        if name.split('/')[0] not in
                        ('reference', 'shaders', 'shaders-msl-no-opt', '.git')}
            expected.update(('src/shaders/runtime.hlsl', 'reference/needed.txt'))
            self.assertEqual(actual, expected)

    @unittest.skipUnless(os.environ.get('MP2_TEST_SDK_ARCHIVE'), 'release SDK not supplied')
    def test_release_sdk_in_user_cache(self):
        # Release CI supplies the actual pack and catalog after packaging. On
        # Windows this runs in a fresh process with LongPathsEnabled=0.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / ('profile-' + 'x' * 40) / 'AppData/Local/mp2recomp-cache'
            noop = lambda *args: None
            try:
                sdk = ensure_sdk(cache, os.environ['MP2_TEST_CATALOG'],
                                 noop, noop, noop,
                                 archive=os.environ['MP2_TEST_SDK_ARCHIVE'])
                for name in ('N64Recomp', 'runtime', 'rt64', 'decomp',
                             'directx-headers', 'libdatachannel'):
                    copy_sdk_sources(sdk / 'sources' / name, root / 'source' / name)
                spirv = root / 'source/rt64/src/contrib/spirv-cross'
                self.assertTrue((spirv / 'spirv_cross.cpp').is_file())
                self.assertFalse((spirv / 'reference').exists())
                self.assertFalse(any(spirv.glob('shaders*')))
            finally:
                import shutil
                for path in (cache, root / 'source'):
                    if path.exists():
                        shutil.rmtree(filesystem_path(path))


if __name__ == '__main__':
    unittest.main()
