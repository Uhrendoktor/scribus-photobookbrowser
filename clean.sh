#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

rm -rf "${SCRIPT_DIR}/dist"

docker image rm \
    scribus-photobook-browser:scribus-1.6.5 \
    2>/dev/null || true

docker image rm \
    scribus-photobook-browser:plugin \
    2>/dev/null || true

echo "Cleaned."