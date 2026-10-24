#!/usr/bin/env python3
"""Power/readout error contracts; mocked hardware, no live qualification."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tgl-boot-readout-') as d:
    b=Path(d)/'check'
    subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(r/'WhateverXe'),str(r/'tests/tgl_boot_readout_fixture.cpp'),'-o',str(b)],check=True)
    subprocess.run([str(b)],check=True)
