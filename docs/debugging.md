# Local debugging

Every normal launch writes a private session under `userdata/debug/` (or
`--data-dir PATH/debug`). Override it with `--debug-dir PATH`. `latest.json`
points to the newest session; use an explicit session when running several
instances. No debug network port is opened.

- `runtime.log`: combined stdout/stderr, flushed continuously. Rotates at 8 MiB,
  keeping three older files per session. Console output is also preserved.
- `events.jsonl`: timestamped commands, overlay loads, GPU identity, capture
  results and errors. Each line has elapsed seconds and VI count.
- `status.json`: refreshed once per second; final status has `stopped: true`.
- `session.json`: schema, process ID, SDL revision and renderer/control mode.
- `frames.jsonl`: actual timestamps, paths, dimensions, VI registers and latest
  graphics-task/color-image/scissor telemetry for completed PNGs.

Session folders are retained until you remove them. Logs may contain netplay
connection codes. Frames/audio contain your local game's content and are ignored
by Git along with saves and generated game code. Don't publish those artifacts
as source changes.

## Player controls

**F8** records the next **15 seconds**, at a target of **10 PNG frames/second**,
plus stereo PCM16 WAV audio. No recorder or FFmpeg installation is required.
Each capture creates a numbered folder with a manifest. Audio is captured after
volume/mute adjustment, before the SDL queue; a sample-rate change starts a new
WAV segment. WAV headers update during recording to retain usable data after a
crash. Captures are forward recordings, not a rolling history.

Frames are copied from the Vulkan presentation surface, including aspect-ratio
bars. They do not include window decorations or the settings window. Encoding
runs on a bounded background queue (64 MiB); dropped frames/errors are reported.
Actual frame times are authoritative: the target rate can fall with a slow game,
GPU or encoder. GPU capture can affect timing and is off outside recordings.
Headless mode supports audio/logs but cannot produce screenshots or PNG frames.

## AI / command-line workflow

Enable local control explicitly:

```sh
./run.sh --debug-controls --auto-start 650
python tools/debug_client.py status
python tools/debug_client.py screenshot
python tools/debug_client.py capture --seconds 15 --fps 10 --wait
python tools/debug_client.py log --lines 80
python tools/debug_client.py input --buttons 0x8000 --frames 4
python tools/debug_client.py input --x -1 --frames 4
python tools/debug_client.py quit
```

The client prints JSON, including absolute artifact paths. Its `--root PATH`
selects a custom debug root; `--session PATH` pins a specific running session.
Inspect the screenshot and its `.png.json` sidecar before choosing the next
input. An accepted input command schedules a press; allow the game's animation
or dialogue to finish before taking the next screenshot. Start = `0x1000`,
A = `0x8000`, B = `0x4000`; analog axes are -1..1 with positive Y pointing up.
Input injection is disabled in netplay/probes and while restoring a checkpoint. Physical input still works.

For repeatable navigation, save a JSON array of input commands with `at_vi`
(absolute VI tick), `frames`, `buttons`, `x`, and `y`, then submit it near boot:

```sh
python tools/debug_client.py script /path/to/local-inputs.json
```

Use generous spacing for scene transitions and the same ROM/save/settings.
This schedules controller input. For loadable states with verified gameplay replay,
use checkpoint mode below. `--frames N` bounds the run.

The protocol is intentionally filesystem based so both humans and models can
use it without an HTTP dependency or authentication setup. Write a command to
`commands/UNIQUE.tmp`, then atomically rename it to `commands/UNIQUE.json`.
The app polls every 100 ms and writes `responses/UNIQUE.json`, with `ok` plus
results or an `error`. Commands are limited to 16 KiB. Supported `op` values:
`status`, `screenshot`, `capture` (`seconds`: 1..60, `fps`: 1..30), `stop_capture`,
`input` (`frames`: 1..3600, 16-bit `buttons`, axes -1..1, optional `at_vi`),
`trace`, `save_state`, `load_state` (`path`), `pause`, `step` (`frames`: 1..600),
`resume`, and `quit`.
Responses acknowledge scheduling. A screenshot is ready when its PNG and JSON
sidecar exist; check frame errors in the event log. A capture finishes at its
deadline or on `stop_capture`; queued encodes may complete afterward.
`trace` writes the next graphics task's interpreted command stream (up to 16,384
commands, current task microcode), including framebuffer/scissor context. It is a
local artifact for inspecting rendering issues. Tasks can switch microcodes;
commands dispatched through another microcode map are not included.
No arbitrary memory writes, shell execution or remote control are exposed.

## Capture and load states

Start **offline checkpoint mode from boot**:

```sh
./run.sh --debug-checkpoints
python tools/debug_client.py save_state
python tools/debug_client.py load_state /absolute/path/to/state-ID.json --wait
python tools/debug_client.py screenshot
python tools/debug_client.py resume
```

`--debug-checkpoints` also enables the JSON controls. **F5** saves a checkpoint,
**F9** loads the latest saved checkpoint, and **F6** resumes after a load.
`--debug-load /path/to/state.json` restores directly from launch. An injected
input command also resumes a loaded checkpoint. `pause` stops at the next input
boundary; `step --frames N` advances a paused checkpoint by N simulation frames.
Screenshots and captures remain available while paused.

A checkpoint stores the initial EEPROM, a fixed RNG seed, and every latched
controller sample plus gameplay hashes. Loads shut down cleanly and restart the
executable, replay the samples, verify every recorded gameplay hash, then pause
at the exact saved simulation-input boundary. A mismatch stops the replay and
writes `state_error`; it never silently claims a successful restore. Status
reports simulation frame, target frame, replaying/paused/failed state.
Saving at a paused boundary is supported. Loading uses an isolated EEPROM under
`debug-replays/`, preserving the ordinary player's save.

These are **replay checkpoints, not instant process/RAM snapshots**. Replaying
currently takes approximately the original play time. This is necessary because
native threads, fiber stacks and GPU resources cannot safely be rewound with a
RAM memcpy. The checked hash covers RNG, board/player and overlay-history state
(the same canonical checkpoints used by netplay), not all RAM or GPU pixels.
Use the same ROM and compatible game/runtime code. Renderer-only changes can be
compared from a checkpoint because they don't change the recorded inputs.
Capture mode must have been enabled when the session started; arbitrary older
sessions cannot retroactively produce loadable states. Checkpoints are private
local JSON files, not distributable source assets.

## Verification

With your local ROM and native build:

```sh
python tools/test_debug.py
python tools/test_debug.py --render
python tools/test_debug_states.py
python tools/test_debug_states.py --render
cmake --build build/native --target renderer_patch_test
./build/native/renderer_patch_test
```

The tests use isolated save/log directories and only stop their own subprocess.
They exercise malformed/invalid commands, live status/input, a 15-second audio
capture, headless errors or Vulkan screenshots/frames, and graceful shutdown.
The render test needs a working display and Vulkan driver. All outputs stay in
`build/`. Windows code paths are supported by the source but require verification
on a Windows machine.

## Pirate Land border regression

The board's background uses 64x48 photo blocks split into 32- and 16-row strips,
with coordinates biased by half a pixel. The pinned RT64 rectangle viewport
conversion ceils those coordinates, shifting the photo right/down and leaving
the first row/column uncovered. The frontend now translates that exact unscaled,
one-cycle, full-scissor 320x240 tile pattern to integer pixel edges before RT64
processes it. It doesn't crop the displayed image or modify guest command memory.
The geometry regression checks all 50 strips cover the complete screen and that
UI/scaled/copy/flip rectangles and other framebuffer layouts aren't changed.
