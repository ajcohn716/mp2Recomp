"""Exercise a packaged SDL Vulkan window on a headless Wayland compositor."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

def test_wayland(program,env=None):
    weston=shutil.which('weston')
    if not weston:raise RuntimeError('Install Weston to run the Wayland package check.')
    # Weston itself must use system graphics libraries, not the SDK's libraries.
    server_env=dict(os.environ);server_env.pop('LD_LIBRARY_PATH',None)
    help_text=subprocess.check_output([weston,'--help'],env=server_env,stderr=subprocess.STDOUT,text=True)
    renderer='--renderer=pixman' if '--renderer' in help_text else '--use-pixman'
    with tempfile.TemporaryDirectory(prefix='mp2-wayland-') as runtime, tempfile.TemporaryFile(mode='w+t') as log:
        os.chmod(runtime,0o700);server_env['XDG_RUNTIME_DIR']=runtime
        server=subprocess.Popen([weston,'--backend=headless-backend.so',renderer,'--socket=mp2-wayland',
            '--idle-time=0','--no-config'],env=server_env,stdout=log,stderr=subprocess.STDOUT)
        try:
            socket=Path(runtime)/'mp2-wayland'
            deadline=time.monotonic()+15
            while not socket.exists():
                if server.poll() is not None or time.monotonic()>deadline:
                    log.seek(0);raise RuntimeError('Weston failed to start:\n'+log.read())
                time.sleep(0.05)
            client_env=dict(env or os.environ,SDL_VIDEODRIVER='wayland',GDK_BACKEND='wayland',
                XDG_RUNTIME_DIR=runtime,WAYLAND_DISPLAY=socket.name,LIBDECOR_FORCE_CSD='1')
            client_env.pop('DISPLAY',None)
            subprocess.run([str(Path(program).resolve()),'--video-self-test'],env=client_env,check=True,timeout=45)
        finally:
            server.terminate()
            try:server.wait(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.wait()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('program',type=Path)
    test_wayland(parser.parse_args().program)
