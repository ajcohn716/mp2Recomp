#!/usr/bin/env python3
"""Verify that the Linux and Windows release archives contain matching netplay IDs."""

import argparse
from pathlib import Path
import re
import tarfile
import zipfile


def build_id(binary, magic, platform):
    if not binary.startswith(magic):
        raise SystemExit(f'{platform} archive does not contain the expected executable format.')
    # The handshake uses a null-terminated lowercase SHA-256 string. Require a
    # unique candidate so unexpected executable contents fail the release check.
    ids = set(re.findall(rb'(?<![0-9a-f])[0-9a-f]{64}(?=\x00)', binary))
    if len(ids) != 1:
        raise SystemExit(f'Expected one netplay build ID in the {platform} executable, found {len(ids)}.')
    return ids.pop().decode('ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('linux_archive', type=Path)
    parser.add_argument('windows_archive', type=Path)
    args = parser.parse_args()

    with tarfile.open(args.linux_archive, 'r:gz') as archive:
        member = archive.getmember('mp2recomp-game-linux-x86_64/mp2recomp')
        if not member.isfile():
            raise SystemExit('Linux archive does not contain a regular game executable.')
        with archive.extractfile(member) as binary:
            linux_id = build_id(binary.read(), b'\x7fELF', 'Linux')
    with zipfile.ZipFile(args.windows_archive) as archive:
        windows_id = build_id(archive.read('mp2recomp-game-windows-x86_64/mp2recomp.exe'), b'MZ', 'Windows')

    if linux_id != windows_id:
        raise SystemExit(f'Netplay build IDs differ\nLinux   {linux_id}\nWindows {windows_id}')
    print(f'Linux and Windows netplay build ID: {linux_id}')


if __name__ == '__main__':
    main()
