#!/usr/bin/env python3
"""Exercise actual probe and connection observer; no live PHY qualification."""
from pathlib import Path
import re, subprocess, tempfile, sys
repo=Path(__file__).resolve().parents[1]
source=(repo/"WhateverXe/kern_xe.cpp").read_text()
if len(sys.argv)>1:
    source=subprocess.check_output(["git","-C",str(repo),"show",sys.argv[1]+":WhateverXe/kern_xe.cpp"],text=True)
patterns=[r"void tglConnectionRecord\(.*?\n\}",r"uint32_t XE::wrapTGLProbePort\(.*?\n\}"]
body="\n".join(re.search(p,source,re.S).group() for p in patterns)
fixture=(repo/"tests/tgl_probe_fixture.cpp").read_text().replace("// PRODUCTION_FUNCTIONS",body)
header=(repo/"WhateverXe/kern_xe.hpp").read_text()
wiring=header[header.index("\tbool tglAUXParentReady"):header.index("\tstatic void wrapTGLEnableAUXPower")]
fixture=fixture.replace("// TC_WIRING_FIELDS",wiring)
with tempfile.TemporaryDirectory(prefix="tgl-probe-") as directory:
    tmp=Path(directory);(tmp/"check.cpp").write_text(fixture)
    subprocess.run(["clang++","-std=c++14","-O1","-g","-fsanitize=address,undefined",
        "-fno-omit-frame-pointer","-I",str(repo/"WhateverXe"),str(tmp/"check.cpp"),"-o",str(tmp/"check")],check=True)
    subprocess.run([str(tmp/"check"),*sys.argv[2:]],check=True)
