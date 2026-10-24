#!/usr/bin/env python3
"""Test the TC-cold protocol core; not kernel/native PCODE integration."""
from pathlib import Path
import subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="tgl-tc-cold-") as directory:
    binary=Path(directory)/"check"
    subprocess.run(["clang++","-std=c++14","-O1","-g","-pthread",
        "-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",str(repo/"WhateverXe"),
        str(repo/"tests/tgl_tc_cold_fixture.cpp"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
