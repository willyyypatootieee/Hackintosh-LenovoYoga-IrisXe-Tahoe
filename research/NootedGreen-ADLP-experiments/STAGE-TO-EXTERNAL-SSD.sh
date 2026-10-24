#!/bin/bash
# Copies the local source/evidence package to an already mounted external SSD.
# Refuses non-mounted, non-writable, and internal-NVMe destinations.
set -euo pipefail

SOURCE=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-}

[ -n "$DEST" ] || { echo "Usage: $0 /mounted/external/volume" >&2; exit 2; }
[ -d "$DEST" ] || { echo "STOP: destination is not a directory: $DEST" >&2; exit 1; }
[ -w "$DEST" ] || { echo "STOP: destination is not writable: $DEST" >&2; exit 1; }

MOUNT=$(findmnt -no TARGET -T "$DEST")
SOURCE_DEVICE=$(findmnt -no SOURCE -T "$DEST")
case "$SOURCE_DEVICE" in
    /dev/nvme0n1*) echo "STOP: refusing internal NVMe destination: $SOURCE_DEVICE" >&2; exit 1 ;;
esac

STAMP=$(date +%Y%m%d-%H%M%S)
OUT="$MOUNT/NootedGreen-ADLP-Sequoia-24H32-$STAMP"
mkdir -p "$OUT"

tar \
    --exclude-vcs \
    --exclude='build' \
    --exclude='DerivedData' \
    --exclude='.DS_Store' \
    -C "$(dirname "$SOURCE")" \
    -cf - "$(basename "$SOURCE")" | tar -C "$OUT" -xf -

# Do not include the manifest itself: it is created by this pipeline and would
# necessarily hash differently once its own entry has been appended.
find "$OUT" -type f ! -name 'SHA256SUMS.txt' -print0 | sort -z | xargs -0 sha256sum > "$OUT/SHA256SUMS.txt"
printf 'staged_source=%s\nstaged_destination=%s\nsource_device=%s\n' "$SOURCE" "$OUT" "$SOURCE_DEVICE" > "$OUT/STAGE-INFO.txt"
echo "Staged package: $OUT"
