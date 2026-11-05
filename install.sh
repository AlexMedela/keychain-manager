#!/bin/sh
# Install Keychain Manager. Usage: [DESTDIR=...] [PREFIX=/usr/local] ./install.sh
# Build first with ./build.sh (or ./build.sh package for a portable binary).
set -e
cd "$(dirname "$0")"
PREFIX="${PREFIX:-/usr/local}"
DEST="${DESTDIR:-}"
[ -x build/keychain-manager ] || { echo "build/keychain-manager missing: run ./build.sh first"; exit 1; }
install -Dm755 build/keychain-manager "$DEST$PREFIX/bin/keychain-manager"
install -Dm644 packaging/dev.keychainmanager.desktop "$DEST$PREFIX/share/applications/dev.keychainmanager.desktop"
install -Dm644 packaging/dev.keychainmanager.svg "$DEST$PREFIX/share/icons/hicolor/scalable/apps/dev.keychainmanager.svg"
install -Dm644 README.md "$DEST$PREFIX/share/doc/keychain-manager/README.md"
for d in docs/*.md; do install -Dm644 "$d" "$DEST$PREFIX/share/doc/keychain-manager/$d"; done
install -Dm644 LICENSE "$DEST$PREFIX/share/licenses/keychain-manager/LICENSE"
install -Dm644 third_party/nanosvg_LICENSE.txt "$DEST$PREFIX/share/licenses/keychain-manager/nanosvg_LICENSE.txt"
install -Dm644 third_party/material-icons/LICENSE "$DEST$PREFIX/share/licenses/keychain-manager/material-icon-theme_LICENSE.txt"
echo "installed to $DEST$PREFIX"
if [ -z "$DEST" ]; then
  command -v update-desktop-database >/dev/null && update-desktop-database -q "$PREFIX/share/applications" || true
  command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$PREFIX/share/icons/hicolor" || true
fi
