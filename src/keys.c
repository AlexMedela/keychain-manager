// keys.c - rebindable shortcuts: the default bindings table (stable slugs for settings.conf), parsing "Ctrl+Shift+C",
// matching key events, labels for menus and the Options > Keyboard page

typedef struct { SDL_Keycode key; u16 mods; } Combo; // mods: KM_CTRL | KM_SHIFT | KM_ALT | KM_SUPER
#define KM_CTRL 1
#define KM_SHIFT 2
#define KM_ALT 4
#define KM_SUPER 8

typedef struct {
    int cmd;
    const char *slug, *def; // def: "" = no default binding
    Combo cur;
} KeyBind;

static KeyBind g_keys[] = {
    {CMD_COPY_SECRET, "copy-secret", "Ctrl+C", {0}},
    {CMD_COPY_ACCOUNT, "copy-account", "Ctrl+Shift+C", {0}},
    {CMD_COPY_WHERE, "copy-where", "Ctrl+Alt+C", {0}},
    {CMD_REVEAL, "reveal", "Ctrl+R", {0}},
    {CMD_EDIT, "edit", "Ctrl+E", {0}},
    {CMD_NEW_ITEM, "new-item", "Ctrl+N", {0}},
    {CMD_DELETE, "delete", "Delete", {0}},
    {CMD_NEW_KEYRING, "new-keyring", "Ctrl+Shift+N", {0}},
    {CMD_RENAME_KEYRING, "rename-keyring", "F2", {0}},
    {CMD_LOCK_ALL, "lock-all", "Ctrl+L", {0}},
    {CMD_FIND, "find", "Ctrl+F", {0}},
    {CMD_PALETTE, "palette", "Ctrl+P", {0}},
    {CMD_COMMANDS, "commands", "Ctrl+Shift+P", {0}},
    {CMD_INSPECTOR, "details", "Ctrl+I", {0}},
    {CMD_OPTIONS, "options", "Ctrl+,", {0}},
    {CMD_KEYS, "keys", "Ctrl+/", {0}},
    {CMD_RELOAD, "reload", "F5", {0}},
    {CMD_SELECT_ALL, "select-all", "Ctrl+A", {0}},
    {CMD_PREV_KEYRING, "prev-keyring", "Alt+Up", {0}},
    {CMD_NEXT_KEYRING, "next-keyring", "Alt+Down", {0}},
    {CMD_ALL_ITEMS, "all-items", "Alt+Home", {0}},
    {CMD_ZOOM_IN, "zoom-in", "Ctrl+=", {0}},
    {CMD_ZOOM_OUT, "zoom-out", "Ctrl+-", {0}},
    {CMD_ZOOM_RESET, "zoom-reset", "Ctrl+0", {0}},
    {CMD_QUIT, "quit", "Ctrl+Q", {0}},
    {CMD_OVERLAY, "frame-times", "Ctrl+Shift+F12", {0}},
};
#define NKEYS ((int)ARRAY_COUNT(g_keys))

static u16 key_mods(SDL_Keymod m) {
    return (u16)(((m & SDL_KMOD_CTRL) ? KM_CTRL : 0) | ((m & SDL_KMOD_SHIFT) ? KM_SHIFT : 0) | ((m & SDL_KMOD_ALT) ? KM_ALT : 0) |
                 ((m & SDL_KMOD_GUI) ? KM_SUPER : 0));
}

// "Ctrl+Shift+C" -> combo; "" or "none" -> unbound. False if malformed.
static bool combo_parse(const char *s, Combo *out) {
    memset(out, 0, sizeof *out);
    if (!s[0] || !strcmp(s, "none")) return true;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    char *p = buf;
    for (;;) {
        char *plus = strchr(p, '+');
        if (!plus || plus == p + strlen(p) - 1) break; // (the last part is the key; "Ctrl++" ends in a plus key)
        *plus = 0;
        if (str_ieq(p, "Ctrl")) out->mods |= KM_CTRL;
        else if (str_ieq(p, "Shift")) out->mods |= KM_SHIFT;
        else if (str_ieq(p, "Alt")) out->mods |= KM_ALT;
        else if (str_ieq(p, "Super")) out->mods |= KM_SUPER;
        else return false;
        p = plus + 1;
    }
    out->key = SDL_GetKeyFromName(p);
    return out->key != SDLK_UNKNOWN;
}

