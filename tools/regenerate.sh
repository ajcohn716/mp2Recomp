#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
test -f 'Mario Party 2 (USA).z64'
if [[ ! -x .venv/bin/splat ]]; then
    python -m venv .venv
    .venv/bin/pip install 'splat64[mips]==0.50.1' 'spimdisasm==1.42.4' 'PyYAML==6.0.3'
fi
mkdir -p build analysis
if [[ ! -e decomp/baserom.us.z64 ]]; then
    ln -s '../Mario Party 2 (USA).z64' decomp/baserom.us.z64
fi
cmake -S N64Recomp -B build/recompiler -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/recompiler --target N64RecompCLI RSPRecomp -j "${JOBS:-8}"
.venv/bin/python tools/regenerate.py \
    --recompiler "$PWD/build/recompiler/N64Recomp" --rsp "$PWD/build/recompiler/RSPRecomp"
