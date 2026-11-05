#!/bin/sh
# Builds keychain-manager-<version>-<arch>.AppImage. Runs inside an Ubuntu 22.04 container (glibc 2.35: the AppImage
# then runs on Ubuntu 22.04+, Debian 12+ and anything newer); SDL 3 is built here and bundled. libdbus and libssl
# come from the system (linuxdeploy leaves the session bus library to the host).
#   podman run --rm -v "$PWD":/src -w /src ubuntu:22.04 sh packaging/appimage/build.sh     (from the repo root)
# Env: SDL_TAG (default: newest release-3.x tag), VERSION (default: from src/app.c), OUT (default: ./dist).
set -eu
ARCH=$(uname -m)
DEPS=/opt/km-deps
OUT=${OUT:-$PWD/dist}
VERSION=${VERSION:-$(sed -n 's/^#define APP_VERSION "\(.*\)".*/\1/p' src/app.c)}
export DEBIAN_FRONTEND=noninteractive

echo "== packages"
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config git curl ca-certificates file \
  desktop-file-utils libfreetype-dev libfontconfig1-dev libssl-dev libdbus-1-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libvulkan-dev libdrm-dev libgbm-dev \
  libdecor-0-dev libudev-dev >/dev/null

echo "== SDL 3"
if [ -z "${SDL_TAG:-}" ]; then
  SDL_TAG=$(git ls-remote --tags --refs https://github.com/libsdl-org/SDL 'release-3.*' | sed 's|.*refs/tags/||' |
            grep -E '^release-3\.[0-9]+\.[0-9]+$' | sort -t. -k2,2n -k3,3n | tail -1)
fi
echo "   $SDL_TAG"
rm -rf /tmp/sdl && git clone -q --depth 1 -b "$SDL_TAG" https://github.com/libsdl-org/SDL /tmp/sdl
cmake -S /tmp/sdl -B /tmp/sdl/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEPS -DCMAKE_INSTALL_LIBDIR=lib \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF \
  -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF >/dev/null
cmake --build /tmp/sdl/build >/dev/null
cmake --install /tmp/sdl/build >/dev/null

echo "== keychain-manager $VERSION"
export PKG_CONFIG_PATH=$DEPS/lib/pkgconfig
rm -rf build AppDir
CFLAGS_PKG="-O2 -g0" LDFLAGS_PKG="-Wl,-rpath,\$ORIGIN/../lib" ./build.sh package
DESTDIR=$PWD/AppDir PREFIX=/usr ./install.sh >/dev/null

echo "== AppImage"
curl -fsSLo /tmp/linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
chmod +x /tmp/linuxdeploy
mkdir -p "$OUT"
export APPIMAGE_EXTRACT_AND_RUN=1 LD_LIBRARY_PATH=$DEPS/lib
export OUTPUT="$OUT/keychain-manager-$VERSION-$ARCH.AppImage"
export LDAI_UPDATE_INFORMATION="gh-releases-zsync|AlexMedela|keychain-manager|latest|keychain-manager-*-$ARCH.AppImage.zsync"
cd "$OUT"
/tmp/linuxdeploy --appdir "$OLDPWD/AppDir" -e "$OLDPWD/AppDir/usr/bin/keychain-manager" \
  -d "$OLDPWD/AppDir/usr/share/applications/dev.keychainmanager.desktop" \
  -i "$OLDPWD/AppDir/usr/share/icons/hicolor/scalable/apps/dev.keychainmanager.svg" --output appimage >/tmp/linuxdeploy.log 2>&1 ||
  { tail -30 /tmp/linuxdeploy.log; exit 1; }
sha256sum "$(basename "$OUTPUT")" > "$(basename "$OUTPUT").sha256"
ls -la "$OUT"
