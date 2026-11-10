#!/usr/bin/env python3
"""Generate game code locally, using either bundled tools or a source tool build."""
import argparse
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--recompiler',type=Path);p.add_argument('--rsp',type=Path);p.add_argument('--jobs',default='4');args=p.parse_args()
    def run(command,**kw):subprocess.run(list(map(str,command)),cwd=ROOT,check=True,**kw)
    (ROOT/'build').mkdir(exist_ok=True);(ROOT/'analysis').mkdir(exist_ok=True)
    rom=ROOT/'Mario Party 2 (USA).z64'
    if not rom.is_file():raise SystemExit('USA ROM is required')
    # Copies work without symlink privileges on Windows.
    import shutil
    base=ROOT/'decomp/baserom.us.z64'
    if not base.exists():shutil.copyfile(rom,base)
    with (ROOT/'build/splat.log').open('w') as log:
        subprocess.run([sys.executable,'-m','splat','split','marioparty2.yaml','--disassemble-all'],cwd=ROOT/'decomp',stdout=log,stderr=subprocess.STDOUT,check=True)
    run([sys.executable,'tools/generate_symbols.py'])
    if not args.recompiler:
        run(['cmake','-S','N64Recomp','-B','build/recompiler','-G','Ninja','-DCMAKE_BUILD_TYPE=Release'])
        run(['cmake','--build','build/recompiler','--target','N64RecompCLI','RSPRecomp','-j',args.jobs])
        suffix='.exe' if sys.platform=='win32' else ''
        args.recompiler=ROOT/('build/recompiler/N64Recomp'+suffix);args.rsp=ROOT/('build/recompiler/RSPRecomp'+suffix)
    for command,logname in (([args.recompiler,'mp2.toml'],'recompile.log'),([args.rsp,'aspMain.toml'],'rsp-recompile.log')):
        with (ROOT/'build'/logname).open('w') as log:run(command,stdout=log,stderr=subprocess.STDOUT)
        if 'Error recompiling' in (ROOT/'build'/logname).read_text():raise SystemExit('Recompilation failed: '+logname)
    print('CPU and audio microcode generated locally.')
if __name__=='__main__':main()
