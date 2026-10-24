#!/usr/bin/env python3
"""Exercise the production port admission/drain core; no hardware or donor."""
from pathlib import Path
import subprocess
import tempfile
repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tgl-port-use-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run(['clang++', '-std=c++14', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I', str(repo / 'WhateverXe'),
                    str(repo / 'tests/tgl_port_use_fixture.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
