<div align="center">

<img src="packaging/dev.keychainmanager.svg" width="84" alt="Keychain Manager logo">

# Keychain Manager

**Your desktop's keyrings, at a glance.** Fast, keyboard-first, for Linux.

[![CI](https://img.shields.io/github/actions/workflow/status/AlexMedela/keychain-manager/ci.yml?branch=main&style=flat&label=CI&logo=githubactions&logoColor=white)](https://github.com/AlexMedela/keychain-manager/actions)
[![Release](https://img.shields.io/github/v/release/AlexMedela/keychain-manager?style=flat&color=7aa2f7&logo=github&include_prereleases)](https://github.com/AlexMedela/keychain-manager/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-9ece6a?style=flat)](LICENSE)
![Linux](https://img.shields.io/badge/Linux-Wayland%20%7C%20X11-bb9af7?style=flat&logo=linux&logoColor=white)
![C11 + SDL3](https://img.shields.io/badge/C11-SDL3-e0af68?style=flat&logo=c&logoColor=white)
![Made with Claude](https://img.shields.io/badge/made_with-Claude-D97757?style=flat&logo=claude&logoColor=white)

<img src="docs/img/hero.png" alt="Keychain Manager: the items of a GNOME keyring, one of them in the details panel" width="900">

</div>

## Features
- **Works with the keyring you have**: GNOME Keyring, KWallet, KeePassXC or any Secret Service provider. It stores nothing itself.
- **Instant**: thousands of items listed in milliseconds; the window never waits on the keyring service.
- **Keyboard-first**: type to search names, accounts and attributes, `Ctrl+P` jumps to any item, every shortcut can be rebound.
- **Knows what things are**: web and Git logins, Wi-Fi keys, SSH and GPG passphrases, browser keys, mail accounts, each with its icon.
- **Copy safely**: secrets leave the clipboard after 30 s and are marked so clipboard histories skip them.
- **Edit everything**: secrets, names, attributes; new items with a password generator; copy or move items between keyrings.
- **Keyrings**: create, rename, lock, unlock, make default, delete; lock them all after a while without use.
- **Live**: changes made by other apps show up by themselves.
- **Careful with secrets**: encrypted transfers from the service, locked and wiped memory, no core dumps.
- **Looks like File Explorer**: the same GPU-drawn look, themes and title bar on every desktop; its theme is taken over on first run.

## Install
| Distro | How | |
|---|---|---|
| Debian 13, Ubuntu 25.10+, Mint, Pop!_OS | add the repo once, then `sudo apt install keychain-manager` ([commands](docs/install.md#debian-13-ubuntu-2510--2604--2610-mint-pop_os)) | ![apt repo](https://img.shields.io/badge/apt%20repo-OBS-e0af68?style=flat) |
| Fedora 43 / 44 | add the repo once, then `sudo dnf install keychain-manager` ([commands](docs/install.md#fedora-43--44)) | ![rpm repo](https://img.shields.io/badge/rpm%20repo-OBS-e0af68?style=flat) |
| openSUSE Tumbleweed / Leap 16 | add the repo once, then `sudo zypper install keychain-manager` ([commands](docs/install.md#opensuse-tumbleweed--leap-160)) | ![rpm repo](https://img.shields.io/badge/rpm%20repo-OBS-e0af68?style=flat) |
| Arch, CachyOS, Manjaro, EndeavourOS | `makepkg -si` in `packaging/arch` of a clone ([commands](docs/install.md#arch-cachyos-manjaro-endeavouros)) | ![ready](https://img.shields.io/badge/ready-PKGBUILD-9ece6a?style=flat) |
| Any distro (Ubuntu 22.04+, Debian 12+...) | AppImage from [Releases](https://github.com/AlexMedela/keychain-manager/releases/latest) ([how](docs/install.md#any-other-distro-appimage)) | ![AppImage](https://img.shields.io/badge/AppImage-x86__64%20%7C%20ARM64-9ece6a?style=flat) |

It needs a keyring service running: GNOME Keyring, KWallet (Plasma), or KeePassXC with its Secret Service integration on.
Details: [docs/install.md](docs/install.md).

## First steps
| Key | |
|---|---|
| Type | Search names, accounts and attributes |
| `Ctrl+P` | Go to any item |
| `Ctrl+C` | Copy the secret (cleared after 30 s) |
| `Ctrl+R` | Show the secret |
| `Enter` | Edit the item |
| `Ctrl+N` | New item |
| `Ctrl+L` | Lock all keyrings |
| `Ctrl+,` | Options: themes, clipboard, auto-lock, keys |

All keys: [docs/keys.md](docs/keys.md)

## Docs
[Install](docs/install.md) · [Keys](docs/keys.md) · [Settings](docs/settings.md) · [Building](docs/building.md) · [Architecture](docs/architecture.md)

## Licence
[MIT](LICENSE). Bundled icons and libraries keep their own licences, listed in [Building](docs/building.md#third-party).
