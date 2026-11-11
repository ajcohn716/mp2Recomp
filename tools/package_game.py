#!/usr/bin/env python3
"""Build and package a portable game on a machine with the supported ROM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from install import InstallError, Installer
from tools.dependency_pack import ensure_sdk, platform_key, sdk_environment, sha256
from tools.windows_release import verify_binaries


def prepare_sdk(args, engine):
    if args.sdk_dir:
        return args.sdk_dir.resolve()

    metadata = json.loads(args.sdk_metadata.read_text())
    platform = platform_key()
    expected = metadata.get('sha256', '')
    if metadata.get('platform') != platform or not re.fullmatch(r'[a-f0-9]{64}', expected):
        raise InstallError('The SDK metadata is invalid or belongs to a different platform.')
    with tempfile.TemporaryDirectory(prefix='catalog-', dir=args.build_dir) as directory:
        catalog = Path(directory)
        (catalog / 'release-catalog.json').write_text(json.dumps({'packs': {platform: metadata}}))
        # The installer already checks the archive, extracts it under its hash
        # and relocates Linux tools before making that cache available.
        return ensure_sdk(args.build_dir, catalog, engine.run, engine.status,
                          engine.check_cancelled, archive=args.sdk_archive)


def payload_files(root, windows):
    binary = 'mp2recomp.exe' if windows else 'mp2recomp'
    required = {binary, 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'README.txt'}
    if not windows:
        required.add('play')
    for name in required:
        if not (root / name).is_file():
            raise InstallError('Missing game package file: ' + name)

    allowed_root = required | {'LICENSE-STEAM-HELPER', 'STEAMWORKS-NOTICE.txt'}
    if windows:
        allowed_root.add('mp2recomp-steam-helper.exe')
    else:
        allowed_root.update({'mp2recomp-steam-helper', 'libsteam_api.so'})

    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        parts = relative.parts
        name = path.name.lower()
        if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
            raise InstallError('Linked files are not supported in game packages: ' + str(relative))
        if path.is_dir():
            continue
        if not path.is_file():
            raise InstallError('Unexpected file type in game package: ' + str(relative))
        allowed = len(parts) == 1 and path.name in allowed_root
        if windows and len(parts) == 1:
            allowed |= name.endswith('.dll')
        if not windows:
            allowed |= parts[0] == 'lib' and '.so' in name
            allowed |= parts[:2] in {('etc', 'fonts'), ('share', 'X11')}
        if not allowed or path.suffix.lower() in {'.z64', '.v64', '.n64', '.rom', '.c', '.cpp', '.h', '.hpp'}:
            raise InstallError('Unexpected file in game package: ' + str(relative))
        # Also reject a ROM copied under a misleading runtime filename.
        with path.open('rb') as stream:
            if stream.read(4) in {b'\x80\x37\x12\x40', b'\x37\x80\x40\x12', b'\x40\x12\x37\x80'}:
                raise InstallError('ROM contents in game package: ' + str(relative))
        yield path


def trim_windows_libraries(stage, sdk):
    """Keep the game/helper imports and the shader compiler loaded at runtime."""
    tool = sdk / 'toolchain/bin/llvm-objdump.exe'
    env = sdk_environment(sdk)
    libraries = {path.name.lower(): path for path in stage.glob('*.dll')}
    roots = [stage / name for name in ('mp2recomp.exe', 'mp2recomp-steam-helper.exe',
                                      'dxcompiler.dll', 'dxil.dll') if (stage / name).is_file()]
    seen = set()
    while roots:
        binary = roots.pop()
        if binary in seen:
            continue
        seen.add(binary)
        headers = subprocess.check_output([str(tool), '-p', str(binary)], env=env, text=True)
        for name in re.findall(r'(?im)^\s*DLL Name:\s*(\S+)', headers):
            library = libraries.get(name.lower())
            if library is not None:
                roots.append(library)
    for library in set(libraries.values()) - seen:
        library.unlink()


def stage_game(args):
    output = args.output.resolve()
    args.build_dir = args.build_dir.resolve()
    if output.exists():
        raise InstallError('Choose a new output directory for the game package.')
    if output == args.build_dir or args.build_dir.is_relative_to(output) or output.is_relative_to(args.build_dir):
        raise InstallError('Keep the game package and the private build directory separate.')
    if args.build_dir.is_relative_to(ROOT) or output.is_relative_to(ROOT):
        raise InstallError('Choose output and build directories outside the source checkout.')
    args.build_dir.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)

    windows = os.name == 'nt'
    crt = []
    if windows:
        if args.vc_runtime_dir is None:
            raise InstallError('Provide the x64 Visual C++ redistributable DLL directory with --vc-runtime-dir.')
        crt = [args.vc_runtime_dir / name for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')]
        missing = [path.name for path in crt if not path.is_file()]
        if missing:
            raise InstallError('Missing Visual C++ runtime libraries: ' + ', '.join(missing))

    engine = Installer()
    sdk = prepare_sdk(args, engine)
    sdk_key = hashlib.sha256(os.fsencode(sdk)).hexdigest()[:12]
    with tempfile.TemporaryDirectory(prefix='game-', dir=output.parent) as directory:
        stage = Path(directory) / 'package'
        engine.install(args.rom, stage, build_dir=args.build_dir / sdk_key, jobs=args.jobs,
                       desktop=False, sdk_dir=sdk, portable=True)
        for library in crt:
            shutil.copy2(library, stage / library.name)
        if windows:
            trim_windows_libraries(stage, sdk)
        (stage / '.install.lock').unlink()
        # This manifest is used to validate SDK inputs. The game loads the
        # helper directly and the package may sign the helper again.
        (stage / 'steam.json').unlink(missing_ok=True)
        launcher = 'mp2recomp.exe' if windows else './play'
        (stage / 'README.txt').write_text(
            f'Mario Party 2 Recompiled\n\nOpen {launcher} and select your own Mario Party 2 USA ROM.\n'
            'The game accepts .z64, .v64 and .n64 files and stores a local copy.\n'
            'Keep the files together. Your saves and settings are in userdata/.\n'
            'To upgrade, copy userdata/ from your previous installation.\n', encoding='utf-8')
        list(payload_files(stage, windows))
        stage.rename(output)
    print('Game staged at ' + str(output))


def archive_game(stage, output, *, unsigned_test=False, unsigned_release=False):
    stage, output = stage.resolve(), output.resolve()
    windows = os.name == 'nt'
    if output.is_relative_to(stage):
        raise InstallError('Choose an archive output directory outside the game folder.')
    if (unsigned_test or unsigned_release) and not windows:
        raise InstallError('Unsigned archive options apply to Windows builds only.')
    files = list(payload_files(stage, windows))
    if windows and not (unsigned_test or unsigned_release):
        verify_binaries(stage)
    output.mkdir(parents=True, exist_ok=True)
    label = 'mp2recomp-game-' + platform_key()
    if unsigned_test:
        label += '-unsigned-test'
    name = label + ('.zip' if windows else '.tar.gz')
    with tempfile.TemporaryDirectory(prefix='package-', dir=output) as directory:
        archive = Path(directory) / name
        if windows:
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=3) as packed:
                for path in files:
                    packed.write(path, Path(label) / path.relative_to(stage))
        else:
            with tarfile.open(archive, 'w:gz', compresslevel=3) as packed:
                for path in files:
                    packed.add(path, arcname=str(Path(label) / path.relative_to(stage)), recursive=False)
        checksum = archive.with_name(name + '.sha256')
        checksum.write_text(sha256(archive) + '  ' + name + '\n', encoding='utf-8')
        archive.replace(output / name)
        checksum.replace(output / checksum.name)
    return output / name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    stage = commands.add_parser('stage', help='Build locally and stage only the files needed to play')
    stage.add_argument('--rom', type=Path, required=True)
    stage.add_argument('--build-dir', type=Path, required=True, help='Private directory for the ROM and generated build files')
    stage.add_argument('--output', type=Path, required=True, help='New directory for the portable game')
    stage.add_argument('--jobs', type=int, default=4)
    sdk = stage.add_mutually_exclusive_group(required=True)
    sdk.add_argument('--sdk-dir', type=Path, help='Extracted and relocated SDK')
    sdk.add_argument('--sdk-archive', type=Path)
    stage.add_argument('--sdk-metadata', type=Path)
    stage.add_argument('--vc-runtime-dir', type=Path, help='Windows x64 Visual C++ redistributable DLLs')
    archive = commands.add_parser('archive', help='Archive a staged game and write its SHA-256 checksum')
    archive.add_argument('--stage', type=Path, required=True)
    archive.add_argument('--output', type=Path, required=True)
    signing = archive.add_mutually_exclusive_group()
    signing.add_argument('--unsigned-test', action='store_true', help='Create a clearly named unsigned Windows test ZIP')
    signing.add_argument('--unsigned-release', action='store_true', help='Create a Windows release ZIP without signature verification')
    args = parser.parse_args()
    if args.command == 'stage':
        if bool(args.sdk_archive) != bool(args.sdk_metadata):
            parser.error('--sdk-archive and --sdk-metadata must be used together')
        if args.jobs < 1:
            parser.error('--jobs must be positive')
        stage_game(args)
    else:
        print(archive_game(args.stage, args.output, unsigned_test=args.unsigned_test,
                           unsigned_release=args.unsigned_release))


if __name__ == '__main__':
    main()
