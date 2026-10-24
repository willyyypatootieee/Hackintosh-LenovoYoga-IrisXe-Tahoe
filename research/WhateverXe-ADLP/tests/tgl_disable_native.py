#!/usr/bin/env python3
"""Replay qualified donor disableDisplay; hardware/dependencies are mocked."""
from pathlib import Path
import argparse
import hashlib
import subprocess
import tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('donor', type=Path)
args = parser.parse_args()
if hashlib.sha256(args.donor.read_bytes()).hexdigest() != 'c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731':
    parser.error('unqualified donor SHA256')
repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tgl-disable-native-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run(['clang++', '-arch', 'x86_64', '-std=c++14', '-O1', '-g',
                    str(repo / 'tests/tgl_disable_native_fixture.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(args.donor.resolve())], check=True)
