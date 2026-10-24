#!/usr/bin/env python3
"""Compile the real native mailbox wrappers and test their shared ownership gate."""
from pathlib import Path
import subprocess
import tempfile
repo=Path(__file__).resolve().parents[1]
source=(repo/'WhateverXe/kern_xe.cpp').read_text()
start=source.index('static TGLPcodeGate tglPcodeGate;')
end=source.index('// Explicit opt-in transport trial',start)
wrappers=source.index('uint32_t XE::wrapTGLMailboxRead(')
stop=source.index('// EDRAM hook starts after native forcewake',wrappers)
fixture=(repo/'tests/tgl_pcode_gate_fixture.cpp').read_text().replace('// NATIVE_WRAPPERS',source[start:end]+source[wrappers:stop])
with tempfile.TemporaryDirectory(prefix='tgl-pcode-gate-') as directory:
    tmp=Path(directory);(tmp/'check.cpp').write_text(fixture)
    subprocess.run(['clang++','-std=c++14','-O1','-g','-pthread','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer','-I',str(repo/'WhateverXe'),str(tmp/'check.cpp'),
                    '-o',str(tmp/'check')],check=True)
    subprocess.run([str(tmp/'check')],check=True)
