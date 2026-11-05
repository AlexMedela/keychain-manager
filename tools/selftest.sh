#!/bin/sh
# Run the app against a throwaway gnome-keyring on its own session bus: never the user's keyrings.
# Usage: tools/selftest.sh [BINARY] -- ARGS...   e.g. tools/selftest.sh --selftest, tools/selftest.sh -- --bench
#        KM_ITEMS=N: fill the test keyring with N generated items (tools/mkitems.sh)
set -e
here="$(cd "$(dirname "$0")" && pwd)"
bin="$here/../build/keychain-manager"
if [ $# -gt 0 ] && [ "$1" != "--" ] && [ "${1#-}" = "$1" ]; then bin="$1"; shift; fi
[ "$1" = "--" ] && shift
[ -x "$bin" ] || { echo "no binary at $bin: build first"; exit 1; }
command -v dbus-run-session >/dev/null || { echo "needs dbus-run-session (dbus)"; exit 1; }
command -v gnome-keyring-daemon >/dev/null || { echo "needs gnome-keyring-daemon"; exit 1; }
tmp="$(mktemp -d /tmp/km-test.XXXXXX)"
trap 'rm -rf "$tmp"' EXIT
export KM_TMP="$tmp" KM_HERE="$here" KM_BIN="$bin"
# (no display for anything the private bus starts: the app renders offscreen, and services it activates, such as
# desktop portals, must not reach the real session's compositor)
unset WAYLAND_DISPLAY DISPLAY
exec dbus-run-session -- sh -c '
    set -e
    export HOME="$KM_TMP/home" XDG_DATA_HOME="$KM_TMP/data" XDG_CONFIG_HOME="$KM_TMP/config" XDG_RUNTIME_DIR="$KM_TMP/run"
    mkdir -p "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    printf test | gnome-keyring-daemon --unlock --components=secrets >/dev/null
    # wait until it owns the name: a request before that would start a second, empty daemon (bus activation)
    i=0
    until dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
          org.freedesktop.DBus.NameHasOwner string:org.freedesktop.secrets 2>/dev/null | grep -q "boolean true"; do
        i=$((i + 1)); [ "$i" -gt 100 ] && { echo "gnome-keyring did not start"; exit 1; }
        sleep 0.05
    done
    sh "$KM_HERE/mkitems.sh" "${KM_ITEMS:-0}"
    export KM_TEST_BUS="$DBUS_SESSION_BUS_ADDRESS"
    "$KM_BIN" "$@"
' sh "$@"
