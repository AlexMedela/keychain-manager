#!/bin/sh
# Build Keychain Manager. Usage: ./build.sh [release|debug|package]
# KM_OUT=<file>: where the binary goes (absolute path).
set -e
cd "$(dirname "$0")"
mode="${1:-release}"
out="${KM_OUT:-build/keychain-manager}"
mkdir -p "$(dirname "$out")"
SRCS="src/*.c src/*.h"
PKGS="sdl3 freetype2 fontconfig dbus-1 openssl"
CFLAGS="-std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation $(pkg-config --cflags $PKGS)"
LIBS="$(pkg-config --libs $PKGS) -lpthread -lm"
case "$mode" in
  release) OPT="-O2 -march=native -DNDEBUG -flto=auto" ;;
  package) OPT="${CFLAGS_PKG:--O2} -DNDEBUG" ;;   # portable: no -march=native (distro packages)
  debug)   OPT="-O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer" ;;
  *) echo "unknown mode: $mode"; exit 1 ;;
esac
# Source must be pure ASCII: UTF-8 text goes in as \x escapes (catches mangled characters).
# shellcheck disable=SC2086
if LC_ALL=C grep -nP '[^\x00-\x7F]' $SRCS; then echo "error: non-ASCII characters in source (use \\x escapes)"; exit 1; fi
${CC:-cc} $CFLAGS $OPT src/main.c -o "$out" $LIBS ${LDFLAGS_PKG:-}
echo "built $out ($mode)"
