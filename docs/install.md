# Installing

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Building](building.md) · [Architecture](architecture.md)

Keychain Manager is a window onto the keyring service your desktop already runs; it needs one of:
**GNOME Keyring** (GNOME, Xfce, Cinnamon, MATE, most tiling setups), **KWallet** (KDE Plasma; its Secret Service
support is on by default since Plasma 5.97) or **KeePassXC** with *Settings > Secret Service Integration* on.

If KWallet is installed with its Secret Service support switched off, the app says so and offers to turn it on
(it sets \`apiEnabled\` in \`kwalletrc\` and restarts KWallet, after asking).

Packages are built on the [openSUSE Build Service](https://build.opensuse.org/package/show/home:AlexMedela/keychain-manager)
for x86_64 and ARM64, from the same repository as File Explorer's: if you added that one, just install the package.

## Debian 13, Ubuntu 25.10 / 26.04 / 26.10, Mint, Pop!_OS
Pick the line for your release (`Debian_13`, `xUbuntu_25.10`, `xUbuntu_26.04`, `xUbuntu_26.10`):
```sh
R=Debian_13
echo "deb https://download.opensuse.org/repositories/home:/AlexMedela/$R/ /" | sudo tee /etc/apt/sources.list.d/alexmedela.list
curl -fsSL "https://download.opensuse.org/repositories/home:AlexMedela/$R/Release.key" | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/alexmedela.gpg >/dev/null
sudo apt update && sudo apt install keychain-manager
```

## Fedora 43 / 44
```sh
sudo dnf config-manager addrepo --from-repofile="https://download.opensuse.org/repositories/home:AlexMedela/Fedora_$(rpm -E %fedora)/home:AlexMedela.repo"
sudo dnf install keychain-manager
```

## openSUSE Tumbleweed / Leap 16.0
```sh
sudo zypper addrepo https://download.opensuse.org/repositories/home:AlexMedela/openSUSE_Tumbleweed/home:AlexMedela.repo   # Leap: .../16.0/...
sudo zypper refresh && sudo zypper install keychain-manager
```

## Arch, CachyOS, Manjaro, EndeavourOS
```sh
git clone https://github.com/AlexMedela/keychain-manager && cd keychain-manager/packaging/arch && makepkg -si
```
Inside a checkout, the PKGBUILD builds that checkout; `git pull` and `makepkg -si` again to update.

## Any other distro (AppImage)
Download `keychain-manager-<version>-<arch>.AppImage` from the
[latest release](https://github.com/AlexMedela/keychain-manager/releases/latest), then `chmod +x` it and run it.
It runs on glibc 2.35+ (Ubuntu 22.04, Debian 12 and newer) and updates with AppImageUpdate.

## From source
See [Building](building.md).
