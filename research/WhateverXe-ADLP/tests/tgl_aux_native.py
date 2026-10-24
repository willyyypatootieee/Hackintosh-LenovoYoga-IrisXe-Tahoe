#!/usr/bin/env python3
"""Replay exact TGL donor AUX methods with the actual TC-cold core.

Supply the unmodified, locally retained framebuffer donor; no Apple payload is
included in this test. Does not load kexts or contact hardware.
"""
from pathlib import Path
import argparse
import hashlib
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('donor', type=Path)
args = parser.parse_args()
expected = 'c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
if hashlib.sha256(args.donor.read_bytes()).hexdigest() != expected:
    parser.error('unqualified framebuffer donor SHA256')
repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tgl-aux-native-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run(['clang++', '-arch', 'x86_64', '-std=c++14', '-O1', '-g',
                    '-I', str(repo / 'WhateverXe'),
                    str(repo / 'tests/tgl_aux_native_fixture.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(args.donor.resolve())], check=True)
