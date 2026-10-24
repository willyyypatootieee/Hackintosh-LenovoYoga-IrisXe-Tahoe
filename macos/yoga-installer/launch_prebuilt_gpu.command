#!/bin/bash
# Uses only macOS built-in tools; no Xcode, CLT, Homebrew or compilation.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
echo 'Yoga Iris Xe — prebuilt GPU installer v2 (native signature repair)'
echo 'Requires the installed external Tahoe 25G241. No EFI or peripheral changes.'
echo 'This build has not yet been validated by macOS codesign/kmutil or a live GPU test.'
echo 'It will stop if preflight checks fail, and will not reboot automatically.'
printf 'Install the GPU payload with a backup? Type INSTALL: '
read -r answer
[ "$answer" = INSTALL ] || exit 0
sudo /bin/bash "$HERE/install_prebuilt_gpu.sh"
printf '\nPress Return to close. '
read -r answer
