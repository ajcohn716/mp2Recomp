#!/usr/bin/env python3
"""ROM-driven installer for Linux and Windows. Game code is generated locally."""
import argparse
import contextlib
try:
    import fcntl
except ImportError:
    fcntl = None
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import threading

ROOT = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
if getattr(sys,"frozen",False):ROOT = ROOT/"source"
ROM_NAME = 'Mario Party 2 (USA).z64'
ROM_SHA1 = '166eda1c05670d337e2c3f15a5db528ae1e5d6e3'
ROM_SIZE = 32 * 1024 * 1024

class InstallError(Exception):
    pass

class Cancelled(InstallError):
    pass

def validate_rom(path):
    path = Path(path).expanduser()
    if not path.is_file() or path.stat().st_size != ROM_SIZE:
        raise InstallError('Select the 32 MiB Mario Party 2 USA ROM.')
    data = path.read_bytes()
    magic = data[:4]
    if magic == bytes.fromhex('37804012'):
        data = bytes(b for i in range(0, len(data), 2) for b in (data[i+1], data[i]))
    elif magic == bytes.fromhex('40123780'):
        data = bytes(b for i in range(0, len(data), 4) for b in data[i:i+4][::-1])
    if hashlib.sha1(data).hexdigest() != ROM_SHA1:
        raise InstallError('ROM does not match Mario Party 2 USA. Expected SHA-1: ' + ROM_SHA1)
    return data

def source_files():
    # This allowlist is also used by the repository publication audit.
    from tools.audit_repository import ALLOWED
    return sorted(ALLOWED)

def source_digest():
    digest = hashlib.sha256()
    for name in source_files():
        digest.update(name.encode()); digest.update((ROOT/name).read_bytes())
    return digest.hexdigest()

def system_problems():
    problems = []
    if sys.platform not in ('linux','win32'):
        problems.append('Supported platforms are Linux and Windows x86-64.')
    for command in ('git', 'cmake', 'ninja', 'clang', 'clang++', 'pkg-config', 'rg'):
        if not shutil.which(command): problems.append('Missing command: ' + command)
    if os.name!='nt' and shutil.which('pkg-config'):
        for package in ('sdl2', 'SDL2_ttf', 'gtk+-3.0'):
            if subprocess.run(['pkg-config','--exists',package]).returncode:
                problems.append('Missing development package: ' + package)
    try:
        import venv, ensurepip  # noqa: F401
    except ImportError:
        problems.append('Python venv and ensurepip support are required.')
    fonts = ('/usr/share/fonts/noto/NotoSans-Regular.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', '/usr/share/fonts/TTF/DejaVuSans.ttf')
    if os.name!='nt' and not any(Path(p).is_file() for p in fonts):
        problems.append('Install the Noto Sans or DejaVu Sans system font for game settings.')
    return problems

def lock_file(file):
    if os.name=='nt':
        import msvcrt
        file.seek(0);file.write('0');file.flush();file.seek(0)
        msvcrt.locking(file.fileno(),msvcrt.LK_NBLCK,1)
    else:fcntl.flock(file,fcntl.LOCK_EX|fcntl.LOCK_NB)

