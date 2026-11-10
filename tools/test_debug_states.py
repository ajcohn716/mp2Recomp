#!/usr/bin/env python3
"""Save, restart, verify and resume a real game's deterministic checkpoint."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
from debug_client import request, session, wait_file

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--render', action='store_true')
args = parser.parse_args()
root = Path('build') / ('state-test-' + str(time.time_ns()))
root.mkdir(parents=True)
env = dict(os.environ, SDL_AUDIODRIVER='dummy')
if not args.render:
    env['SDL_VIDEODRIVER'] = 'dummy'
with (root / 'console.log').open('wb') as console:
    process = subprocess.Popen([str(Path('build/native/mp2recomp').resolve()), *([] if args.render else ['--headless']),
        '--debug-controls', '--debug-checkpoints', '--debug-dir', str(root / 'debug'),
        '--data-dir', str(root / 'data'), '--frames', '3600'], env=env, stdout=console, stderr=console)
    try:
        wait_file(root / 'debug/latest.json', 10)
        folder = session(root / 'debug')
        # Exercise the deterministic input latch and clocks through a scene change.
        request(folder, {'op': 'input', 'buttons': 4096, 'at_vi': 650, 'frames': 6})
        deadline = time.monotonic() + 25
        while request(folder, {'op': 'status'})['status']['checkpoint']['frame'] < 450:
            assert time.monotonic() < deadline
            time.sleep(.1)
        saved = Path(request(folder, {'op': 'save_state'})['path'])
        wait_file(saved, 10)
        data = json.loads(saved.read_text())
        assert data['frame'] >= 450 and len(data['inputs']) == data['frame']
        request(folder, {'op': 'load_state', 'path': str(saved)})
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            current = session(root / 'debug')
            if current != folder:
                state = request(current, {'op': 'status'})['status']['checkpoint']
                assert not state['failed'], (current / 'events.jsonl').read_text()
                if state['paused']:
                    assert state['frame'] == data['frame']
                    events = [json.loads(line) for line in (current / 'events.jsonl').read_text().splitlines()]
                    restored = next(e for e in events if e['event'] == 'state_loaded')
                    assert restored['hash'] == data['hash'] and restored['verified']
                    if args.render:
                        shot = Path(request(current, {'op': 'screenshot'})['path'])
                        wait_file(Path(str(shot) + '.json'), 10)
                        assert shot.read_bytes().startswith(b'\x89PNG')
                        assert request(current, {'op': 'status'})['status']['checkpoint']['frame'] == data['frame']
                    paused_save = Path(request(current, {'op': 'save_state'})['path'])
                    wait_file(paused_save, 10)
                    assert json.loads(paused_save.read_text())['hash'] == data['hash']
                    request(current, {'op': 'step', 'frames': 3})
                    step_deadline = time.monotonic() + 5
                    while True:
                        stepped = request(current, {'op': 'status'})['status']['checkpoint']
                        if stepped['paused'] and stepped['frame'] == data['frame'] + 3:
                            break
                        assert time.monotonic() < step_deadline, stepped
                        time.sleep(.05)
                    request(current, {'op': 'resume'})
                    time.sleep(.2)
                    assert request(current, {'op': 'status'})['status']['checkpoint']['frame'] > data['frame']
                    request(current, {'op': 'quit'})
                    assert process.wait(timeout=15) == 0
                    corrupted = json.loads(saved.read_text())
                    corrupted['inputs'][0][0] ^= 1
                    bad = root / 'corrupted-state.json'
                    bad.write_text(json.dumps(corrupted))
                    failed = subprocess.run([str(Path('build/native/mp2recomp').resolve()), '--headless',
                        '--debug-load', str(bad), '--debug-dir', str(root / 'bad-debug'),
                        '--data-dir', str(root / 'bad-data'), '--frames', '300'],
                        env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'),
                        stdout=console, stderr=console, timeout=15)
                    assert failed.returncode == 1
                    failure = session(root / 'bad-debug')
                    assert any(json.loads(line)['event'] == 'state_error'
                               for line in (failure / 'events.jsonl').read_text().splitlines())
                    print('PASS: saved/reloaded state, verified hashes, paused screenshot/save, exact stepping, resume and mismatch rejection.', root)
                    break
            time.sleep(.1)
        else:
            raise TimeoutError('Checkpoint restoration did not finish')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
