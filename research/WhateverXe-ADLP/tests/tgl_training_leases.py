#!/usr/bin/env python3
"""Exercise the actual multi-port lease functions with fake native pool/MMIO.

This validates ownership and register selection, not physical 3-display output.
The fixture wiring/backend is explicit; physical multi-display qualification is separate.
No Apple binary is needed for this test.
"""
from pathlib import Path
import argparse, subprocess, tempfile
repo = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=repo / 'WhateverXe/kern_xe.cpp',
                    help='optional predecessor source for a regression counterexample')
parser.add_argument('case', nargs='?', choices=('observer', 'arbitration', 'shutdown', 'end'))
args = parser.parse_args()
source = args.source.read_text()
header = (repo / "WhateverXe/kern_xe.hpp").read_text()
fields = header[header.index("\tstruct TGLTrainingLease {"):header.index("\tmach_vm_address_t orgTGLGetOSInformation")]
body = source[source.index("XE::TGLTrainingLease *XE::tglTrainingLeaseForDDI"):source.index("void XE::tglObserveActivation(")]
adoption = source[source.index("bool XE::wrapTGLAllocatePathResources"):source.index("void *XE::wrapTGLAddPath")]
code = (repo / "tests/tgl_training_leases_fixture.cpp").read_text()
wiring = header[header.index("\tbool tglAUXParentReady"):header.index("\tstatic void wrapTGLEnableAUXPower")]
code = code.replace("// TC_WIRING_FIELDS", wiring)
code = code.replace("// LEASE_FIELDS", fields).replace("// LEASE_FUNCTIONS", body + adoption)
with tempfile.TemporaryDirectory(prefix="tgl-leases-") as directory:
    tmp = Path(directory)
    (tmp / "check.cpp").write_text(code)
    subprocess.run(["clang++", "-std=c++14", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(repo / "WhateverXe"), str(tmp / "check.cpp"), "-o", str(tmp / "check")], check=True)
    subprocess.run([str(tmp / "check"), *([args.case] if args.case else [])], check=True)