class Installer:
    def __init__(self, status=print, log=print):
        self.status, self.log = status, log
        self.cancelled = threading.Event()
        self.process = None
        self.process_lock = threading.Lock()

    def cancel(self):
        self.cancelled.set()
        with self.process_lock:
            if self.process and self.process.poll() is None:
                self.terminate(self.process)

    @staticmethod
    def terminate(process):
        if os.name=='nt':
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        else:
            with contextlib.suppress(ProcessLookupError):os.killpg(process.pid,signal.SIGTERM)

    def check_cancelled(self):
        if self.cancelled.is_set(): raise Cancelled('Installation cancelled. You can resume using the same build directory.')

    def run(self, command, cwd, env=None):
        self.check_cancelled()
        self.log('$ ' + shlex.join(map(str, command)))
        with self.process_lock:
            self.check_cancelled()
            # Frozen Qt's DLL search directory must not leak into SDK subprocesses.
            previous=None
            if os.name=='nt' and getattr(sys,'frozen',False):
                import ctypes
                buffer=ctypes.create_unicode_buffer(32768)
                ctypes.windll.kernel32.GetDllDirectoryW(len(buffer),buffer)
                previous=buffer.value
                ctypes.windll.kernel32.SetDllDirectoryW(None)
            try:
                process = self.process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, errors='replace', start_new_session=True)
            finally:
                if previous is not None:ctypes.windll.kernel32.SetDllDirectoryW(previous)
        try:
            for line in process.stdout: self.log(line.rstrip())
            result = process.wait()
        except BaseException:
            if process.poll() is None:
                self.terminate(process)
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    if os.name=="nt":process.kill()
                    else:
                        with contextlib.suppress(ProcessLookupError):os.killpg(process.pid,signal.SIGKILL)
                    process.wait()
            raise
        finally:
            with self.process_lock: self.process = None
        self.check_cancelled()
        if result: raise InstallError(f'Command failed (exit {result}). See the installation log.')

    def install(self, rom, prefix, build_dir=None, jobs=4, desktop=True, dependency_pack=None, sdk_dir=None, source_build=False):
        self.status('Validating ROM')
        data = validate_rom(rom)
        self.check_cancelled()
        problems = system_problems() if source_build else []
        if problems: raise InstallError('\n'.join(problems))
        prefix = Path(prefix).expanduser().resolve()
        digest = source_digest()
        cache = Path(os.environ.get('LOCALAPPDATA',Path.home()/'AppData/Local'))/'mp2recomp-cache' if os.name=='nt' else Path(os.environ.get('XDG_CACHE_HOME',Path.home()/'.cache'))
        build_dir = Path(build_dir).expanduser().resolve() if build_dir else cache/'mp2recomp'/'installer'/digest[:12]
        # Never overwrite the developer checkout or build under the install directory.
        if build_dir == ROOT or ROOT in build_dir.parents or prefix == ROOT or ROOT in prefix.parents:
            raise InstallError('Choose install/build directories outside the source checkout.')
        if prefix == build_dir or prefix in build_dir.parents or build_dir in prefix.parents:
            raise InstallError('Install and build directories must be separate.')
        prefix.mkdir(parents=True,exist_ok=True)
        build_dir.mkdir(parents=True,exist_ok=True)
        with (prefix/'.install.lock').open('a') as lock, (build_dir/'.build.lock').open('a') as build_lock:
            try:
                lock_file(lock)
                lock_file(build_lock)
            except OSError: raise InstallError('Another installation is using this install or build directory.')
            source = build_dir/'source'
            source.mkdir(exist_ok=True)
            for name in source_files():
                self.check_cancelled()
                original, target = ROOT/name, source/name
                target.parent.mkdir(parents=True,exist_ok=True)
                if not target.exists() or original.read_bytes()!=target.read_bytes(): shutil.copy2(original,target)
            rom_file=source/ROM_NAME
            if not rom_file.exists() or hashlib.sha1(rom_file.read_bytes()).hexdigest()!=ROM_SHA1:
                rom_file.write_bytes(data)
            from tools.dependency_pack import ensure_sdk,load_sdk,sdk_environment
            sdk=None;crt_files=[]
            if source_build:
                self.status('Fetching pinned build dependencies')
                self.run([sys.executable,'tools/bootstrap.py'],source)
                env=dict(os.environ,JOBS=str(jobs))
                python=sys.executable;cmake='cmake';cc='clang';cxx='clang++'
                regenerate=['bash','tools/regenerate.sh']
            else:
                sdk=ensure_sdk(cache,ROOT,self.run,self.status,self.check_cancelled,dependency_pack,sdk_dir)
                metadata=load_sdk(sdk);env=sdk_environment(sdk,metadata)
                tool=lambda name:str(sdk/metadata['tools'][name])
                python=tool('python');cmake=tool('cmake');cc=tool('cc');cxx=tool('cxx')
                self.status('Preparing bundled source dependencies')
                for name in ('N64Recomp','runtime','rt64','decomp','directx-headers'):
                    self.check_cancelled()
                    shutil.copytree(sdk/'sources'/name,source/name,dirs_exist_ok=True,symlinks=False)
                if os.name=='nt' and getattr(sys,'frozen',False):
                    # Reuse the VC runtime already bundled for Qt. DXC also needs
                    # it, including when the system has no VC redistributable.
                    crt_files=[p for p in Path(sys._MEIPASS).rglob('*.dll')
                        if p.name.lower().startswith(('msvcp140','vcruntime140','concrt140'))]
                    for library in crt_files:shutil.copy2(library,source/'rt64/src/contrib/dxc/bin/x64'/library.name)
                regenerate=[python,'tools/regenerate.py','--recompiler',tool('recompiler'),'--rsp',tool('rsp'),'--jobs',str(jobs)]
            self.status('Generating native game code from your ROM')
            self.run(regenerate,source,env)
            self.status('Configuring the native build')
            configure=[cmake,'-S','.', '-B','build/native','-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
                '-DCMAKE_C_COMPILER='+cc,'-DCMAKE_CXX_COMPILER='+cxx,'-DCMAKE_POLICY_VERSION_MINIMUM=3.5']
            if sdk:
                configure+=['-DCMAKE_MAKE_PROGRAM='+tool('ninja'),'-DCMAKE_PREFIX_PATH='+str(sdk/'toolchain'),
                    '-DSDL2_DIR='+str(sdk/'toolchain/lib/cmake/SDL2'),'-DSDL2_ttf_DIR='+str(sdk/'toolchain/lib/cmake/SDL2_ttf')]
                if os.name!='nt':
                    sysroot=sdk/'toolchain/x86_64-conda-linux-gnu/sysroot'
                    if sysroot.is_dir():
                        flags='--gcc-toolchain='+str(sdk/'toolchain')+' --sysroot='+str(sysroot)
                        configure+=['-DCMAKE_C_FLAGS='+flags,'-DCMAKE_CXX_FLAGS='+flags]
            self.run(configure,source,env)
            self.status('Building Mario Party 2 (this can take several minutes)')
            self.run([cmake,'--build','build/native','--target','mp2recomp','-j',str(jobs)],source,env)
            self.check_cancelled()
            self.status('Installing the game and launcher')
            # Replace finished files atomically. User saves and preferences are preserved.
            def put(name, contents=None, copy_from=None, executable=False):
                target=prefix/name; temp=prefix/(name+'.installing')
                if copy_from: shutil.copy2(copy_from,temp)
                else: temp.write_bytes(contents)
                temp.chmod(0o755 if executable else 0o600)
                os.replace(temp,target)
            binary='mp2recomp.exe' if os.name=='nt' else 'mp2recomp'
            if sdk and os.name!='nt':
                self.run([tool('patchelf'),'--set-rpath','$ORIGIN/lib',str(source/'build/native'/binary)],source,env)
            put(binary,copy_from=source/'build/native'/binary,executable=True)
            put(ROM_NAME,contents=data)
            if sdk:
                self.status('Installing runtime libraries')
                library_dir=prefix if os.name=='nt' else prefix/'lib'
                library_dir.mkdir(exist_ok=True)
                if os.name!='nt':
                    pulse=sdk/'toolchain/lib/pulseaudio'
                    if pulse.is_dir():shutil.copytree(pulse,library_dir/'pulseaudio',dirs_exist_ok=True)
                pattern='*.dll' if os.name=='nt' else '*.so*'
                search=[sdk/'toolchain'/('bin' if os.name=='nt' else 'lib'),source/'rt64/src/contrib/dxc'/('bin/x64' if os.name=='nt' else 'lib/x64')]
                if os.name=='nt':search+=[sdk/'python']
                for directory in search:
                    for library in directory.glob(pattern):
                        # The Vulkan loader and GPU-driver support libraries belong
                        # to the player's installed graphics stack.
                        graphics=('libvulkan','libdrm','libgbm','libGL','libEGL','libglapi')
                        if os.name!='nt' and library.name.startswith(graphics):continue
                        if library.is_file():shutil.copy2(library,library_dir/library.name)
            for library in crt_files:shutil.copy2(library,prefix/library.name)
            if os.name=='nt':
                # The executable locates ROM/userdata beside itself, even when
                # launched through Explorer with a different working directory.
                launcher_path=prefix/binary
            else:
                launcher='#!/bin/sh\nset -eu\ncd -- '+shlex.quote(str(prefix))+'\nexport LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\nexec ./mp2recomp "$@"\n'
                put('play',contents=launcher.encode(),executable=True)
                launcher_path=prefix/'play'
            put('install.json',contents=(json.dumps({'source_digest':digest,'rom_sha1':ROM_SHA1,'build_directory':str(build_dir)},indent=2)+'\n').encode())
            if desktop and os.name=='nt':
                desktop_dir=Path(os.environ.get('APPDATA',Path.home()/'AppData/Roaming'))/'Microsoft/Windows/Start Menu/Programs'
                desktop_dir.mkdir(parents=True,exist_ok=True)
                (desktop_dir/'Mario Party 2 Recompiled.url').write_text('[InternetShortcut]\nURL='+launcher_path.as_uri()+'\n')
            if desktop and os.name!='nt':
                applications=Path(os.environ.get('XDG_DATA_HOME',Path.home()/'.local/share'))/'applications'
                applications.mkdir(parents=True,exist_ok=True)
                executable=str(prefix/'play').replace('%','%%')
                for char in ('\\','"','`','$'): executable=executable.replace(char,'\\'+char)
                entry='[Desktop Entry]\nType=Application\nName=Mario Party 2 Recompiled\nExec="'+executable+'"\nTerminal=false\nCategories=Game;\n'
                temp=applications/'mp2recomp.desktop.tmp';temp.write_text(entry)
                os.replace(temp,applications/'mp2recomp.desktop')
            self.status('Installed. Ready to play.')
        return launcher_path

