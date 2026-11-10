"""Private build input and allowlisted standalone Steam runtime packaging."""
import base64
import hashlib
import io
import json
from pathlib import Path
import shutil
import zipfile

SDK_VERSION = '1.65'
PRIVATE_INPUT_SHA256 = '11009e5c3c1e75f44dc48f5778a212e70c05f0851001911e39b7f2bfd0082097'
HELPER_RECIPE_FILES = (
    'steam-helper/CMakeLists.txt', 'steam-helper/STEAMWORKS-NOTICE.txt',
    'LICENSE-STEAM-HELPER', 'src/netplay_steam.cpp', 'src/netplay_transport.hpp',
    'src/steam_helper_main.cpp', 'src/steam_helper_protocol.hpp',
)

def helper_recipe(source):
    digest = hashlib.sha256()
    for name in HELPER_RECIPE_FILES:
        digest.update(name.encode() + b'\0')
        digest.update((Path(source) / name).read_bytes().replace(b'\r\n', b'\n'))
    return digest.hexdigest()

def materialize_private_sdk(encoded, destination):
    """CI-only: secrets supply headers and redistributables, never public artifacts."""
    data = base64.b64decode(encoded, validate=True)
    if hashlib.sha256(data).hexdigest() != PRIVATE_INPUT_SHA256:
        raise RuntimeError('Private Steamworks SDK 1.65 build input missing or checksum mismatch')
    destination = Path(destination).resolve()
    binaries = {'redistributable_bin/win64/steam_api64.dll',
                'redistributable_bin/win64/steam_api64.lib',
                'redistributable_bin/linux64/libsteam_api.so'}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for item in archive.infolist():
            path = (destination / item.filename).resolve()
            header = item.filename.startswith('public/steam/') and item.filename.endswith('.h')
            if not path.is_relative_to(destination) or not (header or item.filename in binaries):
                raise RuntimeError('Unexpected private SDK build input')
        archive.extractall(destination)
    print('Prepared private build-only Steamworks SDK ' + SDK_VERSION)

def bundle_names(platform):
    windows = platform == 'windows-x86_64'
    if platform not in ('windows-x86_64', 'linux-x86_64'):
        raise RuntimeError('Unsupported Steam helper platform')
    return {'mp2recomp-steam-helper' + ('.exe' if windows else ''),
            'steam_api64.dll' if windows else 'libsteam_api.so',
            'LICENSE-STEAM-HELPER', 'STEAMWORKS-NOTICE.txt'}

def pack_helper(source, bundle, platform):
    from tools.dependency_pack import sha256
    bundle = Path(bundle)
    names = bundle_names(platform)
    if {p.name for p in bundle.iterdir()} != names:
        raise RuntimeError('Steam helper bundle has missing or unreviewed files')
    metadata = {'format': 1, 'protocol': 2, 'platform': platform,
                'sdk_version': SDK_VERSION, 'recipe': helper_recipe(source),
                'files': {name: sha256(bundle / name) for name in sorted(names)}}
    (bundle / 'steam.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata

def validate_helper(bundle, platform, source=None):
    from tools.dependency_pack import sha256
    bundle = Path(bundle).resolve()
    metadata = json.loads((bundle / 'steam.json').read_text())
    names = bundle_names(platform)
    if (metadata.get('format') != 1 or metadata.get('protocol') != 2 or
            metadata.get('platform') != platform or set(metadata.get('files', {})) != names):
        raise RuntimeError('Incompatible Steam helper bundle')
    if {p.name for p in bundle.iterdir()} != names | {'steam.json'}:
        raise RuntimeError('Unreviewed files in Steam helper bundle')
    if source is not None and metadata.get('recipe') != helper_recipe(source):
        raise RuntimeError('Steam helper does not match this installer release')
    for name, expected in metadata['files'].items():
        path = bundle / name
        if not path.resolve().is_relative_to(bundle) or not path.is_file() or sha256(path) != expected:
            raise RuntimeError('Steam helper missing or damaged: ' + name)
    return metadata

def install_helper(sdk, prefix, source=None):
    sdk, prefix = Path(sdk), Path(prefix)
    catalog = sdk / 'sdk.json'
    if not catalog.is_file():
        return
    metadata = json.loads(catalog.read_text())
    if not metadata.get('steam_helper'):
        return
    bundle = (sdk / metadata['steam_helper']).resolve()
    if not bundle.is_relative_to(sdk.resolve()):
        raise RuntimeError('Invalid Steam helper path')
    validate_helper(bundle, metadata['platform'], source)
    for name in bundle_names(metadata['platform']) | {'steam.json'}:
        shutil.copy2(bundle / name, prefix / name)
    (prefix / ('mp2recomp-steam-helper.exe' if metadata['platform'].startswith('windows')
               else 'mp2recomp-steam-helper')).chmod(0o755)
