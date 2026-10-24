#!/bin/bash
# Repair only the defective v2 HookCase. No builds, EFI or peripheral writes.
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
HERE=$(cd "$(dirname "$0")" && pwd)
PB=/usr/libexec/PlistBuddy
fail() { echo "STOP: $*" >&2; exit 1; }
[ "$(uname -s)" = Darwin ] || fail 'Run only on the installed external Tahoe. No changes made.'
[ "$(uname -m)" = x86_64 ] && [ "$(id -u)" = 0 ] || fail 'Use Repair HookCase.command to authorize.'
[ "$(sw_vers -buildVersion)" = 25G241 ] || fail 'Requires Tahoe build 25G241.'
WORK=$(mktemp -d /private/tmp/yoga-xe-hookcase-v3.XXXXXX)
cleanup() { case "$WORK" in /private/tmp/yoga-xe-hookcase-v3.*) rm -rf "$WORK";; esac; }
trap cleanup EXIT
diskutil info -plist / > "$WORK/root.plist"
CONTAINER=$("$PB" -c 'Print :APFSContainerReference' "$WORK/root.plist")
diskutil apfs list -plist "$CONTAINER" > "$WORK/container.plist"
STORE=$("$PB" -c 'Print :Containers:0:PhysicalStores:0:DeviceIdentifier' "$WORK/container.plist")
if "$PB" -c 'Print :Containers:0:PhysicalStores:1' "$WORK/container.plist" >/dev/null 2>&1; then
    fail 'Multiple physical stores; refusing ambiguous target.'
fi
diskutil info -plist "$STORE" > "$WORK/store.plist"
PARTUUID=$("$PB" -c 'Print :DiskUUID' "$WORK/store.plist" | tr '[:upper:]' '[:lower:]')
[ "$PARTUUID" = 7bb5254d-220e-4792-9421-1682c9f75718 ] || fail 'Not the authorized external installation.'
[ "$("$PB" -c 'Print :Internal' "$WORK/store.plist")" = false ] || fail 'Target is internal.'
INITIAL='/Library/Application Support/YogaXeGPU/backups/20261023-221106'
[ -f "$INITIAL/INSTALL-INCOMPLETE" ] && [ -f "$INITIAL/install.log" ] || fail 'Expected incomplete v2 backup is missing. Nothing changed.'
grep -q '___cxa_atexit' "$INITIAL/install.log" || fail 'Prior failure does not match this targeted repair.'
[ -d /Library/Extensions ] && [ ! -L /Library/Extensions ] && [ -w /Library/Extensions ] || fail 'Unexpected Extensions destination.'
DEST=/Library/Extensions/HookCase.kext
[ -d "$DEST" ] && [ ! -L "$DEST" ] || fail 'Installed HookCase missing or a symlink.'
[ "$("$PB" -c 'Print :CFBundleIdentifier' "$DEST/Contents/Info.plist")" = org.smichaud.HookCase ] || fail 'Unexpected installed HookCase identifier.'
echo 'Checking repair archive checksums...'
(cd "$HERE" && shasum -a 256 --status -c SHA256SUMS) || fail 'Repair checksum mismatch.'
# Verify the already-installed v2 stack; do not reinstall or re-sign it.
for bundle in /Library/Extensions/AppleIntelGraphicsShared.bundle \
    /Library/Extensions/AppleIntelTGLGraphics.kext \
    /Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext \
    /Library/Extensions/AppleIntelTGLGraphicsGLDriver.bundle \
    /Library/Extensions/AppleIntelTGLGraphicsMTLDriver.bundle \
    /Library/Extensions/AppleIntelTGLGraphicsVADriver.bundle \
    /Library/Extensions/AppleIntelTGLGraphicsVAME.bundle \
    /Library/GPUBundles/AppleIntelGraphicsShared.bundle \
    /Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle \
    /System/Library/Caches/xe.bundle; do
    [ -d "$bundle" ] && [ ! -L "$bundle" ] || fail "Expected v2 bundle missing or symlinked: $bundle"
    codesign --verify --deep --strict "$bundle" || fail "Installed v2 signature rejected: $bundle"
