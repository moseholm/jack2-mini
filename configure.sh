#!/bin/bash
# Configures the build in <repo>/build: RelWithDebInfo, with the tests. Run it from anywhere; the VS Code tasks call it.
set -e
cd "$(dirname "$0")"

# The ALSA and udev development files are needed. If the system has none, use the sysroot extracted from the
# packages; install them with: sudo apt install libasound2-dev libudev-dev
if ! pkg-config --exists alsa libudev; then
    SYSROOT=/tmp/opencode/sysroot
    if [ ! -d "$SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig" ]; then
        echo "libasound2-dev and libudev-dev are not installed: sudo apt install libasound2-dev libudev-dev" >&2
        exit 1
    fi
    export PKG_CONFIG_PATH="$SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
fi

exec cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJACK_BUILD_TESTS=ON
