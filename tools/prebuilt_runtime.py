"""Package only allowlisted, ROM-independent runtime archives."""
import hashlib
import json
from pathlib import Path
import shutil

RECIPE_FILES = (
    'dependencies.json', 'CMakeLists.txt', 'cmake/PrebuiltRuntime.cmake',
    'cmake/MinGWCompat.cmake', 'cmake/Netplay.cmake',
    'src/mingw_compat.hpp',
    'tools/build_dependency_pack.py', 'tools/prebuilt_runtime.py', 'tools/sdk_files.py',
    'tools/build_sdl.py', 'tools/dependency-environment.yml',
    'tools/steam_sdk.py', 'steam-helper/CMakeLists.txt',
    'steam-helper/STEAMWORKS-NOTICE.txt', 'LICENSE-STEAM-HELPER',
    'src/netplay_steam.cpp', 'src/netplay_transport.hpp',
    'src/steam_helper_main.cpp', 'src/steam_helper_protocol.hpp',
    'src/netplay_steam_proxy.cpp', 'src/steam_helper_process.hpp',
    'src/netplay_steam_social.hpp',
)


def recipe_digest(source):
    digest = hashlib.sha256()
    for name in RECIPE_FILES:
        digest.update(name.encode() + b'\0')
        digest.update((Path(source) / name).read_bytes().replace(b'\r\n', b'\n'))
    return digest.hexdigest()


def runtime_targets(source):
    # The CMake list is the single reviewed publication allowlist.
    import re
    text = (Path(source) / 'cmake/PrebuiltRuntime.cmake').read_text()
    match = re.search(r'set\(MP2_RUNTIME_TARGETS\s+([^)]*)\)', text)
    if not match:
        raise RuntimeError('Missing runtime publication allowlist')
    return match[1].split()


def pack_runtime(source, build, destination):
    source, build, destination = map(Path, (source, build, destination))
    cache = (build / 'CMakeCache.txt').read_text()
    if 'MP2_PORTABILITY_CHECK:BOOL=ON' not in cache:
        raise RuntimeError('Runtime archives must come from a ROM-free portability build')
    targets = runtime_targets(source)
    entries = {}
    for line in (build / 'runtime-libraries.txt').read_text().splitlines():
        name, path = line.split('|', 1)
        if name not in targets or name in entries:
            raise RuntimeError('Unreviewed or duplicate runtime target: ' + name)
        entries[name] = Path(path)
    if set(entries) != set(targets):
        raise RuntimeError('Incomplete runtime publication manifest')
    # Never sweep the build directory: it can also contain game code and stubs.
    if destination.exists():
        shutil.rmtree(destination)
    libraries = destination / 'libraries'
    libraries.mkdir(parents=True)
    from tools.dependency_pack import sha256
    hashes = {}
    for name, archive in entries.items():
        if not archive.resolve().is_relative_to(build.resolve()):
            raise RuntimeError('Runtime archive is outside the portability build')
        target = libraries / (name + '.a')
        shutil.copy2(archive, target)
        hashes[name] = sha256(target)
    metadata = json.loads((build / 'runtime-toolchain.json').read_text())
    metadata.update(format=1, recipe=recipe_digest(source), libraries=hashes)
    (destination / 'runtime.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata


def validate_runtime(source, runtime):
    runtime = Path(runtime).resolve()
    metadata = json.loads((runtime / 'runtime.json').read_text())
    if metadata.get('format') != 1 or metadata.get('recipe') != recipe_digest(source):
        raise RuntimeError('Prebuilt runtime does not match this installer. Rebuild the SDK for this release.')
    if set(metadata.get('libraries', {})) != set(runtime_targets(source)):
        raise RuntimeError('Prebuilt runtime has an unexpected library set')
    from tools.dependency_pack import sha256
    for name, expected in metadata['libraries'].items():
        archive = runtime / 'libraries' / (name + '.a')
        if not archive.resolve().is_relative_to(runtime) or not archive.is_file() or sha256(archive) != expected:
            raise RuntimeError('Prebuilt runtime is missing or damaged: ' + name)
    return metadata
