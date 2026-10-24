#!/bin/bash
# Installs the captured Yoga GPU stack with repaired HookCase.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
echo 'Yoga Iris Xe — GPU installer (repaired working-stack payload)'
echo 'Requires the installed external Tahoe 25G241. No EFI or peripheral changes.'
echo 'Payload captured from my accelerated Yoga, including repaired HookCase.'
echo 'This consolidated installer still needs a fresh macOS installation test.'
echo 'It will stop if preflight checks fail, and will not reboot automatically.'
printf 'Install the GPU payload with a backup? Type INSTALL: '
read -r answer
[ "$answer" = INSTALL ] || exit 0
sudo /bin/bash "$HERE/install_prebuilt_gpu.sh"
printf '\nPress Return to close. '
read -r answer
