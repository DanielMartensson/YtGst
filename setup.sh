#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Qt installation root. Override with, for example: QT_PREFIX=/opt/Qt ./setup.sh
QT_PREFIX=${QT_PREFIX:-/home/mint/Qt}

QT_DIR=$(ls -d "$QT_PREFIX"/6.*/gcc_64 2>/dev/null | sort -V | tail -1 || true)
if [ -z "$QT_DIR" ]; then
    echo "error: no Qt installation under $QT_PREFIX/6.*/gcc_64 found" >&2
    exit 1
fi
echo "Qt: $QT_DIR"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR"
cmake --build build -j"$(nproc)"

echo
echo "Built: build/ytgst  (run with ./run.sh)"