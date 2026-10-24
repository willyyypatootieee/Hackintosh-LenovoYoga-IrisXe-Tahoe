# Yoga development source archive

**Not a one-click installer. HDMI and stability are not fixed.**

This directory preserves the local GMS/cursor plugins, native Type-C WIP,
sanitizer tests, build scripts, offline binary audits and guarded deployment
tools. See [progress](../../docs/yoga/WIP.md) and [recovery](../../docs/yoga/RECOVERY.md).

Machine serials and original workspace paths in publication copies were redacted.
Disk deployment scripts consequently require explicit requalification and must
not be run by copying a command from an old test. Formatting/repartitioning
scripts are historical research, **not recovery instructions**.

Captured Apple drivers/kernels, personal logs, original configs, generated
objects and large per-boot evidence are not source files. Some tests require
those exact local inputs and will not run in a bare clone. Tests using mocked
rollback do not demonstrate a physical-device rollback.

The local `yoga-typec/` code installs no hooks by itself. Its Mach-O objects are
not a loadable kext. Do not enable stock WhateverXe alongside this NootedGreen
stack; the separate WEX snapshot is research only.
