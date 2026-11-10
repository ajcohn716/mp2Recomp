#!/usr/bin/env python3
"""Exercise the real debug interface with the owner's ROM; --render tests GPU PNG capture."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import wave
from debug_client import request, wait_file


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/native/mp2recomp'))
    parser.add_argument('--rom', type=Path, default=Path('Mario Party 2 (USA).z64'))
    parser.add_argument('--render', action='store_true')
    args = parser.parse_args()
    root = Path('build') / ('debug-test-' + str(time.time_ns()))
    root.mkdir(parents=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy')
    if not args.render:
        env['SDL_VIDEODRIVER'] = 'dummy'
    command = [str(args.binary.resolve()), '--rom', str(args.rom.resolve()), '--debug-controls',
               '--debug-dir', str(root / 'debug'), '--data-dir', str(root / 'data'), '--frames', '2400']
    if not args.render:
        command.append('--headless')
    with (root / 'console.log').open('wb') as console:
        process = subprocess.Popen(command, env=env, stdout=console, stderr=console)
        try:
            wait_file(root / 'debug/latest.json', 15)
            folder = Path(json.loads((root / 'debug/latest.json').read_text())['session'])
            state = request(folder, {'op': 'status'})['status']
            assert state['schema'] == 1 and state['controls']
            # Invalid requests must get errors while subsequent commands still work.
            for bad in ({'op': 'nonexistent'}, {'op': 'capture', 'seconds': 0},
                        {'op': 'input', 'frames': -1}, {'op': 'input', 'buttons': 65536},
                        {'op': 'input', 'x': 2}):
                try:
                    request(folder, bad)
                except RuntimeError:
                    pass
                else:
                    raise AssertionError(f'Accepted invalid command: {bad}')
            (folder / 'commands/broken.json').write_text('{bad json')
            wait_file(folder / 'responses/broken.json', 5)
            assert not json.loads((folder / 'responses/broken.json').read_text())['ok']
            request(folder, {'op': 'input', 'buttons': 4096, 'frames': 4})
            if args.render:
                shot = Path(request(folder, {'op': 'screenshot'})['path'])
                wait_file(Path(str(shot) + '.json'), 10)
                assert shot.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
                meta = json.loads(Path(str(shot) + '.json').read_text())
                assert meta['width'] > 0 and meta['present_vi']['width'] > 0
            else:
                try:
                    request(folder, {'op': 'screenshot'})
                except RuntimeError as error:
                    assert 'Vulkan' in str(error)
                else:
                    raise AssertionError('Headless screenshot accepted')
            capture = Path(request(folder, {'op': 'capture', 'seconds': 15, 'fps': 5})['directory'])
            time.sleep(15.4)
            state = request(folder, {'op': 'status'})['status']
            assert not state['capture_active'] and state['dropped_frames'] == 0
            audio = list(capture.glob('*.wav'))
            assert audio, 'No WAV output'
            total = 0
            for path in audio:
                with wave.open(str(path)) as wav:
                    assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
                    assert wav.getnframes() > 0
                    total += wav.getnframes() / wav.getframerate()
            assert 12 < total < 18, f'Unexpected audio duration: {total}'
            if args.render:
                frames = [json.loads(x) for x in (folder / 'frames.jsonl').read_text().splitlines()]
                assert len(frames) > 30 and all(f['ok'] for f in frames)
            request(folder, {'op': 'capture', 'seconds': 1})
            request(folder, {'op': 'stop_capture'})
            request(folder, {'op': 'quit'})
            assert process.wait(timeout=15) == 0
            assert json.loads((folder / 'status.json').read_text())['stopped']
            log = (folder / 'runtime.log').read_text()
            assert '[MP2] ROM loaded' in log and '[MP2] VI' in log
            events = [json.loads(x)['event'] for x in (folder / 'events.jsonl').read_text().splitlines()]
            assert 'capture_finished' in events and events[-1] == 'session_finished'
            print(f'PASS: JSON control, validation, 15-second WAV/capture, logs and clean shutdown. {root}')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


if __name__ == '__main__':
    main()
