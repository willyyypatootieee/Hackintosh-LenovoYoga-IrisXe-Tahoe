#!/bin/bash
# Run as root on the same external Tahoe installation, from its backup folder.
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(uname -s)" = Darwin ] && [ "$(id -u)" = 0 ] || exit 1
case "$HERE" in '/Library/Application Support/YogaXeGPU/backups/'*) ;; *) echo 'Unexpected backup location'; exit 1;; esac
[ -f "$HERE/replaced-paths.txt" ] && [ -f "$HERE/new-paths.txt" ] || exit 1
PB=/usr/libexec/PlistBuddy
WORK=$(mktemp -d /private/tmp/yoga-xe-restore.XXXXXX)
trap 'rm -f "$WORK/root.plist" "$WORK/container.plist" "$WORK/store.plist"; rmdir "$WORK" 2>/dev/null || true' EXIT
diskutil info -plist / > "$WORK/root.plist"
CONTAINER=$("$PB" -c 'Print :APFSContainerReference' "$WORK/root.plist")
diskutil apfs list -plist "$CONTAINER" > "$WORK/container.plist"
STORE=$("$PB" -c 'Print :Containers:0:PhysicalStores:0:DeviceIdentifier' "$WORK/container.plist")
if "$PB" -c 'Print :Containers:0:PhysicalStores:1' "$WORK/container.plist" >/dev/null 2>&1; then exit 1; fi
diskutil info -plist "$STORE" > "$WORK/store.plist"
PARTUUID=$("$PB" -c 'Print :DiskUUID' "$WORK/store.plist" | tr '[:upper:]' '[:lower:]')
[ "$PARTUUID" = 7bb5254d-220e-4792-9421-1682c9f75718 ] || exit 1
[ "$("$PB" -c 'Print :Internal' "$WORK/store.plist")" = false ] || exit 1
# Only paths from the fixed GPU allowlist may be removed or restored.
allowed() {
    case "$1" in
        Library/Extensions/HookCase.kext|Library/Extensions/AppleIntelTGLGraphics.kext|Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext|Library/Extensions/AppleIntelTGLGraphicsMTLDriver.bundle|Library/Extensions/AppleIntelTGLGraphicsGLDriver.bundle|Library/Extensions/AppleIntelTGLGraphicsVADriver.bundle|Library/Extensions/AppleIntelTGLGraphicsVAME.bundle|Library/Extensions/AppleIntelGraphicsShared.bundle|Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle|Library/GPUBundles/AppleIntelGraphicsShared.bundle|System/Library/Caches/xe.bundle) return 0;;
        *) return 1;;
    esac
}
for list in replaced-paths new-paths; do
    while IFS= read -r rel; do allowed "$rel" || exit 1; done < "$HERE/$list.txt"
done
while IFS= read -r rel; do
    [ -d "$HERE/original/$rel" ] || exit 1
done < "$HERE/replaced-paths.txt"
while IFS= read -r rel; do
    rm -rf "/$rel"
    ditto "$HERE/original/$rel" "/$rel"
done < "$HERE/replaced-paths.txt"
while IFS= read -r rel; do rm -rf "/$rel"; done < "$HERE/new-paths.txt"
/usr/bin/kmutil load -z --load-style match-only
echo 'Previous GPU files restored. No EFI changes and no automatic reboot.'
