#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

BIN=build/ytgst
if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found – run ./setup.sh first" >&2
    exit 1
fi

# Pick up the directory holding the Qt libraries the binary was built with, so
# GStreamer's qml6glsink plugin binds to that same Qt instead of the distro's.
QT_LIBDIR=$(ldd "$BIN" | awk '/libQt6Core\.so\.6/ {print $3; exit}')
if [ -n "$QT_LIBDIR" ]; then
    QT_LIBDIR=$(dirname "$QT_LIBDIR")
    export LD_LIBRARY_PATH="$QT_LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

exec "$BIN" "$@"