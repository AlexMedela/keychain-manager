# Keychain Manager - RPM package for Fedora and openSUSE (built on OBS: home:AlexMedela/keychain-manager, see _service).
Name:           keychain-manager
Version:        0.1.0
Release:        0
Summary:        A fast, keyboard-first front end for your desktop's keyrings
License:        MIT
URL:            https://github.com/AlexMedela/keychain-manager
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  gcc
BuildRequires:  grep
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(sdl3)
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(dbus-1)
%if 0%{?suse_version}
BuildRequires:  libopenssl-3-devel
%else
BuildRequires:  pkgconfig(openssl) >= 3.0
%endif
Recommends:     (gnome-keyring or kf6-kwallet or keepassxc)

%description
See, search, copy and edit the passwords your apps keep in the desktop keyring (GNOME Keyring, KWallet, KeePassXC
and other Secret Service providers), with a fast GPU-drawn interface. It stores nothing itself: the keyring
service keeps the secrets, this is a window onto them.

%prep
%autosetup -p1

%build
CFLAGS_PKG="%{optflags}" LDFLAGS_PKG="%{?build_ldflags}" ./build.sh package

%install
DESTDIR=%{buildroot} PREFIX=%{_prefix} ./install.sh

%files
%{_bindir}/keychain-manager
%{_datadir}/applications/dev.keychainmanager.desktop
%{_datadir}/icons/hicolor/scalable/apps/dev.keychainmanager.svg
%{_datadir}/doc/keychain-manager/
%{_datadir}/licenses/keychain-manager/

%changelog
