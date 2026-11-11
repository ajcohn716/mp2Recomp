#!/usr/bin/env python3
"""Verify and repackage signed Windows payloads before publishing checksums."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.dependency_pack import extract_archive, load_sdk, sha256
from tools.prebuilt_runtime import recipe_digest, validate_runtime
from tools.steam_sdk import bundle_names, helper_recipe, pack_helper, validate_helper

SIGNING_VARIABLES = (
    'AZURE_SIGNING_CLIENT_ID', 'AZURE_SIGNING_TENANT_ID',
    'AZURE_SIGNING_SUBSCRIPTION_ID', 'AZURE_SIGNING_ENDPOINT',
    'AZURE_SIGNING_ACCOUNT', 'AZURE_SIGNING_CERTIFICATE_PROFILE',
)


def check_configuration():
    missing = [name for name in SIGNING_VARIABLES if not os.environ.get(name, '').strip()]
    if missing:
        raise RuntimeError('Configure release-signing environment variables: ' + ', '.join(missing)
                           + '. See docs/windows-signing.md; unsigned release fallback is disabled.')


def verify_binaries(root):
    if sys.platform != 'win32':
        raise RuntimeError('Windows signature verification requires a Windows runner')
    subprocess.run(['pwsh', '-NoProfile', '-File', str(ROOT / 'tools/windows_signing.ps1'),
                    '-Mode', 'Verify', '-Root', str(Path(root).resolve())], check=True)


def sdk_metadata(path):
    metadata = json.loads(Path(path).read_text())
    if (metadata.get('platform') != 'windows-x86_64' or
            metadata.get('filename') != 'mp2recomp-sdk-windows-x86_64.zip' or
            metadata.get('runtime_recipe') != recipe_digest(ROOT) or
            metadata.get('steam_helper_recipe') != helper_recipe(ROOT)):
        raise RuntimeError('Windows SDK metadata does not match this release; rebuild the SDK')
    return metadata


def finalize_sdk(stage, metadata_path):
    stage, metadata_path = Path(stage), Path(metadata_path)
    metadata = sdk_metadata(metadata_path)
    verify_binaries(stage)
    validate_runtime(ROOT, stage / 'runtime')
    # Authenticode changes the helper bytes. Refresh its allowlisted hashes only
    # after signature verification, before computing the outer SDK checksum.
    helper = stage / 'steam'
    previous = json.loads((helper / 'steam.json').read_text())
    if (previous.get('recipe') != helper_recipe(ROOT) or
            previous.get('protocol') != 2 or previous.get('platform') != 'windows-x86_64' or
            set(previous.get('files', {})) != bundle_names('windows-x86_64') or
            {p.name for p in helper.iterdir()} != bundle_names('windows-x86_64') | {'steam.json'}):
        raise RuntimeError('Unexpected Steam helper manifest before signing')
    (helper / 'steam.json').unlink()
    pack_helper(ROOT, helper, 'windows-x86_64')
    validate_helper(helper, 'windows-x86_64', ROOT)
    load_sdk(stage)
    archive = Path(shutil.make_archive(
        str(metadata_path.parent / 'mp2recomp-sdk-windows-x86_64'), 'zip', stage))
    metadata.update(sha256=sha256(archive), windows_signing=1)
    metadata_path.write_text(json.dumps(metadata, indent=2) + '\n')
    print('Packaged signed SDK: ' + str(archive))


def verify_sdk(archive, metadata_path):
    metadata = sdk_metadata(metadata_path)
    if metadata.get('windows_signing') != 1:
        raise RuntimeError('SDK predates Windows signing; rebuild instead of reusing this SDK run')
    if sha256(archive) != metadata['sha256']:
        raise RuntimeError('Windows SDK checksum mismatch')
    # Never trust only the metadata marker, including when reusing old CI runs.
    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='verify-sdk-', dir=ROOT / 'build') as temporary:
        stage = Path(temporary)
        extract_archive(archive, stage)
        load_sdk(stage)
        validate_helper(stage / 'steam', 'windows-x86_64', ROOT)
        validate_runtime(ROOT, stage / 'runtime')
        verify_binaries(stage)


def finalize_installer(folder, archive):
    folder, archive = Path(folder), Path(archive)
    verify_binaries(folder)
    if not (folder / 'mp2recomp-installer.exe').is_file():
        raise RuntimeError('Missing Windows installer executable')
    if archive.suffix != '.zip':
        raise RuntimeError('Windows installer archive must be a ZIP')
    shutil.make_archive(str(archive.with_suffix('')), 'zip', folder)
    archive.with_name(archive.name + '.sha256').write_text(
        sha256(archive) + '  ' + archive.name + '\n')
    print('Packaged signed installer: ' + str(archive))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('check-config')
    sdk = commands.add_parser('finalize-sdk')
    sdk.add_argument('--stage', type=Path, required=True)
    sdk.add_argument('--metadata', type=Path, required=True)
    verify = commands.add_parser('verify-sdk')
    verify.add_argument('--archive', type=Path, required=True)
    verify.add_argument('--metadata', type=Path, required=True)
    installer = commands.add_parser('finalize-installer')
    installer.add_argument('--folder', type=Path, required=True)
    installer.add_argument('--archive', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'check-config':
        check_configuration()
    elif args.command == 'finalize-sdk':
        finalize_sdk(args.stage, args.metadata)
    elif args.command == 'verify-sdk':
        verify_sdk(args.archive, args.metadata)
    else:
        finalize_installer(args.folder, args.archive)


if __name__ == '__main__':
    main()
