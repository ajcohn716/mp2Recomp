#!/usr/bin/env python3
"""Package only reviewed authored source into a self-extracting Linux installer."""
import base64
import io
from pathlib import Path
import subprocess
import sys
import tarfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from install import source_files

def main():
    subprocess.run([sys.executable,str(ROOT/'tools/audit_repository.py')],cwd=ROOT,check=True)
    stream=io.BytesIO()
    with tarfile.open(fileobj=stream,mode='w:gz') as archive:
        for name in source_files():
            path=ROOT/name
            if path.is_symlink() or not path.is_file():raise SystemExit('Not a regular source file: '+name)
            # Package the exact staged blobs that passed the publication audit.
            data=subprocess.check_output(['git','show',':'+name],cwd=ROOT)
            info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o755 if path.stat().st_mode&0o111 else 0o644
            archive.addfile(info,io.BytesIO(data))
    output=ROOT/'dist/mp2recomp-installer.run';output.parent.mkdir(exist_ok=True)
    header='''#!/bin/sh
# Source-only installer. Requires Python 3; GUI additionally requires GTK3/PyGObject.
exec python3 - "$0" "$@" <<'MP2_PYTHON'
import base64, io, pathlib, subprocess, sys, tarfile, tempfile
payload = pathlib.Path(sys.argv[1]).read_bytes().partition(b"\\n__MP2_PAYLOAD__\\n")[2]
with tempfile.TemporaryDirectory(prefix="mp2recomp-installer-") as directory:
    with tarfile.open(fileobj=io.BytesIO(base64.b64decode(payload)), mode="r:gz") as archive:
        archive.extractall(directory, filter="data")
    result = subprocess.call([sys.executable, str(pathlib.Path(directory)/"install.py"), *sys.argv[2:]])
sys.exit(result)
MP2_PYTHON
__MP2_PAYLOAD__
'''
    output.write_bytes(header.encode()+base64.encodebytes(stream.getvalue()));output.chmod(0o755)
    print(output)
if __name__=='__main__':main()
