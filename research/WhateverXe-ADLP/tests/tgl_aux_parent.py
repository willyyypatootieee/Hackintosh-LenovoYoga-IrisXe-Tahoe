#!/usr/bin/env python3
"""Check real AUX wrappers: inherited refs and multi-port ownership.

The fixture supplies a qualified-port mask for simulated ports; this does not
expand live PHY qualification or test parallel native power execution.
An optional old revision demonstrates the inherited-ref lifetime defect.
"""
from pathlib import Path
import re, subprocess, tempfile, sys
repo = Path(__file__).resolve().parents[1]
def source(path):
    if len(sys.argv) > 1:
        return subprocess.check_output(["git", "-C", str(repo), "show", sys.argv[1]+":"+path], text=True)
    return (repo/path).read_text()
cpp = source("WhateverXe/kern_xe.cpp")
header = source("WhateverXe/kern_xe.hpp")
fields = header[header.index("\tbool tglAUXParentReady"):header.index("\tstatic void wrapTGLEnableAUXPower")]
body = "\n".join(re.search(pattern,cpp,re.S).group() for pattern in [
    r"void XE::wrapTGLEnableAUXPower\(.*?\n\}", r"void XE::wrapTGLDisableAUXPower\(.*?\n\}"])
fixture = (repo/"tests/tgl_aux_parent_fixture.cpp").read_text()
fixture = fixture.replace("// AUX_FIELDS",fields).replace("// AUX_WRAPPERS",body)
fixture = "#define WIRING " + str(int("tglTCWiring" in fields)) + "\n" + fixture
fixture = "#define LEGACY " + str(int("tglAUXParentRefs[9]" not in fields)) + "\n" + fixture
with tempfile.TemporaryDirectory(prefix="tgl-aux-parent-") as directory:
    tmp = Path(directory); (tmp/"check.cpp").write_text(fixture)
    subprocess.run(["clang++","-std=c++14","-O1","-g","-fsanitize=address,undefined",
        "-fno-omit-frame-pointer","-I",str(repo/"WhateverXe"),str(tmp/"check.cpp"),"-o",str(tmp/"check")],check=True)
    subprocess.run([str(tmp/"check")],check=True)
