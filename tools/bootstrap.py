#!/usr/bin/env python3
"""Fetch the recorded dependencies without overwriting an existing checkout."""
import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for name, spec in json.loads((root / "dependencies.json").read_text()).items():
    path = root / name
    if not path.exists():
        subprocess.run(["git", "clone", spec["url"], str(path)], check=True)
        subprocess.run(["git", "-C", str(path), "checkout", spec["commit"]], check=True)
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != spec["commit"]:
        raise SystemExit(f"{name}: expected {spec['commit']}, found {actual}; existing checkout left untouched")
    subprocess.run(["git", "-C", str(path), "submodule", "update", "--init", "--recursive"], check=True)
print("Dependencies ready.")
