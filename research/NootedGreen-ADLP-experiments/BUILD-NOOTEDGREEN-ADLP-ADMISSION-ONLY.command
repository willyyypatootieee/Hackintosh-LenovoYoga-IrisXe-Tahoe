#!/bin/bash
# Build-only launcher for the ADL-P / Sequoia accelerator-admission test kext.
# It never installs a kext, modifies an EFI/KC/boot-args, or restarts macOS.
set -euo pipefail

pause() { printf '\nPress Return to close...'; read -r _ || true; }
stop() { printf '\nSTOP: %s\n' "$*" >&2; pause; exit 1; }
finish() { printf '\n%s\n' "$*"; pause; }

ROOT=$(cd "$(dirname "$0")" && pwd)
STAMP=$(date +%Y%m%d-%H%M%S)
OUT="$HOME/NootedGreen-ADLP-Sequoia-builds/24H32-admission-only-$STAMP"

[ "$(uname -m)" = x86_64 ] || stop 'Requires the intended x86_64 Mac.'
[ "$(sw_vers -productVersion)" = 15.8.1 ] || stop "Requires macOS 15.8.1; found $(sw_vers -productVersion)."
[ "$(sw_vers -buildVersion)" = 24H32 ] || stop "Requires build 24H32; found $(sw_vers -buildVersion)."
command -v xcodebuild >/dev/null 2>&1 || stop 'Xcode/xcodebuild is unavailable.'
[ -f "$ROOT/NootedGreen.xcodeproj/project.pbxproj" ] || stop 'Run from the complete NootedGreen source package.'
[ -d "$ROOT/MacKernelSDK" ] || stop 'MacKernelSDK is missing from the source package.'

mkdir -p "$OUT"
{
    echo '== NootedGreen ADL-P Sequoia admission-only build =='
    echo 'Purpose: add only the opt-in -ngreenadlpadmit gate.'
    echo 'This artifact does not enable RPL compatibility, full-Metal, scheduler, or topology overrides.'
    date -u
    sw_vers
    xcodebuild -version
    echo "source=$ROOT"
    git -C "$ROOT" rev-parse HEAD 2>&1 || true
    git -C "$ROOT" status --short 2>&1 || true
    git -C "$ROOT" diff --check 2>&1 || true
} | tee "$OUT/BUILD-INFO.txt"

xcodebuild \
    -project "$ROOT/NootedGreen.xcodeproj" \
    -target NootedGreen \
    -configuration Release \
    -sdk macosx \
    -arch x86_64 \
    CODE_SIGN_IDENTITY=- \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    CONFIGURATION_BUILD_DIR="$OUT" \
    build 2>&1 | tee "$OUT/build.log"

KEXT="$OUT/NootedGreen.kext"
BIN="$KEXT/Contents/MacOS/NootedGreen"
[ -f "$BIN" ] || stop "Build produced no kext binary; inspect $OUT/build.log"
plutil -lint "$KEXT/Contents/Info.plist" | tee "$OUT/plutil.log"
file "$BIN" | tee "$OUT/filetype.txt"
lipo -archs "$BIN" | tee "$OUT/architectures.txt"
lipo -archs "$BIN" | grep -Fx x86_64 >/dev/null || stop 'Build is not x86_64.'
shasum -a 256 "$BIN" | tee "$OUT/NootedGreen.sha256"
ditto -c -k --sequesterRsrc --keepParent "$KEXT" "$OUT/NootedGreen-ADLP-admission-only.kext.zip"
shasum -a 256 "$OUT/NootedGreen-ADLP-admission-only.kext.zip" | tee -a "$OUT/NootedGreen.sha256"

finish "BUILD PASSED. Output: $OUT\n\nThis kext is not installed. Record BUILD-INFO.txt and NootedGreen.sha256 before creating a separate external-EFI test profile."
