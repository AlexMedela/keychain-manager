# Building from source

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Building](building.md) · [Architecture](architecture.md)

```sh
git clone https://github.com/AlexMedela/keychain-manager && cd keychain-manager
./build.sh package && sudo PREFIX=/usr/local ./install.sh
```
On Arch, `cd packaging/arch && makepkg -si` builds a package from the checkout instead.

## Dependencies
SDL 3, FreeType, fontconfig, libdbus-1 and OpenSSL 3 (development packages), a C11 compiler and pkg-config:

| Distro | |
|---|---|
| Arch | `pacman -S base-devel sdl3 freetype2 fontconfig dbus openssl` |
| Fedora | `dnf install gcc pkgconf-pkg-config SDL3-devel freetype-devel fontconfig-devel dbus-devel openssl-devel` |
| Debian 13, Ubuntu 25.10+ | `apt install build-essential pkg-config libsdl3-dev libfreetype-dev libfontconfig-dev libdbus-1-dev libssl-dev` |

## Build & run
```sh
./build.sh            # release build -> build/keychain-manager
./build.sh debug      # ASan/UBSan build
./build.sh package    # portable (no -march=native), what packages use
./build/keychain-manager
```

## Tools and self-tests
The self-tests never touch your keyrings: `tools/selftest.sh` starts a private session bus with a throwaway
GNOME Keyring in `/tmp`, fills it with demo items and runs the binary there (it needs `dbus-run-session`,
`gnome-keyring-daemon` and `secret-tool`). The binary refuses `--selftest` on any other bus.

```sh
tools/selftest.sh -- --selftest                   # all suites: model, secret, edit, ui
tools/selftest.sh -- --selftest-secret            # one suite
KM_SHOT_DIR=/tmp/shots tools/selftest.sh -- --selftest-ui   # + a screenshot (BMP) per UI state
KM_ITEMS=5000 tools/selftest.sh -- --bench        # time listing 5,000 items
tools/selftest.sh -- --screenshot out.bmp         # one frame, headless
```
`tools/gen_icons.py` regenerates `src/icons_data.h`, `tools/gen_logo.py` the logo.

## Third-party
- [nanosvg](https://github.com/memononen/nanosvg) (zlib licence) - SVG icons.
- [Material Icon Theme](https://github.com/material-extensions/vscode-material-icon-theme) (MIT) - item icons.
- The look, fonts code and colour themes are shared with [File Explorer](https://github.com/AlexMedela/file-explorer).
