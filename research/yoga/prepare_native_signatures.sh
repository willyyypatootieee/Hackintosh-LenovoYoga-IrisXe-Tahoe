#!/bin/bash
# Runs only on a disposable preflight copy. Native ad-hoc signing, not a build.
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
[ "$(uname -s)" = Darwin ] || { echo 'Native signing requires macOS.' >&2; exit 1; }
ROOT=${1:?Prepared payload directory required}
case "$ROOT" in /private/tmp/yoga-xe-preflight.*/prepared) ;; *) echo 'Refusing non-temporary payload path' >&2; exit 1;; esac
[ -d "$ROOT/Library/Extensions" ] && [ -d "$ROOT/System/Library/Caches/xe.bundle" ]
for bundle in "$ROOT"/Library/Extensions/* "$ROOT"/Library/GPUBundles/* "$ROOT"/System/Library/Caches/*; do
    echo "Native signing: ${bundle#"$ROOT/"}"
    # Sign nested binaries before sealing the container. Enumerate only the
    # known direct MacOS children; resources are not executable code.
    for binary in "$bundle"/Contents/MacOS/*; do
        [ -f "$binary" ] && [ ! -L "$binary" ] || continue
        /usr/bin/codesign --force --sign - --timestamp=none "$binary"
        /usr/bin/codesign --verify --strict "$binary"
    done
    /usr/bin/codesign --force --sign - --timestamp=none "$bundle"
    /usr/bin/codesign --verify --deep --strict --verbose=2 "$bundle"
done
echo 'Native signing and strict verification passed for all 11 GPU bundles.'
