#!/usr/bin/env python3
"""Convert the community Splat disassembly into N64Recomp symbol metadata."""
import bisect
import hashlib
import json
import re
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
(ROOT / "analysis").mkdir(exist_ok=True)
rom = ROOT / "Mario Party 2 (USA).z64"
assert hashlib.sha1(rom.read_bytes()).hexdigest() == "166eda1c05670d337e2c3f15a5db528ae1e5d6e3"
segments = yaml.safe_load((ROOT / "decomp/marioparty2.yaml").read_text())["segments"]
sections = []
for i, seg in enumerate(segments):
    if not isinstance(seg, dict) or seg["type"] != "code":
        continue
    following = segments[i + 1]
    end = following["start"] if isinstance(following, dict) else following[0]
    sections.append(dict(name=seg["name"], rom=seg["start"], vram=seg["vram"], size=end-seg["start"], functions={}))
starts = [s["rom"] for s in sections]
pattern = re.compile(r"(?:nonmatching|matching) (\w+), (0x[0-9A-Fa-f]+).*?glabel \1\s*\n\s*/\* ([0-9A-Fa-f]+) ([0-9A-Fa-f]+)", re.S)
for asm in (ROOT / "decomp/asm").rglob("*.s"):
    for match in pattern.finditer(asm.read_text()):
        name, size, offset, vram = match.groups()
        offset, vram, size = int(offset, 16), int(vram, 16), int(size, 16)
        sec = sections[bisect.bisect_right(starts, offset) - 1]
        assert offset < sec["rom"] + sec["size"], (name, sec)
        assert vram == sec["vram"] + offset - sec["rom"], (name, sec)
        # The entry point's indirect tail jump is handled by a small host wrapper.
        sec["functions"][vram] = dict(name=name, vram=vram, size=size)

# Use game-prefixed names for libc symbols, avoiding conflicts with the host.
for sec in sections:
    for func in sec["functions"].values():
        if func["name"] in ("setjmp", "longjmp"):
            func["name"] = "mp2_" + func["name"]

out = []
for sec in sections:
    out += ["[[section]]", f'name = ".{sec["name"]}"', f'rom = 0x{sec["rom"]:X}', f'vram = 0x{sec["vram"]:X}', f'size = 0x{sec["size"]:X}', "functions = ["]
    for f in sorted(sec["functions"].values(), key=lambda f: f["vram"]):
        out.append(f'    {{ name = "{f["name"]}", vram = 0x{f["vram"]:X}, size = 0x{f["size"]:X} }},')
    out += ["]", ""]
(ROOT / "mp2.syms.toml").write_text("\n".join(out))
(ROOT / "analysis/sections.json").write_text(json.dumps([{**s, "functions": list(s["functions"].values())} for s in sections], indent=2))
print(f'{sum(len(s["functions"]) for s in sections)} functions in {len(sections)} sections')