def launch_game(launcher):
    env=dict(os.environ)
    if getattr(sys,'frozen',False):
        if os.name=='nt':
            import ctypes
            buffer=ctypes.create_unicode_buffer(32768)
            ctypes.windll.kernel32.GetDllDirectoryW(len(buffer),buffer)
            ctypes.windll.kernel32.SetDllDirectoryW(None)
            try:return subprocess.Popen([str(launcher)],env=env,start_new_session=True)
            finally:ctypes.windll.kernel32.SetDllDirectoryW(buffer.value)
        if 'LD_LIBRARY_PATH_ORIG' in env:env['LD_LIBRARY_PATH']=env['LD_LIBRARY_PATH_ORIG']
        else:env.pop('LD_LIBRARY_PATH',None)
    return subprocess.Popen([str(launcher)],env=env,start_new_session=True)

def default_prefix():
    if os.name=='nt':return Path(os.environ.get('LOCALAPPDATA',Path.home()/'AppData/Local'))/'mp2recomp'
    return Path(os.environ.get('XDG_DATA_HOME',Path.home()/'.local/share'))/'mp2recomp'

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,help='Mario Party 2 USA ROM (.z64, .v64, or .n64)')
    parser.add_argument('--prefix',type=Path,default=default_prefix(),help='Install directory (default: %(default)s)')
    parser.add_argument('--build-dir',type=Path,help='Separate build/cache directory; reusable after a failed install')
    parser.add_argument('--jobs',type=int,default=min(4,os.cpu_count() or 1),help='Parallel compiler jobs (default: %(default)s)')
    parser.add_argument('--launch',action='store_true',help='Launch the installed game when finished')
    parser.add_argument('--no-desktop',action='store_true',help='Do not create a desktop-menu entry')
    parser.add_argument('--source-build',action='store_true',help='Developer mode: use system build tools instead of a dependency pack')
    parser.add_argument('--dependency-pack',type=Path,help='Offline dependency archive')
    parser.add_argument('--sdk-dir',type=Path,help='Already unpacked dependency pack')
    parser.add_argument('--gui-smoke-test',action='store_true',help=argparse.SUPPRESS)
    parser.add_argument('--gui',action='store_true',help='Open the GUI, optionally prefilled with --rom')
    parser.add_argument('--check-system',action='store_true',help='Check build requirements without installing')
    parser.add_argument('--validate-rom',action='store_true',help='Validate --rom without installing')
    args=parser.parse_args(argv)
    if args.jobs<1 or args.jobs>256: parser.error('--jobs must be between 1 and 256')
    if args.check_system:
        problems=system_problems();print('\n'.join(problems) if problems else 'Build requirements are available.');return bool(problems)
    if args.validate_rom:
        if not args.rom: parser.error('--validate-rom requires --rom')
        try:validate_rom(args.rom);print('Valid Mario Party 2 USA ROM.');return 0
        except (OSError,InstallError) as exc:print(str(exc),file=sys.stderr);return 1
    if args.gui or args.gui_smoke_test or not args.rom:
        try:
            from tools.installer_gui import run_gui
            return run_gui(args)
        except ImportError as exc:
            print('GUI requires PySide6. Use a packaged installer or install tools/installer-requirements.txt. CLI is available with --rom. '+str(exc),file=sys.stderr);return 1
    installer=Installer()
    try:
        launcher=installer.install(args.rom,args.prefix,args.build_dir,args.jobs,not args.no_desktop,dependency_pack=args.dependency_pack,sdk_dir=args.sdk_dir,source_build=args.source_build)
        print('Launcher: '+str(launcher))
        if args.launch:launch_game(launcher)
        return 0
    except KeyboardInterrupt:
        installer.cancel();print('Installation cancelled.',file=sys.stderr);return 130
    except (InstallError,OSError,RuntimeError,ValueError) as exc:
        print(str(exc),file=sys.stderr);return 1

if __name__=='__main__':raise SystemExit(main())
