"""Verified, app-local dependency packs shared by the GUI and CLI."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import tarfile
import tempfile
import urllib.request
import zipfile

FORMAT=1

def platform_key():
    if platform.machine().lower() not in ('amd64','x86_64'):raise RuntimeError('This release supports x86-64 computers only.')
    return ('windows' if os.name=='nt' else 'linux')+'-x86_64'

def load_sdk(root):
    root=Path(root).resolve();data=json.loads((root/'sdk.json').read_text())
    if data.get('format')!=FORMAT or data.get('platform')!=platform_key():raise RuntimeError('Dependency pack is incompatible with this computer.')
    for key in ('python','cmake','ninja','cc','cxx','recompiler','rsp'):
        path=(root/data['tools'][key]).resolve()
        if not path.is_relative_to(root) or not path.is_file():raise RuntimeError('Dependency pack is incomplete: '+key)
    return data

def extract_archive(archive,dest):
    dest=Path(dest).resolve()
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as z:
            for item in z.infolist():
                path=(dest/item.filename).resolve()
                if not path.is_relative_to(dest) or (item.external_attr>>16)&0o170000==0o120000:
                    raise RuntimeError('Unsafe path in dependency pack')
            z.extractall(dest)
    else:
        with tarfile.open(archive,'r:*') as tar:tar.extractall(dest,filter='data')

def sha256(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        while chunk:=f.read(1024*1024):h.update(chunk)
    return h.hexdigest()

def ensure_sdk(cache,source_root,run,status,check_cancelled,archive=None,sdk_dir=None):
    if sdk_dir:
        root=Path(sdk_dir).expanduser().resolve();load_sdk(root);return root
    from install import lock_file
    directory=Path(cache)/'dependencies';directory.mkdir(parents=True,exist_ok=True)
    with (directory/'.prepare.lock').open('a') as lock:
        try:lock_file(lock)
        except OSError:raise RuntimeError('Another installer is preparing dependency packs. Try again when it finishes.')
        return _ensure_sdk(cache,source_root,run,status,check_cancelled,archive,sdk_dir)

def _ensure_sdk(cache,source_root,run,status,check_cancelled,archive=None,sdk_dir=None):
    if sdk_dir:
        root=Path(sdk_dir).expanduser().resolve();load_sdk(root);return root
    catalog_path=Path(source_root)/'release-catalog.json'
    if not catalog_path.is_file():
        raise RuntimeError('No dependency catalog in this source checkout. Use a release installer, --sdk-dir, or --source-build with system build tools.')
    catalog=json.loads(catalog_path.read_text());entry=catalog['packs'][platform_key()]
    expected=entry['sha256']
    root=Path(cache)/'dependencies'/expected
    if (root/'.ready').exists():load_sdk(root);return root
    parent=root.parent;parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pack-',dir=parent) as temporary:
        temporary=Path(temporary)
        if archive:
            downloaded=Path(archive).expanduser().resolve()
        else:
            # An offline pack may be distributed beside the packaged installer.
            import sys
            adjacent=Path(sys.executable).parent/entry['filename']
            if not adjacent.is_file():adjacent=Path(source_root)/entry['filename']
            if adjacent.is_file():downloaded=adjacent
            else:
                status('Downloading verified build tools')
                url=entry['url']
                if not url.startswith('https://'):raise RuntimeError('Dependency downloads must use HTTPS')
                downloaded=temporary/'download'
                with urllib.request.urlopen(url,timeout=60) as response,downloaded.open('wb') as out:
                    while chunk:=response.read(1024*1024):check_cancelled();out.write(chunk)
        status('Verifying dependency pack')
        if sha256(downloaded)!=expected:raise RuntimeError('Dependency checksum mismatch. Download the pack matching this installer release.')
        check_cancelled();unpacked=temporary/'sdk';unpacked.mkdir()
        extract_archive(downloaded,unpacked);data=load_sdk(unpacked)
        # Publish a complete directory only. Concurrent installs can share a pack.
        try:os.rename(unpacked,root)
        except OSError:
            if not (root/'sdk.json').is_file():raise
        if data.get('relocate'):
            env=sdk_environment(root,data)
            run([str(root/data['tools']['python']),str(root/data['relocate'])],root,env)
    (root/'.ready').write_text('1\n')
    return root

def sdk_environment(root,data=None):
    root=Path(root).resolve();data=data or load_sdk(root);prefix=root/'toolchain'
    env=dict(os.environ)
    env['PATH']=os.pathsep.join([str(root/'tools'),str(prefix/'bin'),env.get('PATH','')])
    env['CMAKE_PREFIX_PATH']=str(prefix)
    env['CONDA_PREFIX']=str(prefix)
    env['PKG_CONFIG_PATH']=str(prefix/'lib/pkgconfig')
    if os.name!='nt':env['LD_LIBRARY_PATH']=str(prefix/'lib')+os.pathsep+env.get('LD_LIBRARY_PATH','')
    return env
