#!/usr/bin/env python3
"""Compose the real native base setter, TC-cold core and FIA mode lifetime."""
from pathlib import Path
import argparse,hashlib,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('donor',type=Path);a=p.parse_args()
if hashlib.sha256(a.donor.read_bytes()).hexdigest()!='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731':p.error('unqualified donor SHA256')
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tgl-tc-mode-') as d:
 b=Path(d)/'check'
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(r/'WhateverXe'),str(r/'tests/tgl_tc_mode_fixture.cpp'),'-o',str(b)],check=True)
 subprocess.run([str(b),str(a.donor.resolve())],check=True)
