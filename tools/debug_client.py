#!/usr/bin/env python3
"""Local JSON diagnostics client. No dependencies, socket, or shell injection."""
import argparse
import json
from pathlib import Path
import sys
import time
import uuid


def session(root, explicit=None):
    return explicit.resolve() if explicit else Path(json.loads((root / 'latest.json').read_text())['session'])


def request(folder, command, timeout=10):
    ident = uuid.uuid4().hex
    pending = folder / 'commands' / (ident + '.tmp')
    pending.write_text(json.dumps(command), encoding='utf-8')
    pending.rename(pending.with_suffix('.json'))
    reply = folder / 'responses' / (ident + '.json')
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = json.loads(reply.read_text(encoding='utf-8'))
        except (FileNotFoundError, json.JSONDecodeError):
            time.sleep(.05)
            continue
        if not result.get('ok'):
            raise RuntimeError(result.get('error', 'Command failed'))
        return result
    raise TimeoutError(f'No response from {folder}; launch with --debug-controls and select a live session')


def wait_file(path, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.is_file():
            return
        time.sleep(.05)
    raise TimeoutError(f'Capture did not finish writing {path}; inspect events.jsonl')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('userdata/debug'))
    parser.add_argument('--session', type=Path, help='Target a specific session instead of latest.json')
    parser.add_argument('--timeout', type=float, default=10)
    sub = parser.add_subparsers(dest='op', required=True)
    sub.add_parser('status')
    sub.add_parser('screenshot')
    sub.add_parser('trace')
    capture = sub.add_parser('capture')
    capture.add_argument('--seconds', type=int, default=15)
    capture.add_argument('--fps', type=int, default=10)
    capture.add_argument('--wait', action='store_true')
    sub.add_parser('stop_capture')
    pad = sub.add_parser('input')
    pad.add_argument('--buttons', type=lambda s: int(s, 0), default=0,
                     help='N64 bitmask: A=0x8000 B=0x4000 Start=0x1000')
    pad.add_argument('--x', type=float, default=0)
    pad.add_argument('--y', type=float, default=0)
    pad.add_argument('--frames', type=int, default=4)
    pad.add_argument('--at-vi', type=int)
    script = sub.add_parser('script')
    script.add_argument('path', type=Path, help='JSON array of input commands, with absolute at_vi values')
    sub.add_parser('quit')
    sub.add_parser('save_state')
    load = sub.add_parser('load_state')
    load.add_argument('path', type=Path)
    load.add_argument('--wait', action='store_true', help='Wait for replay and hash verification')
    sub.add_parser('resume')
    sub.add_parser('pause')
    step = sub.add_parser('step')
    step.add_argument('--frames', type=int, default=1)
    log = sub.add_parser('log')
    log.add_argument('--lines', type=int, default=80)
    args = parser.parse_args()
    folder = session(args.root, args.session)
    if args.op == 'log':
        print('\n'.join((folder / 'runtime.log').read_text(errors='replace').splitlines()[-args.lines:]))
        return
    if args.op == 'script':
        commands = json.loads(args.path.read_text())
        if not isinstance(commands, list):
            raise ValueError('Script must be an array')
        results = [request(folder, dict(item, op='input'), args.timeout) for item in commands]
        print(json.dumps(results, indent=2))
        return
    command = {'op': args.op}
    for key in ('seconds', 'fps', 'buttons', 'x', 'y', 'frames', 'at_vi'):
        value = getattr(args, key, None)
        if value is not None:
            command[key] = value
    if args.op == 'load_state':
        command['path'] = str(args.path.resolve())
    result = request(folder, command, args.timeout)
    if args.op in ('save_state', 'trace'):
        wait_file(Path(result['path']), args.timeout)
    if args.op == 'load_state' and args.wait:
        target = json.loads(args.path.read_text())['frame']
        deadline = time.monotonic() + target / 15 + args.timeout
        while time.monotonic() < deadline:
            current = session(folder.parent)
            if current != folder:
                try:
                    state = request(current, {'op': 'status'}, 2)['status']['checkpoint']
                except TimeoutError:
                    time.sleep(.2)
                    continue
                if state['failed']:
                    raise RuntimeError(f'Checkpoint replay failed; inspect {current / "events.jsonl"}')
                if state['paused'] and not state['replaying'] and state['frame'] == target:
                    result['session'] = str(current)
                    result['checkpoint'] = state
                    break
            time.sleep(.2)
        else:
            raise TimeoutError('Checkpoint replay did not finish')
    if args.op == 'screenshot':
        path = Path(result['path'])
        wait_file(Path(str(path) + '.json'), args.timeout)
        wait_file(path, args.timeout)
        meta = json.loads(Path(str(path) + '.json').read_text())
        if not meta.get('ok', True):
            raise RuntimeError(meta.get('error', 'Screenshot failed'))
    if args.op == 'capture' and args.wait:
        target = result['directory']
        deadline = time.monotonic() + args.seconds + args.timeout
        while time.monotonic() < deadline:
            events = [json.loads(line) for line in (folder / 'events.jsonl').read_text().splitlines()]
            if any(e['event'] == 'capture_finished' and e['directory'] == target for e in events):
                if request(folder, {'op': 'status'}, args.timeout)['status']['pending_frames'] == 0:
                    break
            time.sleep(.1)
        else:
            raise TimeoutError('Capture did not finish; inspect events.jsonl')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(json.dumps({'ok': False, 'error': str(error)}), file=sys.stderr)
        sys.exit(1)
