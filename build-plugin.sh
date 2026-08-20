#!/usr/bin/env bash

set -euo pipefail

IMAGE="scribus-photobook-browser:plugin"
BUILD_JOBS="${BUILD_JOBS:-4}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIBUS_VERSION="${SCRIBUS_VERSION:-1.6.5}"
VERSION="$(grep -oP 'about->version\s*=\s*QStringLiteral\("([^"]+)"\);' "${SCRIPT_DIR}/plugin/photobookbrowserplugin.cpp" | head -n 1 | cut -d '"' -f 2)"

mkdir -p "${SCRIPT_DIR}/dist"

echo "Building Docker image..."
docker build \
    --build-arg "BUILD_JOBS=${BUILD_JOBS}" \
    --build-arg "SCRIBUS_VERSION=${SCRIBUS_VERSION}" \
    --build-arg "VERSION=${VERSION}" \
    --tag "${IMAGE}" \
    -f "${SCRIPT_DIR}/Dockerfile.plugin" \
    "${SCRIPT_DIR}"

echo
echo "Running build container..."

docker run \
    --rm \
    --user "$(id -u):$(id -g)" \
    --volume "${SCRIPT_DIR}/dist:/workspace/dist" \
    "${IMAGE}"

echo
echo "=============================================="
echo "Build finished"
echo "=============================================="
echo

find "${SCRIPT_DIR}/dist" -maxdepth 1 -type f -print