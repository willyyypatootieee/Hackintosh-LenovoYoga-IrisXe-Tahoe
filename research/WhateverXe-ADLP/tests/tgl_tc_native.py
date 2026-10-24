#!/usr/bin/env python3
"""Replay the native ABI bridge with real base/reset, port-AUX and PG accounting."""
from pathlib import Path
import argparse,hashlib,re,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('donor',type=Path);a=p.parse_args()
b=a.donor.read_bytes()
if hashlib.sha256(b).hexdigest()!='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731':p.error('unqualified donor SHA256')
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
for name,offset in [('tcBaseEntry',0x1b4c8),('tcAuxGetEntry',0x1d23e),('tcAuxPutEntry',0x1d27a),('tcPGGetEntry',0x5bedc),('tcPGPutEntry',0x5b366)]:
 v=re.search(r'const uint8_t '+name+r'\[\] \{([^}]+)\}',s).group(1)
 expected=bytes(int(n.strip(),0) for n in v.split(','));assert b[offset:offset+len(expected)]==expected,name
with tempfile.TemporaryDirectory(prefix='tgl-tc-native-') as d:
 exe=Path(d)/'check'
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(r/'WhateverXe'),str(r/'tests/tgl_tc_native_fixture.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe),str(a.donor.resolve())],check=True)
