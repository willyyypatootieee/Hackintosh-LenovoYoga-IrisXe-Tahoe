#!/usr/bin/env python3
"""Exercise actual property loader and lane admission with mocked IOKit."""
from pathlib import Path
import re, subprocess, tempfile
repo=Path(__file__).resolve().parents[1]
cpp=(repo/'WhateverXe/kern_xe.cpp').read_text()
header=(repo/'WhateverXe/kern_xe.hpp').read_text()
fields=header[header.index('\tbool tglAUXParentReady'):header.index('\tstatic void wrapTGLEnableAUXPower')]
body='\n'.join(re.search(p,cpp,re.S).group() for p in
 [r'bool XE::tglLoadTCWiring\(.*?\n\}',r'uint32_t XE::wrapTGLLaneMask\(.*?\n\}'])
fixture=(repo/'tests/tgl_wiring_fixture.cpp').read_text().replace('// FIELDS',fields).replace('// FUNCTIONS',body)
with tempfile.TemporaryDirectory(prefix='tgl-wiring-') as directory:
    path=Path(directory);(path/'check.cpp').write_text(fixture)
    subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer','-I',str(repo/'WhateverXe'),str(path/'check.cpp'),'-o',str(path/'check')],check=True)
    subprocess.run([str(path/'check')],check=True)
