#!/usr/bin/env python3
"""Freeze the Qt installer, Python runtime and reviewed source for this platform."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from install import source_files
from tools.dependency_pack import platform_key,sha256
from tools.prebuilt_runtime import recipe_digest

def main():
    p=argparse.ArgumentParser();p.add_argument('--metadata',type=Path,required=True)
    p.add_argument('--bundle-sdk',action='store_true',help='Include the matching offline SDK beside the installer');p.add_argument('--tag',required=True);p.add_argument('--repository',default='ajcohn716/mp2Recomp');args=p.parse_args()
    subprocess.run([sys.executable,str(ROOT/'tools/audit_repository.py')],cwd=ROOT,check=True)
    source=ROOT/'build/installer-source'
    if source.exists():shutil.rmtree(source)
    source.mkdir(parents=True)
    for name in source_files():
        data=subprocess.check_output(['git','show',':'+name],cwd=ROOT)
        target=source/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    packs={}
    for item in args.metadata.glob('mp2recomp-sdk-*.json'):
        entry=json.loads(item.read_text())
        if entry.get('runtime_recipe')!=recipe_digest(source):
            raise SystemExit('SDK runtime recipe mismatch: '+item.name+'. Rebuild SDKs for this release.')
        entry['url']=f'https://github.com/{args.repository}/releases/download/{args.tag}/{entry["filename"]}'
        packs[entry.pop('platform')]=entry
    if platform_key() not in packs:raise SystemExit('Missing SDK metadata for '+platform_key())
    (source/'release-catalog.json').write_text(json.dumps({'format':1,'packs':packs},indent=2))
    dist=ROOT/'dist/installer';dist.mkdir(parents=True,exist_ok=True)
    subprocess.run([sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onedir','--name','mp2recomp-installer',
        '--specpath',str(ROOT/'build/installer-spec'),'--workpath',str(ROOT/'build/installer-freeze'),
        '--distpath',str(dist),'--add-data',str(source)+os.pathsep+'source',
        '--hidden-import','tools.installer_gui',str(ROOT/'install.py')],cwd=ROOT,check=True)
    folder=dist/'mp2recomp-installer'
    for name in ('LICENSE','THIRD_PARTY_NOTICES.md','README.md'):
        shutil.copy2(source/name,folder/name)
    if os.name=='nt':
        required={'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll'}
        missing=required-{p.name.lower() for p in folder.rglob('*.dll')}
        if missing:raise SystemExit('Missing Qt/DXC runtime libraries: '+', '.join(sorted(missing)))
    if args.bundle_sdk:
        entry=packs[platform_key()];sdk=args.metadata/entry['filename']
        if sha256(sdk)!=entry['sha256']:raise SystemExit('SDK checksum does not match metadata')
        shutil.copy2(sdk,folder/entry['filename'])
    name='mp2recomp-installer-'+platform_key()
    if os.name=='nt':archive=Path(shutil.make_archive(str(ROOT/'dist'/name),'zip',folder))
    else:
        archive=ROOT/'dist'/(name+'.tar.gz')
        with tarfile.open(archive,'w:gz') as tar:
            for item in sorted(folder.iterdir()):tar.add(item,arcname=item.name)
    archive.with_name(archive.name+'.sha256').write_text(sha256(archive)+'  '+archive.name+'\n')
    print(archive)
if __name__=='__main__':main()
