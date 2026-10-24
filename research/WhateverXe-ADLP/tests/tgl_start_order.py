#!/usr/bin/env python3
"""Bounded startup capture publication under concurrent callbacks."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tgl-start-order-') as d:
 b=Path(d)/'check'
 subprocess.run(['clang++','-std=c++14','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(r/'WhateverXe'),str(r/'tests/tgl_start_order.cpp'),'-o',str(b)],check=True)
 subprocess.run([str(b)],check=True)
