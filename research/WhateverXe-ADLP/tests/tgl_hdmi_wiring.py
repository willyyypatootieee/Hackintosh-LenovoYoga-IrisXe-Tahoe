#!/usr/bin/env python3
"""Test the actual HDMI loader/topology/GMBUS/lane entry points with IOKit mocks."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text();h=(r/'WhateverXe/kern_xe.hpp').read_text()
fields=h[h.index(' TGLHDMIWiring tglHDMIWiring;'):h.index('\tbool tglGMBusReady')]
functions='\n'.join(re.search(p,s,re.S).group() for p in [r'bool XE::tglLoadHDMIWiring\(.*?\n\}',r'bool XE::wrapTGLGetOSInformation\(.*?\n\}',r'uint32_t XE::wrapTGLGMBus\(.*?\n\}',r'bool XE::wrapTGLComputeLanes\(.*?\n\}'])
observations=s[s.index('struct TGLGMBusObservation {'):s.index('struct TGLMEIRecord {')]
code=(r/'tests/tgl_hdmi_wiring_fixture.cpp').read_text().replace('// FIELDS',fields).replace('// OBSERVATIONS',observations).replace('// FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='tgl-hdmi-wiring-') as d:
 d=Path(d);(d/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(r/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
