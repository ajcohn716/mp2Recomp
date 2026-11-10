#!/usr/bin/env python3
"""Four real game processes in isolated folders; requires the owner's local ROM.

Uses a test lease and only terminates subprocesses it starts. The private traces
are checks of canonical gameplay state, not a complete-memory determinism proof.
"""
import argparse
import asyncio
import contextlib
import os
from pathlib import Path
import re
import sys
import tempfile
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


@contextlib.contextmanager
def lease():
    # One machine-wide test lease owns the four slots for this entire run.
    with open(Path(tempfile.gettempdir()) / 'mp2recomp-netplay-test.lock', 'a+b') as handle:
        if os.name == 'nt':
            import msvcrt
            handle.write(b'0'); handle.flush(); handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


async def main(args):
    root = Path(__file__).resolve().parents[1]
    folder = root / 'build' / ('netplay-game-' + str(time.time_ns()))
    folder.mkdir(parents=True)
    print('Private logs and traces:', folder, flush=True)
    processes, files = [], []
    with lease():
        async def spawn(player, code=''):
            data = folder / f'data-{player}'
            data.mkdir()
            if args.save:
                (data / 'saves').mkdir()
                (data / 'saves/mp2.us.bin').write_bytes(args.save.read_bytes())
            command = [str(args.binary.resolve()), '--netplay', '--host-address', '127.0.0.1', '--host-port', str(args.port),
                       '--netplay-auto-start', '4', '--headless', '--data-dir', str(data),
                       '--netplay-frames', str(args.frames), '--netplay-bot',
                       '--netplay-trace', str(folder / f'p{player}.trace'), '--rom', str(args.rom.resolve())]
            if code:
                command += ['--join', code]
            log = (folder / f'p{player}.log').open('wb')
            files.append(log)
            process = await asyncio.create_subprocess_exec(*command, cwd=root,
                env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'), stdout=log, stderr=log)
            processes.append(process)
        try:
            await spawn(0)
            deadline = time.monotonic() + 15
            code = None
            while time.monotonic() < deadline:
                match = re.search(r'\[NETPLAY\] connection code (MP2-[A-Za-z0-9_-]+)', (folder / 'p0.log').read_text(errors='replace'))
                if match:
                    code = match[1]
                    break
                if processes[0].returncode is not None:
                    break
                await asyncio.sleep(.02)
            if not code:
                raise RuntimeError('Host did not create a room; inspect p0.log')
            for i in range(1, 4):
                await spawn(i, code)
                # Keep joins ordered so input ownership has a stable oracle.
                deadline = time.monotonic() + 10
                while not re.search(r'\[NETPLAY\] joined as player', (folder / f'p{i}.log').read_text(errors='replace')):
                    if time.monotonic() > deadline:
                        raise RuntimeError('Guest failed to join')
                    await asyncio.sleep(.02)
            result = await asyncio.wait_for(asyncio.gather(*(p.wait() for p in processes)), args.frames / 20 + 45)
            if any(result):
                raise RuntimeError('Game exit codes: ' + str(result))
            traces = [(folder / f'p{i}.trace').read_text().splitlines() for i in range(4)]
            if any(len(t) != args.frames for t in traces):
                raise RuntimeError('Incomplete traces: ' + str([len(t) for t in traces]))
            for i in range(1, 4):
                for frame, (host, guest) in enumerate(zip(traces[0], traces[i])):
                    if host != guest:
                        raise RuntimeError(f'First state/input mismatch: player {i+1}, frame {frame}')
            print(f'PASS: four game processes, {args.frames} matching gameplay checkpoints, RNG states and four-port inputs', flush=True)
        finally:
            for process in processes:
                if process.returncode is None:
                    process.kill()
            await asyncio.gather(*(p.wait() for p in processes))
            for file in files:
                file.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/native/mp2recomp'))
    parser.add_argument('--rom', type=Path, default=Path('Mario Party 2 (USA).z64'))
    parser.add_argument('--save', type=Path)
    parser.add_argument('--port', type=int, default=17777)
    parser.add_argument('--frames', type=int, default=1200)
    asyncio.run(main(parser.parse_args()))
