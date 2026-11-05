# Settings

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Building](building.md) · [Architecture](architecture.md)

Options (Ctrl+,) are saved in `~/.config/keychain-manager/settings.conf`. That file holds **only** these
preferences: no item names, attributes or secrets - nothing about your keyrings is kept by this app.

| Option | |
|---|---|
| Theme | Dark, Light, Black, Monokai, Nord, plus JSON colour schemes in `~/.config/keychain-manager/themes/` (the same format as File Explorer's). |
| Accent colour | The theme's own, or one of nine; the logo and the app icon follow it. |
| Text size | 9 to 24 (zoom with Ctrl+= / Ctrl+- on top of it). |
| Clear copied secrets | 10 s, 30 s (default), 1 min, 2 min or never. Only cleared if the clipboard still holds the secret. |
| Lock keyrings | When the app isn't used for 5 min, 15 min or 1 hour (default: never). Locks every open keyring in the service. |
| Columns | Account, Where, Kind, Keyring, Modified, Created (Name is always there). |
| Alternating row colours, smooth scrolling | |
| Title bar | Automatic (the app's own), this app's, or the system's. |

**First run:** if File Explorer is installed, its theme, accent, font and text size (and its JSON colour schemes)
are taken over once, so both apps look the same. Afterwards each keeps its own settings.

Window size, panel widths, sort order and the last keyring shown are kept too. Shortcuts changed in Options >
Keyboard are saved as `key=slug:Ctrl+...` lines.
