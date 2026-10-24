#!/usr/bin/env python3
"""Check TGL coordinates and replay actual DKL access (no physical PHY qualification)."""
from pathlib import Path
import re, subprocess, tempfile
repo=Path(__file__).resolve().parents[1]
source=(repo/'WhateverXe/kern_xe.cpp').read_text()
old=(repo/'tests/tgl_dekel_predecessor.cpp.inc').read_text()
pattern=r'bool XE::tglDekelAccess\(.*?\n\}'
current=re.search(pattern,source,re.S).group()
previous=re.search(pattern,old,re.S).group().replace('XE::tglDekelAccess','XE::tglDekelAccessOld')
wrappers='\n'.join(re.search(p,source,re.S).group() for p in [r'uint32_t XE::wrapTGLReadDekel\(.*?\n\}',r'void XE::wrapTGLWriteDekel\(.*?\n\}'])
header=(repo/'WhateverXe/kern_xe.hpp').read_text()
fields=header[header.index('\tbool tglAUXParentReady'):header.index('\tstatic void wrapTGLEnableAUXPower')]
fixture=(repo/'tests/tgl_port_map_fixture.cpp').read_text().replace('// FIELDS',fields)
with tempfile.TemporaryDirectory(prefix='tgl-port-map-') as directory:
 tmp=Path(directory);(tmp/'check.cpp').write_text(fixture.replace('// PRODUCTION_ACCESS',current+'\n'+previous+'\n'+wrappers))
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(repo/'WhateverXe'),str(tmp/'check.cpp'),'-o',str(tmp/'check')],check=True)
 subprocess.run([str(tmp/'check')],check=True)
