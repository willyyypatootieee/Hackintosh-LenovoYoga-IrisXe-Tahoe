# WhateverXe

This repository owns WhateverXe, AppleIntelTGLCompat, their shared contracts and
standalone CPU tests. Preserve exact driver and physical identity guards. Keep
refactoring separate from adding support for new hardware.

## Documentation

README is the user-facing entry point: purpose, current compatibility, required
components, configuration links, build instructions and license. Update those
sections when user-visible behavior changes; do not append implementation or
per-machine test history after each change.
Keep README.md and README_ja.md synchronized in the same change, including
requirements, compatibility, links and examples. Do not add personal names or
private identifying information to user documentation without explicit consent.

Document required and optional boot arguments in both READMEs. Do not add
migration instructions for unreleased internal revisions.
Public docs describe current settings and requirements. Keep
investigation notes, implementation diaries, internal diagnostic layouts,
experimental procedures and live machine evidence in the maintainer's private
project documentation, outside this repository. Do not link public instructions
to private files or require access to private history to use or test the source.
Source attribution, licenses and reproducible test fixtures remain here.

## Build and test

Build with `python3 tools/build.py`; run CPU tests with `python3 tools/test.py`.
Lilu.kext and MacKernelSDK are local ignored dependencies. Optional native replay
requires the exact Apple drivers accepted by the tests. Do not commit generated
payloads, Apple driver files, credentials or local machine state.
