#!/bin/bash
set -e

SYSROOT=/tmp/arm-sysroot
BUILD_DIR=/tmp/systemd-stable/build

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Read available options from meson_options.txt and disable everything we don't need
MESON_OPTS=""
while IFS= read -r opt; do
    name=$(echo "$opt" | grep -oP "option\('\K[^']+")
    case "$name" in
        mode|static-libsystemd|tests|skip-deps|version-tag|shared-lib-tag|split-usr|split-bin|rootlibdir|rootprefix|link-udev-shared|link-systemctl-shared|link-networkd-shared|link-timesyncd-shared|link-journalctl-shared|link-boot-shared|first-boot-full-preset|rc-local|telinit-path|sysvinit-path|sysvrcnd-path|bashcompletiondir|zshcompletiondir|default-hierarchy|debug-shell|debug-tty|ok-color|urlify)
            # Keep defaults for these
            ;;
        *)
            # Disable everything else
            MESON_OPTS="$MESON_OPTS -D${name}=false"
            ;;
    esac
done < <(grep "^option(" /tmp/systemd-stable/meson_options.txt)

# Force our specific settings
MESON_OPTS="$MESON_OPTS -Dstatic-libsystemd=pic -Dmode=release -Dtests=false -Dskip-deps=true"
MESON_OPTS="$MESON_OPTS -Dlink-udev-shared=false -Dlink-systemctl-shared=false"
MESON_OPTS="$MESON_OPTS -Dlink-networkd-shared=false -Dlink-timesyncd-shared=false"
MESON_OPTS="$MESON_OPTS -Dlink-journalctl-shared=false -Dlink-boot-shared=false"

echo "=== Meson options ==="
echo "$MESON_OPTS"
echo "====================="

meson setup "$BUILD_DIR" /tmp/systemd-stable \
    --cross-file /tmp/arm-cross.txt \
    --prefix="$SYSROOT" \
    $MESON_OPTS \
    2>&1 | tail -100

echo "=== Meson configure done ==="
