#!/usr/bin/env python3
"""Extract actual bootstrap AUX adapter and all three kernel wrappers."""
from pathlib import Path
import subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
s=(repo/'WhateverXe/kern_xe.cpp').read_text()
state=s[s.index('struct TGLBootstrapTrialState'):s.index('static void tglBootstrapTrialRun(')]
adapter=s[s.index('struct TGLBootstrapAUXStats'):s.index('static bool tglBootstrapAUXSerialize(')]
wrappers=s[s.index('uint32_t XE::wrapTGLReadAUX('):s.index('uint32_t XE::wrapTGLTrainingPattern(')]
with tempfile.TemporaryDirectory(prefix='tgl-bootstrap-aux-') as directory:
 d=Path(directory);(d/'adapter.inc').write_text(state+adapter);(d/'wrappers.inc').write_text(wrappers)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(d),'-I',str(repo/'WhateverXe'),str(repo/'tests/tgl_bootstrap_aux_fixture.cpp'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check')],check=True)
