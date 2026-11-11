#!/usr/bin/env bash

set -euo pipefail

SCRIBUS_VERSION="${SCRIBUS_VERSION:-1.7.3}"
IMAGE="scribus-photobook-browser:scribus-${SCRIBUS_VERSION}"
BUILD_JOBS="${BUILD_JOBS:-4}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "${SCRIBUS_VERSION}" in
    1.6.*) UBUNTU_VERSION="22.04" ;;
    1.7.*) UBUNTU_VERSION="24.04" ;;
    *) echo "Unsupported Scribus version: ${SCRIBUS_VERSION}" >&2; exit 2 ;;
esac

mkdir -p "${SCRIPT_DIR}/dist"

echo "Building Scribus ${SCRIBUS_VERSION} Docker image (Ubuntu ${UBUNTU_VERSION})..."
docker build \
    --build-arg "UBUNTU_VERSION=${UBUNTU_VERSION}" \
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
