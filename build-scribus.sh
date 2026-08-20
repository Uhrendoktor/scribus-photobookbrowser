#!/usr/bin/env bash

set -euo pipefail

SCRIBUS_VERSION="${SCRIBUS_VERSION:-1.6.5}"
IMAGE="scribus-photobook-browser:scribus-${SCRIBUS_VERSION}"
BUILD_JOBS="${BUILD_JOBS:-4}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "${SCRIPT_DIR}/dist"

echo "Building Docker image..."
docker build \
    --build-arg "BUILD_JOBS=${BUILD_JOBS}" \
    --build-arg "SCRIBUS_VERSION=${SCRIBUS_VERSION}" \
    --tag "${IMAGE}" \
    -f "${SCRIPT_DIR}/Dockerfile.scribus" \
    "${SCRIPT_DIR}"

echo
echo "Running build container..."

docker run \
    --rm \
    --user "$(id -u):$(id -g)" \
    --volume "${SCRIPT_DIR}/dist:/workspace/dist" \
    "${IMAGE}"
