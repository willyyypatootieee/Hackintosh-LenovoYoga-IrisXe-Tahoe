#!/usr/bin/env python3
"""Extract the opt-in host adapter; fake firmware/MMIO, no hardware qualification."""
from pathlib import Path
import subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
s=(repo/'WhateverXe/kern_xe.cpp').read_text()
s=s[s.index('struct TGLBootstrapTrialState'):s.index('// Upper AUX runtime attachment',s.index('struct TGLBootstrapTrialState'))]
with tempfile.TemporaryDirectory(prefix='tgl-bootstrap-host-') as d:
    d=Path(d);(d/'adapter.inc').write_text(s)
    subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(d),'-I',str(repo/'WhateverXe'),str(repo/'tests/tgl_bootstrap_host_fixture.cpp'),'-o',str(d/'check')],check=True)
    subprocess.run([str(d/'check')],check=True)
