#!/usr/bin/env python3
"""FIA operations under an explicit TC-cold reference; offline only."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tgl-fia-') as d:
 b=Path(d)/'check'
 subprocess.run(['clang++','-std=c++14','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(r/'WhateverXe'),str(r/'tests/tgl_fia_fixture.cpp'),'-o',str(b)],check=True)
 subprocess.run([str(b)],check=True)