done
PREPARED="$WORK/HookCase.kext"
ditto "$HERE/payload/HookCase.kext" "$PREPARED"
codesign --force --sign - --timestamp=none "$PREPARED/Contents/MacOS/HookCase"
codesign --force --sign - --timestamp=none "$PREPARED"
codesign --verify --deep --strict --verbose=2 "$PREPARED" || fail 'Corrected HookCase native signature verification failed.'
BACKUP="/Library/Application Support/YogaXeGPU/repairs/$(date +%Y%m%d-%H%M%S)-hookcase-v3"
[ ! -e "$BACKUP" ] || fail 'Repair backup already exists.'
mkdir -p "$BACKUP"
exec > >(tee "$BACKUP/repair.log") 2>&1
echo "Original pre-GPU backup preserved untouched: $INITIAL"
echo "This repair backs up and replaces ONLY: $DEST"
echo "Repair log and previous HookCase: $BACKUP"
ditto "$DEST" "$BACKUP/HookCase-before.kext"
printf '%s\n' "$INITIAL" > "$BACKUP/original-v2-backup.txt"
cp "$HERE/SHA256SUMS" "$BACKUP/repair-SHA256SUMS"
touch "$BACKUP/REPAIR-INCOMPLETE"
rm -rf "$DEST"
ditto "$PREPARED" "$DEST"
chown -R root:wheel "$DEST"
chmod -R u+rwX,go+rX,go-w "$DEST"
codesign --verify --deep --strict "$DEST" || fail "Installed corrected HookCase signature rejected. Log: $BACKUP/repair.log"
echo 'Registering corrected HookCase plus the two already-installed TGL kexts...'
KMUTIL_STATUS=0
if /usr/bin/kmutil load -z --load-style match-only \
    -p /Library/Extensions/HookCase.kext \
    -p /Library/Extensions/AppleIntelTGLGraphics.kext \
    -p /Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext > "$BACKUP/kmutil.log" 2>&1; then
    KMUTIL_STATUS=0
else
    KMUTIL_STATUS=$?
fi
cat "$BACKUP/kmutil.log"
printf '%s\n' "$KMUTIL_STATUS" > "$BACKUP/kmutil-exit-status.txt"
# A nonzero reboot-required result is not a link failure. Accept ONLY the
# exact standalone request naming our three extensions, never a substring in
# mixed/error output. Preserve all other nonzero results as failures.
KMUTIL_MESSAGE=$(sed '/^[[:space:]]*$/d' "$BACKUP/kmutil.log")
EXPECTED_REBOOT='Loading extension(s): org.smichaud.HookCase,com.xxxxx.driver.AppleIntelTGLGraphics,com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer requires a reboot'
if [ "$KMUTIL_STATUS" -eq 0 ]; then
    mv "$BACKUP/REPAIR-INCOMPLETE" "$BACKUP/KMUTIL-RETURNED-SUCCESS"
    echo 'SUCCESS: corrected HookCase installed; kmutil returned success.'
elif [ "$KMUTIL_MESSAGE" = "$EXPECTED_REBOOT" ]; then
    mv "$BACKUP/REPAIR-INCOMPLETE" "$BACKUP/KMUTIL-REQUIRES-REBOOT"
    echo 'REBOOT REQUIRED: corrected HookCase installed; macOS requests a restart to load the three extensions.'
else
    fail "kmutil failed (exit $KMUTIL_STATUS). Do not reboot for a GPU test. Full log: $BACKUP/repair.log. Original backup remains $INITIAL."
fi
echo 'This is not yet proof of GPU loading or Metal acceleration.'
echo 'Approve extensions in Privacy & Security if requested, then restart directly into this external Tahoe.'
echo 'No automatic reboot. No EFI, CPU, Wi-Fi, trackpad, audio or USB changes.'
