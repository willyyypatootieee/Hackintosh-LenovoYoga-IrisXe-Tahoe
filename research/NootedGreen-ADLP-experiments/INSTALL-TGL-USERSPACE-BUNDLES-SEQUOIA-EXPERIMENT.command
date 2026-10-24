#!/bin/bash
# Stages donor TGL user-space GPU bundles for the external Sequoia experiment.
# Default mode is a read-only preflight.  --apply copies only the listed bundles
# to /Library/Extensions after preserving any pre-existing copies.  It does not
# touch EFI, injected kexts, kernel collections, boot arguments, or NVRAM.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
SRC="$ROOT/sle_Internal/sle"
DEST=/Library/Extensions
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP="$HOME/NootedGreen-ADLP-Sequoia-bundle-backups/$STAMP"
APPLY=0

if [ "${1:-}" = "--apply" ]; then
    APPLY=1
elif [ -n "${1:-}" ]; then
    echo "Usage: $0 [--apply]" >&2
    exit 2
fi

[ "$(uname -m)" = x86_64 ] || { echo 'STOP: intended only for x86_64 macOS.' >&2; exit 1; }
[ "$(sw_vers -productVersion)" = 15.8.1 ] || { echo "STOP: requires macOS 15.8.1; found $(sw_vers -productVersion)." >&2; exit 1; }
[ "$(sw_vers -buildVersion)" = 24H32 ] || { echo "STOP: requires Sequoia build 24H32; found $(sw_vers -buildVersion)." >&2; exit 1; }
[ -d "$SRC" ] || { echo "STOP: bundle source directory is missing: $SRC" >&2; exit 1; }
[ -d "$DEST" ] || { echo "STOP: destination is missing: $DEST" >&2; exit 1; }

BUNDLES=(
    AppleIntelGraphicsShared.bundle
    AppleIntelTGLGraphicsMTLDriver.bundle
    AppleIntelTGLGraphicsGLDriver.bundle
    AppleIntelTGLGraphicsVADriver.bundle
    AppleIntelTGLGraphicsVAME.bundle
)

echo '== TGL userspace bundle preflight =='
echo "source=$SRC"
echo "destination=$DEST"
echo "mode=$([ "$APPLY" -eq 1 ] && echo apply || echo read-only)"
echo 'physical_gpu=8086:46A6 revision 0C (unchanged; 9A49 is a donor-driver persona only)'
echo

for bundle in "${BUNDLES[@]}"; do
    path="$SRC/$bundle"
    [ -d "$path" ] || { echo "STOP: required source bundle is missing: $path" >&2; exit 1; }
    [ -f "$path/Contents/Info.plist" ] || { echo "STOP: malformed source bundle: $path" >&2; exit 1; }
    [ -d "$path/Contents/MacOS" ] || { echo "STOP: no executable directory: $path" >&2; exit 1; }
    printf 'SOURCE  %s  ' "$bundle"
    /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$path/Contents/Info.plist" 2>/dev/null || true
    shasum -a 256 "$path/Contents/Info.plist"
done

if [ "$APPLY" -ne 1 ]; then
    cat <<'EOF'

No files were changed. To copy only these five user-space bundles to the
running external Sequoia installation, rerun with:

  ./INSTALL-TGL-USERSPACE-BUNDLES-SEQUOIA-EXPERIMENT.command --apply

That operation needs an administrator password, backs up same-named existing
bundles under your home directory, and requires a reboot before a clean Metal
test. It does not rebuild a kernel collection because these are user-space
bundles, not injected kernel kexts.
EOF
    exit 0
fi

mkdir -p "$BACKUP"
echo
echo "Backup directory: $BACKUP"

for bundle in "${BUNDLES[@]}"; do
    if [ -e "$DEST/$bundle" ]; then
        echo "Backing up existing $DEST/$bundle"
        sudo ditto "$DEST/$bundle" "$BACKUP/$bundle"
    fi
done

for bundle in "${BUNDLES[@]}"; do
    echo "Installing $bundle"
    sudo rm -rf "$DEST/$bundle"
    sudo ditto "$SRC/$bundle" "$DEST/$bundle"
    sudo chown -R root:wheel "$DEST/$bundle"
    sudo chmod -R go-w "$DEST/$bundle"
done

{
    echo "installed_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "source=$SRC"
    echo "destination=$DEST"
    echo "backup=$BACKUP"
    echo 'bundles='
    for bundle in "${BUNDLES[@]}"; do
        shasum -a 256 "$DEST/$bundle/Contents/Info.plist"
    done
} | tee "$BACKUP/INSTALL-INFO.txt"

cat > "$BACKUP/RESTORE-PREEXISTING-BUNDLES.sh" <<'EOF'
#!/bin/bash
# Restores the bundle state captured before one explicit experimental install.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=/Library/Extensions
BUNDLES=(
    AppleIntelGraphicsShared.bundle
    AppleIntelTGLGraphicsMTLDriver.bundle
    AppleIntelTGLGraphicsGLDriver.bundle
    AppleIntelTGLGraphicsVADriver.bundle
    AppleIntelTGLGraphicsVAME.bundle
)

for bundle in "${BUNDLES[@]}"; do
    sudo rm -rf "$DEST/$bundle"
    if [ -d "$HERE/$bundle" ]; then
        sudo ditto "$HERE/$bundle" "$DEST/$bundle"
        sudo chown -R root:wheel "$DEST/$bundle"
        sudo chmod -R go-w "$DEST/$bundle"
        echo "Restored prior $bundle"
    else
        echo "Removed experimental $bundle (none existed before this install)"
    fi
done
echo 'Restore complete. Reboot before testing graphics again.'
EOF
chmod 700 "$BACKUP/RESTORE-PREEXISTING-BUNDLES.sh"

echo
echo 'Installed donor user-space bundles. Reboot from the external experimental EFI,'
echo 'then run COLLECT-NOOTEDGREEN-ADLP-SEQUOIA.command --metal-probe after login.'
echo "To undo this userspace install: $BACKUP/RESTORE-PREEXISTING-BUNDLES.sh"
