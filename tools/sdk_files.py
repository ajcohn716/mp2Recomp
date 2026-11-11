"""SDK file operations that do not depend on Windows' long-path policy."""
import ntpath
import os
from pathlib import Path
import shutil


def windows_extended_path(path):
    """Return an absolute Win32 extended path, including for UNC shares."""
    path = ntpath.abspath(os.fspath(path))
    if path.startswith('\\\\?\\'):
        return path
    if path.startswith('\\\\'):
        return '\\\\?\\UNC\\' + path[2:]
    return '\\\\?\\' + path


def filesystem_path(path):
    # Keep these prefixes at file I/O boundaries. Build tools and their command
    # lines still receive ordinary paths, since not all tools accept \\?\ paths.
    path = Path(path).expanduser().resolve()
    return Path(windows_extended_path(path)) if os.name == 'nt' else path


def sdk_source_ignore(directory, names):
    ignored = {'.git', '.github', '.venv', '__pycache__'} & set(names)
    if Path(directory).name == 'spirv-cross':
        # Upstream regression inputs and expected outputs are not used by RT64.
        # Keep compiler sources, CMake files, licenses and tests-other intact.
        ignored.update(name for name in names
                       if name == 'reference' or name.startswith('shaders'))
    return ignored


def copy_sdk_sources(source, destination):
    return shutil.copytree(filesystem_path(source), filesystem_path(destination),
                           dirs_exist_ok=True, symlinks=False,
                           ignore=sdk_source_ignore)
