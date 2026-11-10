#!/usr/bin/env python3
"""Build a redistributable SDK from open-source tools; never reads a game ROM."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.dependency_pack import sha256
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--toolchain',type=Path,required=True);p.add_argument('--python',type=Path,required=True)
    p.add_argument('--platform',required=True);p.add_argument('--output',type=Path,default=ROOT/'dist');p.add_argument('--jobs',default='3');args=p.parse_args()
    args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    stage=args.output/'sdk-stage'
    if stage.exists():shutil.rmtree(stage)
    stage.mkdir()
    def copytree(source,target):
        shutil.copytree(source,target,dirs_exist_ok=True,symlinks=True,ignore=shutil.ignore_patterns('.git','.github','.venv','__pycache__'))
    windows=args.platform.startswith('windows')
    # Linux's conda-pack contains relocation records and third-party licenses.
    if windows:
        copytree(args.toolchain,stage/'toolchain')
        python_root=args.python.parent
        copytree(python_root,stage/'python')
        python=stage/'python/python.exe'
    else:
        packed=args.output/'conda-environment.tar.gz'
        subprocess.run([str(args.python),'-m','conda_pack.cli','-p',str(args.toolchain),'-o',str(packed),'--force'],check=True)
        (stage/'toolchain').mkdir(exist_ok=True)
        with tarfile.open(packed) as archive:archive.extractall(stage/'toolchain',filter='data')
        packed.unlink()
        python=stage/'toolchain/bin/python'
    sources=stage/'sources';sources.mkdir(exist_ok=True)
    for name in ('N64Recomp','runtime','rt64','directx-headers','libdatachannel'):copytree(ROOT/name,sources/name)
    decomp=sources/'decomp';decomp.mkdir(exist_ok=True)
    for name in ('marioparty2.yaml','symbol_addrs.txt'):shutil.copy2(ROOT/'decomp'/name,decomp/name)
    # Preserve license files and package metadata; only game metadata is taken
    # from the decomp project. No decompiled C, assembly, assets or ROMs are packed.
    suffix='.exe' if windows else ''
    tools={'python':str(python.relative_to(stage)),
        'cmake':'toolchain/bin/cmake'+suffix,'ninja':'toolchain/bin/ninja'+suffix,
        'cc':'toolchain/bin/clang'+suffix,'cxx':'toolchain/bin/clang++'+suffix,
        'recompiler':'tools/N64Recomp'+suffix,'rsp':'tools/RSPRecomp'+suffix}
    data={'format':1,'platform':args.platform,'tools':tools,'sources':'sources'}
    if not windows:
        data['relocate']='toolchain/bin/conda-unpack'
        tools['patchelf']='toolchain/bin/patchelf'
    (stage/'sdk.json').write_text(json.dumps(data,indent=2))
    from tools.dependency_pack import sdk_environment
    env=sdk_environment(stage,data)
    # Use the original prefix for compilation; compiled tools find their shared
    # libraries through sdk_environment after installation.
    env['PATH']=str(args.toolchain/'bin')+os.pathsep+env.get('PATH','')
    env['CMAKE_PREFIX_PATH']=str(args.toolchain)
    env['CONDA_PREFIX']=str(args.toolchain)
    env['PKG_CONFIG_PATH']=os.pathsep.join([str(args.toolchain/'lib/pkgconfig'),str(args.toolchain/'share/pkgconfig')])
    if not windows:env['PKG_CONFIG']=str(args.toolchain/'bin/pkg-config.bin')
    env['PKG_CONFIG_LIBDIR']=env['PKG_CONFIG_PATH']
    if not windows:env['LD_LIBRARY_PATH']=str(args.toolchain/'lib')
    build=ROOT/'build/sdk-tools'
    compiler_flags=[]
    if windows:
        compiler_flags=['-DCMAKE_CXX_FLAGS=-include cstdlib']
    else:
        sysroot=args.toolchain/'x86_64-conda-linux-gnu/sysroot'
        flags='--gcc-toolchain='+str(args.toolchain)+' --sysroot='+str(sysroot)
        compiler_flags=['-DCMAKE_C_FLAGS='+flags,'-DCMAKE_CXX_FLAGS='+flags]
    cmake=str(args.toolchain/('bin/cmake'+suffix))
    subprocess.run([cmake,'--fresh','-S',str(ROOT/'N64Recomp'),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER='+str(args.toolchain/('bin/clang'+suffix)),'-DCMAKE_CXX_COMPILER='+str(args.toolchain/('bin/clang++'+suffix)),*compiler_flags],env=env,check=True)
    subprocess.run([str(args.toolchain/('bin/cmake'+suffix)),'--build',str(build),'--target','N64RecompCLI','RSPRecomp','-j',args.jobs,'--','-k','0'],env=env,check=True)
    (stage/'tools').mkdir(exist_ok=True)
    for tool in ('N64Recomp','RSPRecomp'):
        destination=stage/'tools'/(tool+suffix);shutil.copy2(build/(tool+suffix),destination)
        if not windows:subprocess.run([str(args.toolchain/'bin/patchelf'),'--set-rpath','$ORIGIN/../toolchain/lib',str(destination)],env=env,check=True)
    # A dependency pack is publishable only after compiling and running the
    # complete host runtime with synthetic (non-game) link fixtures.
    check=ROOT/'build/sdk-platform-check'
    subprocess.run([cmake,'--fresh','-S',str(ROOT),'-B',str(check),'-G','Ninja','-DMP2_PORTABILITY_CHECK=ON',
        '-DCMAKE_POLICY_VERSION_MINIMUM=3.5','-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_COMPILER='+str(args.toolchain/('bin/clang'+suffix)),'-DCMAKE_CXX_COMPILER='+str(args.toolchain/('bin/clang++'+suffix)),*compiler_flags],env=env,check=True)
    subprocess.run([cmake,'--build',str(check),'--target','mp2recomp','netplay_test','-j',args.jobs,'--','-k','0'],env=env,check=True)
    subprocess.run([str(check/('mp2recomp'+suffix)),'--self-test'],env=env,check=True)
    subprocess.run([str(check/('netplay_test'+suffix))],env=env,check=True)
    if not windows:
        from install import install_linux_runtime_data
        install_linux_runtime_data(stage,check)
        probe_env=dict(env,FONTCONFIG_FILE='/missing/build-host/fonts.conf',
            FONTCONFIG_PATH='/missing/build-host/fonts',XKB_CONFIG_ROOT='/missing/build-host/xkb',GDK_BACKEND='x11')
        subprocess.run(['xvfb-run','-a',str(check/'mp2recomp'),'--runtime-self-test'],env=probe_env,check=True)
    for path in stage.rglob('*'):
        if path.is_file() and path.suffix.lower() in ('.z64','.v64','.n64'):raise SystemExit('ROM-like file rejected: '+str(path))
    name='mp2recomp-sdk-'+args.platform
    if windows:archive=Path(shutil.make_archive(str(args.output/name),'zip',stage))
    else:
        archive=args.output/(name+'.tar.gz')
        with tarfile.open(archive,'w:gz',compresslevel=3) as tar:
            for item in sorted(stage.iterdir()):tar.add(item,arcname=item.name)
    (args.output/(name+'.json')).write_text(json.dumps({'platform':args.platform,'filename':archive.name,'sha256':sha256(archive)},indent=2))
    print(archive)
if __name__=='__main__':main()
