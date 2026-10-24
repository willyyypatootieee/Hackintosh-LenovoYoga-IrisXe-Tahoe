#!/usr/bin/env python3
"""Replay the exact TGL eDRAM interval and save a non-deployable audit copy."""
from pathlib import Path
import argparse,hashlib,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('donor',type=Path);p.add_argument('--audit-copy',type=Path,required=True)
a=p.parse_args()
if hashlib.sha256(a.donor.read_bytes()).hexdigest()!='890735a93a19d3ddda24ff31bda3075dbd145fb7b26c11769f33c4564a8027ac':p.error('unqualified donor SHA256')
if a.audit_copy.exists() or a.audit_copy.resolve()==a.donor.resolve():p.error('audit output must be a new path')
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
start=s.index('static uint32_t tglPcodeEDRAMCalls');end=s.index('static bool tglPcodeEDRAMSerialize(',start)
fixture=(r/'tests/tgl_pcode_edram_fixture.cpp').read_text().replace('// EDRAM_HELPER',s[start:end])
with tempfile.TemporaryDirectory(prefix='tgl-edram-') as d:
 d=Path(d);(d/'check.cpp').write_text(fixture)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O1','-g','-I',str(r/'WhateverXe'),str(d/'check.cpp'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check'),str(a.donor.resolve()),str(a.audit_copy.resolve())],check=True)
