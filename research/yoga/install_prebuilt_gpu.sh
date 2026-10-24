#!/bin/bash
# No compiler/Xcode/CLT dependency. Run only on the installed external Tahoe.
# Does not modify EFI, ACPI, boot arguments or unrelated device drivers.
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
HERE=$(cd "$(dirname "$0")" && pwd)
PB=/usr/libexec/PlistBuddy
EXPECTED_PARTUUID=7bb5254d-220e-4792-9421-1682c9f75718
fail() { echo "STOP: $*" >&2; exit 1; }
[ "$(uname -s)" = Darwin ] || fail 'This installer runs on Tahoe, not Linux. No changes made.'
[ "$(uname -m)" = x86_64 ] || fail 'Requires Intel x86_64.'
[ "$(id -u)" = 0 ] || fail 'Open Install GPU.command to authorize installation.'
[ "$(sw_vers -buildVersion)" = 25G241 ] || fail 'This payload is restricted to Tahoe build 25G241.'
[ -d "$HERE/payload" ] && [ -f "$HERE/SHA256SUMS" ] || fail 'Missing payload or checksum list.'
WORK=$(mktemp -d /private/tmp/yoga-xe-preflight.XXXXXX)
cleanup() {
    case "$WORK" in /private/tmp/yoga-xe-preflight.*) rm -rf "$WORK";; esac
}
trap cleanup EXIT
diskutil info -plist / > "$WORK/root.plist"
CONTAINER=$("$PB" -c 'Print :APFSContainerReference' "$WORK/root.plist")
diskutil apfs list -plist "$CONTAINER" > "$WORK/container.plist"
STORE=$("$PB" -c 'Print :Containers:0:PhysicalStores:0:DeviceIdentifier' "$WORK/container.plist")
if "$PB" -c 'Print :Containers:0:PhysicalStores:1' "$WORK/container.plist" >/dev/null 2>&1; then
    fail 'Multiple physical stores: refusing an ambiguous target.'
fi
diskutil info -plist "$STORE" > "$WORK/store.plist"
PARTUUID=$("$PB" -c 'Print :DiskUUID' "$WORK/store.plist" | tr '[:upper:]' '[:lower:]')
[ "$PARTUUID" = "$EXPECTED_PARTUUID" ] || fail 'Booted system is not on the authorized external target partition.'
INTERNAL=$("$PB" -c 'Print :Internal' "$WORK/store.plist")
[ "$INTERNAL" = false ] || fail 'Target is reported as internal. No installation attempted.'

echo 'Checking payload checksums...'
(cd "$HERE" && shasum -a 256 --status -c SHA256SUMS) || fail 'Payload checksum mismatch.'
echo 'All payload checksums passed.'
# Repair signatures with Apple's native tool on disposable copies, never on
# the checksum-protected originals or currently installed drivers. rcodesign
# left stale detached CodeDirectory data in the shared resource bundles.
PREPARED="$WORK/prepared"
ditto "$HERE/payload" "$PREPARED"
NATIVE_LOG="$HERE/native-signing-v2.log"
echo 'Preparing native ad-hoc signatures (built-in codesign; no Xcode)...'
if ! /bin/bash "$HERE/prepare_native_signatures.sh" "$PREPARED" > "$NATIVE_LOG" 2>&1; then
    cat "$NATIVE_LOG"
    fail "Native signing or strict verification failed before installing files. Log: $NATIVE_LOG"
fi
cat "$NATIVE_LOG"
echo 'Checking signatures with Apple codesign (no developer tools required)...'
for bundle in "$PREPARED"/Library/Extensions/* "$PREPARED"/Library/GPUBundles/* "$PREPARED"/System/Library/Caches/*; do
    /usr/bin/codesign --verify --deep --strict "$bundle" || fail "Signature rejected: $bundle"
done

# These paths are writable Data firmlinks on the reference Tahoe installation.
# Do not remount System or disable protections to force an unexpected layout.
for dir in /Library/Extensions /Library/GPUBundles /System/Library/Caches; do
    [ -d "$dir" ] && [ ! -L "$dir" ] || fail "Unexpected destination layout: $dir"
    [ -w "$dir" ] || fail "Destination not writable: $dir. No root remount will be attempted."
done
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP="/Library/Application Support/YogaXeGPU/backups/$STAMP"
mkdir -p "$BACKUP/original"
exec > >(tee "$BACKUP/install.log") 2>&1
echo "GPU-only backup: $BACKUP"
echo 'Wi-Fi, trackpad, audio, CPU and EFI are not modified.'
sw_vers > "$BACKUP/system-version.txt"
sysctl -n kern.bootargs > "$BACKUP/boot-args-before.txt"
csrutil status > "$BACKUP/csr-status.txt" 2>&1 || true
touch "$BACKUP/replaced-paths.txt" "$BACKUP/new-paths.txt"

# Back up all existing destinations before replacing the first bundle.
for category in Library/Extensions Library/GPUBundles System/Library/Caches; do
    for source in "$PREPARED/$category/"*; do
        rel="$category/$(basename "$source")"
        if [ -L "/$rel" ]; then fail "Destination bundle is a symlink: /$rel"; fi
        if [ -e "/$rel" ]; then
            mkdir -p "$BACKUP/original/$category"
            ditto "/$rel" "$BACKUP/original/$rel"
            printf '%s\n' "$rel" >> "$BACKUP/replaced-paths.txt"
        else
            printf '%s\n' "$rel" >> "$BACKUP/new-paths.txt"
        fi
    done
done
cp "$HERE/SHA256SUMS" "$BACKUP/payload-SHA256SUMS"
cp "$NATIVE_LOG" "$BACKUP/native-signing-v2.log"
cp "$HERE/restore_prebuilt_gpu.sh" "$BACKUP/restore_prebuilt_gpu.sh"
touch "$BACKUP/INSTALL-INCOMPLETE"
for category in Library/Extensions Library/GPUBundles System/Library/Caches; do
    for source in "$PREPARED/$category/"*; do
        dest="/$category/$(basename "$source")"
        # Remove only each named GPU destination, after it has been backed up.
        rm -rf "$dest"
        ditto "$source" "$dest"
        chown -R root:wheel "$dest"
        chmod -R u+rwX,go+rX,go-w "$dest"
        codesign --verify --deep --strict "$dest" || fail "Installed signature check failed: $dest; backup: $BACKUP"
    done
done
echo 'Registering the three required kexts using Tahoe kmutil...'
if ! /usr/bin/kmutil load -z --load-style match-only \
    -p /Library/Extensions/HookCase.kext \
    -p /Library/Extensions/AppleIntelTGLGraphics.kext \
    -p /Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext; then
    fail "kmutil did not complete. Preserve the log at $BACKUP/install.log. Do not treat this as a successful deployment."
fi
mv "$BACKUP/INSTALL-INCOMPLETE" "$BACKUP/FILES-INSTALLED-KMUTIL-RETURNED-SUCCESS"
echo 'Files installed; kmutil returned success. This does not prove the kexts are approved or loaded.'
echo 'Approve an extension in Privacy & Security if macOS requests it.'
echo 'No automatic reboot. The external EFI still requires a separate checked GPU review.'
echo "Backup and restore script: $BACKUP"
