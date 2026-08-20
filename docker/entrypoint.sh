#!/usr/bin/env bash

set -euo pipefail

SCRIBUS_VERSION="${1}"
PLUGIN_VERSION="${2}"

INSTALL_ROOT="/opt/scribus-install"
OUTPUT_ROOT="/workspace/dist"

mkdir -p "${OUTPUT_ROOT}"

echo
echo "=============================================="
echo " Scribus PhotoBook Browser build"
echo "=============================================="
echo

echo "Scribus installation:"
find "${INSTALL_ROOT}" -maxdepth 4 -type f -name 'scribus' -print || true

echo
echo "Plugin files:"
find "${INSTALL_ROOT}" -type f \
    \( -name 'photobookbrowser.so' \
       -o -name '*photobookbrowser*.so' \) \
    -print

echo

PLUGIN="$(find "${INSTALL_ROOT}" -type f \
    \( -name 'photobookbrowser.so' \
       -o -name '*photobookbrowser*.so' \) \
    | head -n 1)"

if [[ -z "${PLUGIN}" ]]; then
    echo "ERROR: photobookbrowser plugin was not built."
    exit 1
fi

ARCH="$(uname -m)"

OUT="${OUTPUT_ROOT}/photobookbrowser-${PLUGIN_VERSION}-scribus-${SCRIBUS_VERSION}-linux-${ARCH}.so"

cp "${PLUGIN}" "${OUT}"

echo "Created:"
echo "  ${OUT}"

echo
file "${OUT}"

echo
echo "Done."