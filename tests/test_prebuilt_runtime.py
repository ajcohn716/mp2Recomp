"""Publication and compatibility checks use synthetic archives, never a ROM."""
import json
from pathlib import Path
import tempfile
import unittest

from tools.prebuilt_runtime import RECIPE_FILES, pack_runtime, runtime_targets, validate_runtime

ROOT = Path(__file__).resolve().parents[1]


class PrebuiltRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        for name in RECIPE_FILES:
            target = self.source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((ROOT / name).read_bytes())
        self.build = self.root / 'build'
        self.build.mkdir()
        (self.build / 'CMakeCache.txt').write_text('MP2_PORTABILITY_CHECK:BOOL=ON\n')
        manifest = []
        for name in runtime_targets(self.source):
            archive = self.build / (name + '.a')
            archive.write_bytes(b'synthetic open-source archive: ' + name.encode())
            manifest.append(name + '|' + str(archive))
        (self.build / 'runtime-libraries.txt').write_text('\n'.join(manifest) + '\n')
        (self.build / 'runtime-toolchain.json').write_text(json.dumps({
            'compiler_id': 'Clang', 'compiler_version': '21.1.8',
            'system': 'Linux', 'processor': 'x86_64'}))
        self.runtime = self.root / 'runtime'

    def test_only_reviewed_dependencies_survive_relocation(self):
        (self.build / 'libmp2_code.a').write_bytes(b'never publish game code')
        (self.build / 'mp2recomp').write_bytes(b'never publish game executable')
        (self.build / 'aspMain.cpp.o').write_bytes(b'never publish microcode')
        pack_runtime(self.source, self.build, self.runtime)
        moved = self.root / 'relocated runtime with spaces'
        self.runtime.rename(moved)
        validate_runtime(self.source, moved)
        self.assertEqual({p.stem for p in (moved / 'libraries').iterdir()},
                         set(runtime_targets(self.source)))
        self.assertEqual({p.name for p in moved.iterdir()}, {'runtime.json', 'libraries'})

    def test_social_header_invalidates_cached_sdk(self):
        pack_runtime(self.source, self.build, self.runtime)
        with (self.source / 'src/netplay_steam_social.hpp').open('a') as stream:
            stream.write('\n// Changed helper client interface\n')
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            validate_runtime(self.source, self.runtime)

    def test_helper_protocol_1_is_rejected(self):
        from tools.steam_sdk import bundle_names, pack_helper, validate_helper
        bundle = self.root / 'steam'
        bundle.mkdir()
        for name in bundle_names('linux-x86_64'):
            (bundle / name).write_bytes(b'synthetic helper file')
        metadata = pack_helper(self.source, bundle, 'linux-x86_64')
        self.assertEqual(metadata['protocol'], 2)
        validate_helper(bundle, 'linux-x86_64', self.source)
        metadata['protocol'] = 1
        (bundle / 'steam.json').write_text(json.dumps(metadata))
        with self.assertRaisesRegex(RuntimeError, 'Incompatible Steam helper'):
            validate_helper(bundle, 'linux-x86_64', self.source)

    def test_game_target_in_manifest_is_rejected(self):
        with (self.build / 'runtime-libraries.txt').open('a') as stream:
            stream.write('mp2_code|' + str(self.build / 'libmp2_code.a') + '\n')
        with self.assertRaisesRegex(RuntimeError, 'Unreviewed'):
            pack_runtime(self.source, self.build, self.runtime)

    def test_game_build_is_rejected(self):
        (self.build / 'CMakeCache.txt').write_text('MP2_PORTABILITY_CHECK:BOOL=OFF\n')
        with self.assertRaisesRegex(RuntimeError, 'ROM-free'):
            pack_runtime(self.source, self.build, self.runtime)

    def test_windows_compatibility_header_invalidates_cached_sdk(self):
        pack_runtime(self.source, self.build, self.runtime)
        with (self.source / 'src/mingw_compat.hpp').open('a') as stream:
            stream.write('\n// Changed runtime compilation input\n')
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            validate_runtime(self.source, self.runtime)

    def test_changed_recipe_and_corrupt_archive_are_rejected(self):
        pack_runtime(self.source, self.build, self.runtime)
        (self.runtime / 'libraries/rt64.a').write_bytes(b'corrupt')
        with self.assertRaisesRegex(RuntimeError, 'damaged'):
            validate_runtime(self.source, self.runtime)
        pack_runtime(self.source, self.build, self.runtime)
        with (self.source / 'dependencies.json').open('a') as stream:
            stream.write('\n')
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            validate_runtime(self.source, self.runtime)

    def test_unreviewed_metadata_and_external_archives_are_rejected(self):
        pack_runtime(self.source, self.build, self.runtime)
        path = self.runtime / 'runtime.json'
        metadata = json.loads(path.read_text())
        metadata['libraries']['../mp2_code'] = '0' * 64
        path.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(RuntimeError, 'unexpected library set'):
            validate_runtime(self.source, self.runtime)
        external = self.root / 'outside.a'
        external.write_bytes(b'external archive')
        manifest = self.build / 'runtime-libraries.txt'
        text = manifest.read_text().replace(str(self.build / 'rt64.a'), str(external))
        manifest.write_text(text)
        with self.assertRaisesRegex(RuntimeError, 'outside'):
            pack_runtime(self.source, self.build, self.runtime)


if __name__ == '__main__':
    unittest.main()
