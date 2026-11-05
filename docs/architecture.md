# Architecture

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Building](building.md) · [Architecture](architecture.md)

Keychain Manager is a front end only: every keyring and secret stays in the service that owns it (GNOME Keyring,
KWallet, KeePassXC...), reached through the freedesktop **Secret Service** D-Bus API.

- `src/secret.c` - one thread owns the D-Bus connection. The UI hands it requests and gets events back
  (`main_post`), so the window never waits on the bus. Listing is pipelined (`src/dbus.c`): the properties of
  every item are requested at once and the replies collected together, so thousands of items take about one
  round-trip instead of one per item. Secrets are only fetched when you show, copy or edit one.
- Change signals (`ItemCreated`, `ItemChanged`, `ItemDeleted`, `CollectionChanged`...) are gathered per loop
  round and fetched together; the list updates in place when other apps change the keyring. If the service restarts
  the app reconnects.
- Unlocking, creating and deleting keyrings may need the keyring's password: the service shows its own window
  (a *prompt*); this app never asks for or sees master passwords.
- Transfers are encrypted: `dh-ietf1024-sha256-aes128-cbc-pkcs7` (Diffie-Hellman, HKDF-SHA256, AES-128-CBC, as
  libsecret does), `plain` only if a service doesn't offer it.
- Secrets in memory live in locked pages (`mlock`), are wiped (`explicit_bzero`) when hidden, copied out or
  closed; core dumps and ptrace by other processes are off. Copied secrets carry the
  `x-kde-passwordManagerHint` so clipboard histories skip them, and are cleared after a timeout.
- `src/draw.c`, `src/font.c`, `src/theme.c` - the same GPU-batched drawing, glyph atlas and themes as File
  Explorer: a frame is a few `SDL_RenderGeometry` calls, and the app sleeps when idle.
- `src/model.c` - the UI-side model: what each item is (from its `xdg:schema` and attributes: web, Wi-Fi, Git,
  SSH, mail, browser keys...), the filtered and sorted view, selection that survives updates.
