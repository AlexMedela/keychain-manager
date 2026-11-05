#!/bin/sh
# Fill the current session's default keyring with demo items (+ N generated ones). Only for test buses:
# tools/selftest.sh calls it; refuses to run without KM_TMP (set by the harness).
set -e
[ -n "$KM_TMP" ] || { echo "mkitems.sh only runs inside tools/selftest.sh"; exit 1; }
n="${1:-0}"
put() { # label secret attr value...
    label="$1"; secret="$2"; shift 2
    printf %s "$secret" | secret-tool store --label="$label" "$@"
}
put "github.com" "ghp_demo4Fj29xQ" xdg:schema org.gnome.keyring.NetworkPassword server github.com protocol https user alex
put "Git: https://gitlab.com" "glpat-demo-77" xdg:schema org.git.Password server gitlab.com protocol https user alex@example.com
put "Network secret for Home Wi-Fi/802-11-wireless-security/psk" "correct horse battery" xdg:schema org.freedesktop.NetworkManager.Connection connection-uuid 6b2f setting-name 802-11-wireless-security setting-key psk
put "Chrome Safe Storage" "c2FmZS1zdG9yYWdl" xdg:schema chrome_libsecret_os_crypt_password_v2 application chrome
put "IMAP password for alex@example.com" "mail-pass-1" xdg:schema org.gnome.keyring.NetworkPassword server imap.example.com protocol imap user alex@example.com
put "Unlock password for: alex@laptop" "ssh-pass" xdg:schema org.freedesktop.Secret.Generic unique ssh-store:/home/alex/.ssh/id_ed25519
put "docker-credential-secretservice" "dckr_pat_demo" xdg:schema org.freedesktop.Secret.Generic label docker server https://index.docker.io/v1/ username alex
put "Visual Studio Code" "vsc-token" xdg:schema org.freedesktop.Secret.Generic application vscode service vscodevscode.github-authentication account github.auth
put "Password for 'alex' on 'pypi'" "pypi-AgEIc" xdg:schema org.freedesktop.Secret.Generic application "Python keyring library" service pypi username alex
put "Notes: Recovery codes" "1234-5678 9012-3456" xdg:schema org.gnome.keyring.Note
i=0
while [ "$i" -lt "$n" ]; do
    put "Generated item $i" "secret-$i" xdg:schema org.freedesktop.Secret.Generic service "svc$((i % 97)).example.org" username "user$i"
    i=$((i + 1))
done