static void combo_label(const Combo *c, char *out, size_t cap) {
    out[0] = 0;
    if (!c->key) return;
    snprintf(out, cap, "%s%s%s%s%s", (c->mods & KM_CTRL) ? "Ctrl+" : "", (c->mods & KM_SHIFT) ? "Shift+" : "",
             (c->mods & KM_ALT) ? "Alt+" : "", (c->mods & KM_SUPER) ? "Super+" : "", SDL_GetKeyName(c->key));
}

static void keys_defaults(void) {
    for (int i = 0; i < NKEYS; i++) combo_parse(g_keys[i].def, &g_keys[i].cur);
}

// settings.conf: key=slug:Combo (only the ones changed from the defaults)
static void keys_load_line(const char *v) {
    const char *colon = strchr(v, ':');
    if (!colon) return;
    for (int i = 0; i < NKEYS; i++)
        if (strlen(g_keys[i].slug) == (size_t)(colon - v) && !strncmp(g_keys[i].slug, v, (size_t)(colon - v))) {
            Combo c;
            if (combo_parse(colon + 1, &c)) g_keys[i].cur = c;
        }
}

static void keys_save(FILE *f) {
    for (int i = 0; i < NKEYS; i++) {
        Combo d;
        combo_parse(g_keys[i].def, &d);
        if (d.key == g_keys[i].cur.key && d.mods == g_keys[i].cur.mods) continue;
        char lab[64];
        combo_label(&g_keys[i].cur, lab, sizeof lab);
        fprintf(f, "key=%s:%s\n", g_keys[i].slug, lab[0] ? lab : "none");
    }
}

static void key_label_for(int cmd, char *out, size_t cap) {
    out[0] = 0;
    for (int i = 0; i < NKEYS; i++)
        if (g_keys[i].cmd == cmd && g_keys[i].cur.key) { combo_label(&g_keys[i].cur, out, cap); return; }
}

// The command bound to this key press, or CMD_NONE.
static int key_command(SDL_Keycode key, SDL_Keymod mod) {
    u16 m = key_mods(mod);
    if (key >= SDLK_KP_0 && key <= SDLK_KP_9 && !(mod & SDL_KMOD_NUM)) return CMD_NONE;
    for (int i = 0; i < NKEYS; i++) {
        Combo *c = &g_keys[i].cur;
        if (!c->key || c->key != key) continue;
        if (c->mods == m) return g_keys[i].cmd;
        // "Ctrl+=" also when Shift is needed to reach the key on this layout ("+" on many keyboards)
        if ((m & KM_SHIFT) && c->mods == (m & ~KM_SHIFT) && (key == SDLK_EQUALS || key == SDLK_PLUS)) return g_keys[i].cmd;
    }
    if ((key == SDLK_PLUS || key == SDLK_KP_PLUS) && (m & KM_CTRL)) return CMD_ZOOM_IN;
    if (key == SDLK_KP_MINUS && (m & KM_CTRL)) return CMD_ZOOM_OUT;
    return CMD_NONE;
}

// Rebinding (Options > Keyboard): the combo is taken from whatever command had it.
static void key_rebind(int idx, Combo c) {
    if (idx < 0 || idx >= NKEYS) return;
    for (int i = 0; i < NKEYS; i++)
        if (i != idx && c.key && g_keys[i].cur.key == c.key && g_keys[i].cur.mods == c.mods) memset(&g_keys[i].cur, 0, sizeof(Combo));
    g_keys[idx].cur = c;
    A.settings_dirty = true;
}
