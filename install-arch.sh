#!/bin/bash
# Build & install on Arch (and Arch-based: CachyOS, EndeavourOS, Manjaro).
# Builds the C++ wallpaper plugin from source (so the DayNightMonitor and
# any other custom changes in this fork are included) and installs the
# Plasma 6 wallpaper package both system-wide and to the user's
# ~/.local/share/plasma/wallpapers directory.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$REPO_DIR/source"
BUILD_DIR="$SOURCE_DIR/build"

echo ">>> Installing build dependencies (sudo required)"
sudo pacman -S --needed --noconfirm \
    base-devel cmake ninja extra-cmake-modules \
    qt6-declarative qt6-websockets qt6-webchannel \
    libplasma kpackage \
    mpv lz4 vulkan-headers \
    gst-libav

echo ">>> Initializing git submodules"
git -C "$REPO_DIR" submodule update --init --force --recursive

echo ">>> Configuring CMake"
rm -rf "$BUILD_DIR"
cmake -B "$BUILD_DIR" -S "$SOURCE_DIR" -DCMAKE_BUILD_TYPE=Release -G Ninja

echo ">>> Building"
cmake --build "$BUILD_DIR"

echo ">>> Installing system-wide (sudo required)"
sudo cmake --install "$BUILD_DIR"

echo ">>> Installing wallpaper package to user-local Plasma directory"
if kpackagetool6 -t Plasma/Wallpaper -l 2>/dev/null | grep -q '^com\.github\.catsout\.wallpaperEngineKde$'; then
    kpackagetool6 -t Plasma/Wallpaper -u "$SOURCE_DIR/plugin"
else
    kpackagetool6 -t Plasma/Wallpaper -i "$SOURCE_DIR/plugin"
fi

echo ">>> Restarting plasmashell"
systemctl --user restart plasma-plasmashell.service

echo ">>> Done. Open Desktop Settings → Wallpaper → Wallpaper Engine."
