"""Build and verify the SDK's Linux SDL, including both display backends."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def build_sdl(prefix,jobs='3'):
    prefix=Path(prefix).resolve()
    path=os.pathsep.join(str(prefix/p) for p in ('lib/pkgconfig','share/pkgconfig'))
    env=dict(os.environ,PATH=str(prefix/'bin')+os.pathsep+os.environ.get('PATH',''),
        LD_LIBRARY_PATH=str(prefix/'lib'),PKG_CONFIG_PATH=path,PKG_CONFIG_LIBDIR=path)
    cmake=str(prefix/'bin/cmake');build=ROOT/'build/sdk-sdl'
    flags='--gcc-toolchain='+str(prefix)+' --sysroot='+str(prefix/'x86_64-conda-linux-gnu/sysroot')
    subprocess.run([cmake,'--fresh','-S',str(ROOT/'SDL'),'-B',str(build),'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        '-DCMAKE_C_COMPILER='+str(prefix/'bin/clang'),'-DCMAKE_CXX_COMPILER='+str(prefix/'bin/clang++'),
        '-DCMAKE_C_FLAGS='+flags,'-DCMAKE_CXX_FLAGS='+flags,
        '-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCMAKE_INSTALL_LIBDIR=lib',
        '-DCMAKE_PREFIX_PATH='+str(prefix),'-DCMAKE_FIND_ROOT_PATH='+str(prefix),
        '-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY','-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY',
        '-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY','-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER','-DPKG_CONFIG_EXECUTABLE='+str(prefix/'bin/pkg-config.bin'),
        '-DSDL_SHARED=ON','-DSDL_STATIC=OFF','-DSDL_TEST=OFF','-DSDL_TESTS=OFF',
        '-DSDL_WAYLAND=ON','-DSDL_WAYLAND_SHARED=ON','-DSDL_WAYLAND_LIBDECOR=ON',
        '-DSDL_WAYLAND_LIBDECOR_SHARED=ON','-DSDL_X11=ON','-DSDL_VULKAN=ON'],env=env,check=True)
    subprocess.run([cmake,'--build',str(build),'-j',str(jobs)],env=env,check=True)
    subprocess.run([cmake,'--install',str(build)],env=env,check=True)
    # The library must remain usable after SDK extraction and game installation.
    library=prefix/'lib/libSDL2-2.0.so.0'
    subprocess.run([str(prefix/'bin/patchelf'),'--set-rpath','$ORIGIN',str(library.resolve())],env=env,check=True)
    pc=prefix/'lib/pkgconfig/sdl2.pc'
    lines=pc.read_text().splitlines()
    pc.write_text('\n'.join('prefix=${pcfiledir}/../..' if line.startswith('prefix=') else line for line in lines)+'\n')
    license_dir=prefix/'share/licenses/SDL2';license_dir.mkdir(parents=True,exist_ok=True)
    shutil.copy2(ROOT/'SDL/LICENSE.txt',license_dir/'LICENSE.txt')
    probe='''import ctypes,json,sys
sdl=ctypes.CDLL(sys.argv[1]);sdl.SDL_GetVideoDriver.restype=ctypes.c_char_p
drivers=[sdl.SDL_GetVideoDriver(i).decode() for i in range(sdl.SDL_GetNumVideoDrivers())]
assert {'wayland','x11'}.issubset(drivers),drivers
print(json.dumps(drivers))
'''
    output=subprocess.check_output([str(prefix/'bin/python'),'-c',probe,str(library)],env=env,text=True)
    drivers=json.loads(output)
    print('SDK SDL video drivers: '+', '.join(drivers),flush=True)
    return drivers

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',type=Path,required=True);parser.add_argument('--jobs',default='3')
    args=parser.parse_args();build_sdl(args.prefix,args.jobs)
