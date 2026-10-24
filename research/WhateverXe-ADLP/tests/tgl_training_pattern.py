#!/usr/bin/env python3
"""Run the actual training-pattern wrapper against guarded MMIO, ASan/UBSan."""
from pathlib import Path
import re, subprocess, tempfile, sys
repo=Path(__file__).resolve().parents[1]
cpp=Path(sys.argv[1]).read_text() if len(sys.argv)>1 else (repo/'WhateverXe/kern_xe.cpp').read_text()
header=(repo/'WhateverXe/kern_xe.hpp').read_text()
fields=header[header.index('\tbool tglAUXParentReady'):header.index('\tstatic void wrapTGLEnableAUXPower')]
body=re.search(r'uint32_t XE::wrapTGLTrainingPattern\(.*?\n\}',cpp,re.S).group()
fixture=(repo/'tests/tgl_training_pattern_fixture.cpp').read_text().replace('// FIELDS',fields).replace('// FUNCTION',body)
with tempfile.TemporaryDirectory(prefix='tgl-pattern-') as directory:
    path=Path(directory); (path/'check.cpp').write_text(fixture)
    subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer','-I',str(repo/'WhateverXe'),str(path/'check.cpp'),'-o',str(path/'check')],check=True)
    subprocess.run([str(path/'check')],check=True)
