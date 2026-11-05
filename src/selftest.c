// selftest.c - headless self-tests against a throwaway gnome-keyring on a private session bus (tools/selftest.sh
// starts both and fills the keyring with tools/mkitems.sh's demo items). Suites: model, secret, edit, ui.

// The harness exports KM_TEST_BUS = the bus it started; anything else (the user's own session) is refused.
static bool selftest_bus_ok(void) {
    const char *t = getenv("KM_TEST_BUS"), *b = getenv("DBUS_SESSION_BUS_ADDRESS");
    return t && b && t[0] && !strcmp(t, b);
}

static int g_st_fail, g_st_pass;

#define CHECK(cond, ...) do { if (cond) g_st_pass++; else { g_st_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Item *st_find_label(const char *label) {
    for (int i = 0; i < A.nitems; i++)
        if (!strcmp(A.items[i]->label, label)) return A.items[i];
    return NULL;
}

typedef bool (*StCond)(void *ctx);

// Run the loop until cond holds (or ms pass). False on timeout.
static bool st_wait(StCond cond, void *ctx, int ms) {
    u64 end = now_ns() + (u64)ms * 1000000ull;
    while (now_ns() < end) {
        pump();
        if (cond(ctx)) return true;
        SDL_WaitEventTimeout(NULL, 5);
    }
    pump();
    return cond(ctx);
}

static bool c_loaded(void *c) { (void)c; return A.loaded && !A.loading && A.ss_state != SS_CONNECTING; }
static bool c_req(void *c) { return g_test_done_req == *(u32 *)c; }
static bool c_revealed(void *c) { (void)c; return A.rev.p != NULL || !A.rev_item; }
static bool c_item(void *c) { return item_by_path(c) != NULL; }
static bool c_no_item(void *c) { return item_by_path(c) == NULL; }
static bool c_label(void *c) { return st_find_label(c) != NULL; }
static bool c_sheet_secret(void *c) { (void)c; return g_sheet.secret_loaded; }
static bool c_clip(void *c) { (void)c; return A.clip_owned; }
static bool c_no_clip(void *c) { (void)c; return !A.clip_owned; }

static int st_coll(const char *label) {
    for (int i = 0; i < A.ncolls; i++)
        if (!strcmp(coll_label(i), label)) return i;
    return -1;
}

static bool st_req_wait(u32 id, int ms) { return st_wait(c_req, &id, ms); }

static void st_idle(int ms) { // let signals and their refreshes arrive
    u64 end = now_ns() + (u64)ms * 1000000ull;
    while (now_ns() < end) { pump(); SDL_WaitEventTimeout(NULL, 5); }
}

// The revealed secret of an item, as text ("" if it couldn't be read).
static const char *st_reveal(Item *it) {
    static char out[512];
    out[0] = 0;
    if (!it) return out;
    reveal_clear();
    reveal_toggle(it);
    st_wait(c_revealed, NULL, 5000);
    if (A.rev.p) snprintf(out, sizeof out, "%.*s", (int)A.rev.n, (const char *)A.rev.p);
    reveal_clear();
    return out;
}

static void st_key(SDL_Keycode key, SDL_Keymod mod) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.mod = mod;
    e.key.down = true;
    handle_event(&e);
    pump();
}

static void st_type(const char *s) {
    on_text(s);
    pump();
}

static void st_select(Item *it) {
    for (int i = 0; i < A.nview; i++)
        if (A.view[i] == it) { select_only(i); return; }
}

// --- suites ---------------------------------------------------------------------

static void st_model(void) {
    printf("selftest model\n");
    CHECK(A.ss_state == SS_READY, "service ready (state %d: %s)", A.ss_state, A.ss_msg);
    CHECK(!strcmp(A.backend, "GNOME Keyring"), "backend is GNOME Keyring (%s)", A.backend);
    CHECK(A.encrypted, "encrypted transfer session");
    int login = st_coll("Login");
    CHECK(login >= 0 && A.colls[login].is_default, "Login keyring found and default");
    CHECK(login == 0, "default keyring sorts first");
    CHECK(A.nitems >= 10, "demo items listed (%d)", A.nitems);
    Item *gh = st_find_label("github.com");
    CHECK(gh && !strcmp(gh->kind, "Network password") && gh->icon == ICON_HTTP, "network password kind + icon");
    CHECK(gh && !strcmp(gh->account, "alex") && !strcmp(gh->where, "github.com"), "account + where of github.com");
    Item *wifi = st_find_label("Network secret for Home Wi-Fi/802-11-wireless-security/psk");
    CHECK(wifi && wifi->icon == ICON_WIFI, "Wi-Fi secret icon");
    Item *ch = st_find_label("Chrome Safe Storage");
    CHECK(ch && ch->icon == ICON_BROWSER && !strcmp(ch->where, "chrome"), "browser key");
    Item *ssh = st_find_label("Unlock password for: alex@laptop");
    CHECK(ssh && !strcmp(ssh->kind, "SSH key passphrase") && !strcmp(ssh->where, "/home/alex/.ssh/id_ed25519"), "ssh passphrase");
    Item *git = st_find_label("Git: https://gitlab.com");
    CHECK(git && git->icon == ICON_GIT, "git credential");
    Item *py = st_find_label("Password for 'alex' on 'pypi'");
    CHECK(py && py->icon == ICON_PYTHON, "python keyring");
    Item *note = st_find_label("Notes: Recovery codes");
    CHECK(note && !strcmp(note->kind, "Note"), "note");
    // which app saved it, and which desktop entry is that app
    Item *bk = st_find_label("Brave Safe Storage"), *pm = st_find_label("Password for 'me' on 'Proton'");
    CHECK(bk && !strcmp(bk->app_key, "brave"), "app of a browser key");
    CHECK(pm && !strcmp(pm->app_key, "Proton"), "app of a Python keyring item (its service)");
    CHECK(py && !strcmp(py->app_key, "pypi"), "a name-like service is tried as an app (no match: the Python icon stays)");
    DeskEnt mail = {.id = "Proton Mail", .name = "Proton Mail", .first = "Proton", .exec = "proton-mail", .icon = "proton-mail"};
    DeskEnt tricks = {.id = "protontricks", .name = "Protontricks", .first = "Protontricks", .exec = "protontricks", .icon = "x"};
    DeskEnt brave = {.id = "brave-browser", .name = "Brave Web Browser", .first = "Brave", .exec = "brave", .icon = "brave-browser"};
    DeskEnt chromium = {.id = "chromium", .name = "Chromium", .first = "Chromium", .exec = "chromium", .icon = "chromium"};
    CHECK(desk_score("proton", &mail) > desk_score("proton", &tricks), "\"Proton\" is Proton Mail, not Protontricks");
    CHECK(desk_score("brave", &brave) >= 85 && desk_score("brave", &chromium) == 0, "\"brave\" is Brave");
    CHECK(desk_score("chromium", &chromium) == 100, "\"chromium\" is Chromium");
    // a streamed reload's placeholders keep what an item already shows
    if (gh) {
        ItemInfo ii = {0};
        ii.coll = strdup(gh->coll_path);
        ii.partial = true;
        item_take(gh, &ii);
        free(ii.coll);
        CHECK(!strcmp(gh->label, "github.com") && !gh->pending && gh->nattrs >= 3, "partial update keeps label + attributes");
    }
    // view: all, one keyring, search, sort
    side_select(-1);
    int all = A.nview;
    CHECK(all == A.nitems, "all items shown (%d of %d)", all, A.nitems);
    int session = st_coll("session");
    side_select(session);
    CHECK(A.nview == A.colls[session].nitems, "one keyring's items");
    side_select(-1);
    te_set(&A.search, "alex", 4, 4);
    view_rebuild();
    int na = A.nview;
    CHECK(na >= 5 && na < all, "search 'alex' narrows (%d)", na);
    te_set(&A.search, "imap ALEX", 9, 9);
    view_rebuild();
    CHECK(A.nview == 1 && A.view[0] == st_find_label("IMAP password for alex@example.com"), "two words, any case");
    te_set(&A.search, "zzzz-nothing", 12, 12);
    view_rebuild();
    CHECK(A.nview == 0, "no match");
    te_set(&A.search, "", 0, 0);
    view_rebuild();
    run_command(CMD_SORT, COL_ACCOUNT);
    CHECK(A.sort_col == COL_ACCOUNT && !A.sort_desc, "sort by account");
    bool sorted = true;
    for (int i = 1; i < A.nview; i++) sorted &= natcmp(A.view[i - 1]->account, A.view[i]->account) <= 0;
    CHECK(sorted, "sorted by account");
    run_command(CMD_SORT, COL_ACCOUNT);
    CHECK(A.sort_desc, "second click: descending");
    run_command(CMD_SORT, COL_NAME);
    CHECK(!strcmp(A.view[0]->label, "Brave Safe Storage"), "by name (natural, case-insensitive): %s", A.view[0]->label);
    // palette: fuzzy item search
    palette_open(false);
    te_set(&A.pal_te, "gthb", 4, 4);
    palette_update();
    CHECK(A.pal_n >= 1 && A.pal[0].item == gh, "palette finds github.com by 'gthb'");
    palette_accept(0);
    CHECK(cursor_item() == gh, "palette selects it");
    palette_open(true);
    te_set(&A.pal_te, ">lock all", 9, 9);
    palette_update();
    CHECK(A.pal_n >= 1 && A.pal[0].cmd == CMD_LOCK_ALL, "palette finds commands");
    A.pal_open = false;
}

static void st_secret(void) {
    printf("selftest secret\n");
    Item *gh = st_find_label("github.com");
    CHECK(!strcmp(st_reveal(gh), "ghp_demo4Fj29xQ"), "reveal decrypts the secret");
    Item *wifi = st_find_label("Network secret for Home Wi-Fi/802-11-wireless-security/psk");
    CHECK(!strcmp(st_reveal(wifi), "correct horse battery"), "secret with spaces");
    // copy: on the clipboard, then cleared by the timer
    A.clip_secs = 1;
    copy_secret(gh);
    CHECK(st_wait(c_clip, NULL, 5000), "copied to the clipboard");
    CHECK(A.clip.p && A.clip.n == 15 && !memcmp(A.clip.p, "ghp_demo4Fj29xQ", 15), "clipboard holds the secret");
    size_t hn = 0;
    const void *hint = clip_data(NULL, "x-kde-passwordManagerHint", &hn);
    CHECK(hn == 6 && !memcmp(hint, "secret", 6), "marked as a password for clipboard managers");
    CHECK(st_wait(c_no_clip, NULL, 4000), "cleared after the timeout");
    CHECK(!A.clip.p, "wiped from memory");
    A.clip_secs = 30;
    // lock everything, then unlock (gnome-keyring's test interface: no password window)
    int n = action_lock_all();
    CHECK(n >= 1, "locks the open keyrings (%d)", n);
    st_idle(600);
    int login = st_coll("Login");
    CHECK(login >= 0 && A.colls[login].locked, "Login is locked");
    gh = st_find_label("github.com");
    CHECK(!gh || gh->locked, "its items are locked");
    SsReq *r = ss_req(RQ_TEST_UNLOCK);
    r->path = strdup(A.colls[login].path);
    sec_set(&r->secret, "test", 4);
    CHECK(st_req_wait(ss_submit(r), 5000) && g_test_done_ok, "unlocked with its password");
    st_idle(300);
    login = st_coll("Login");
    CHECK(login >= 0 && !A.colls[login].locked, "Login is open again");
    gh = st_find_label("github.com");
    CHECK(gh && !gh->locked && !strcmp(st_reveal(gh), "ghp_demo4Fj29xQ"), "items readable again");
    // idle lock
    A.idle_lock_min = 1;
    A.idle_locked = false;
    A.last_input = now_ns() - 61000000000ull;
    timers_tick();
    CHECK(A.idle_locked, "idle timeout locks");
    st_idle(600);
    login = st_coll("Login");
    CHECK(login >= 0 && A.colls[login].locked, "locked after being idle");
    r = ss_req(RQ_TEST_UNLOCK);
    r->path = strdup(A.colls[login].path);
    sec_set(&r->secret, "test", 4);
    st_req_wait(ss_submit(r), 5000);
    st_idle(300);
    A.idle_lock_min = 0;
}

static void st_edit(void) {
    printf("selftest edit\n");
    int login = st_coll("Login");
    side_select(login);
    // new item through the editor
    sheet_open_item(NULL, login);
    CHECK(g_sheet.kind == SHEET_ITEM && g_sheet.nattrs == 3, "editor opens with the generic attributes");
    st_type("example.org");
    sheet_focus(1);
    st_type("p4ss w0rd");
    sheet_focus(5); // "service" value
    st_type("example.org");
    sheet_focus(7); // "username" value
    st_type("bob");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    CHECK(!sheet_open(), "saved and closed");
    CHECK(st_wait(c_label, "example.org", 5000), "created item shows up");
    Item *it = st_find_label("example.org");
    CHECK(it && !strcmp(it->account, "bob") && !strcmp(it->where, "example.org"), "with its attributes");
    st_idle(200);
    CHECK(cursor_item() == st_find_label("example.org"), "and is selected");
    CHECK(!strcmp(st_reveal(st_find_label("example.org")), "p4ss w0rd"), "secret stored (encrypted transfer)");
    // edit it: label, secret, one more attribute
    it = st_find_label("example.org");
    sheet_open_item(it, it->coll);
    CHECK(st_wait(c_sheet_secret, NULL, 5000), "editor loads the secret");
    CHECK(!strcmp(g_sheet.secret.buf, "p4ss w0rd"), "into the secret field");
    te_set(&g_sheet.label, "example.org (work)", 0, 0);
    sheet_focus(1);
    g_sheet.secret.anchor = g_sheet.secret.caret = (int)strlen(g_sheet.secret.buf); // (focusing selects it all)
    st_type("!");
    attr_row_add("note", "edited");
    sheet_save_item();
    CHECK(st_wait(c_label, "example.org (work)", 5000), "renamed");
    it = st_find_label("example.org (work)");
    st_idle(200);
    it = st_find_label("example.org (work)");
    CHECK(it && attr_get(it, "note") && !strcmp(attr_get(it, "note"), "edited"), "attribute added");
    CHECK(!strcmp(st_reveal(it), "p4ss w0rd!"), "secret changed");
    // a password generated by the editor
    sheet_open_item(NULL, login);
    sheet_generate();
    CHECK(strlen(g_sheet.secret.buf) == 20 && g_sheet.show_secret, "generated a 20-character password");
    sheet_close();
    CHECK(!g_sheet.secret.buf[0], "the editor's secret is wiped on close");
    // a new keyring, copy and move into it
    SsReq *r = ss_req(RQ_TEST_CREATE);
    r->label = strdup("Work");
    sec_set(&r->secret, "pw", 2);
    CHECK(st_req_wait(ss_submit(r), 5000) && g_test_done_ok, "keyring created");
    st_idle(300);
    int work = st_coll("Work");
    CHECK(work >= 0, "Work keyring listed");
    side_select(st_coll("Login"));
    it = st_find_label("example.org (work)");
    st_select(it);
    run_command(CMD_COPY_TO, st_coll("Work"));
    st_idle(800);
    int copies = 0;
    for (int i = 0; i < A.nitems; i++) copies += !strcmp(A.items[i]->label, "example.org (work)");
    CHECK(copies == 2, "copied into Work (%d copies)", copies);
    side_select(st_coll("Work"));
    CHECK(A.nview == 1 && !strcmp(st_reveal(A.view[0]), "p4ss w0rd!"), "the copy has the secret");
    side_select(st_coll("Login"));
    it = st_find_label("Notes: Recovery codes");
    st_select(it);
    run_command(CMD_MOVE_TO, st_coll("Work"));
    st_idle(800);
    it = st_find_label("Notes: Recovery codes");
    CHECK(it && it->coll == st_coll("Work"), "moved to Work");
    // rename + default
    work = st_coll("Work");
    u32 id = ss_simple(RQ_RENAME_COLL, A.colls[work].path, "Work stuff");
    CHECK(st_req_wait(id, 5000) && st_coll("Work stuff") >= 0, "keyring renamed");
    id = ss_simple(RQ_SET_DEFAULT, A.colls[st_coll("Work stuff")].path, NULL);
    st_req_wait(id, 5000);
    st_idle(200);
    CHECK(A.colls[st_coll("Work stuff")].is_default && !A.colls[st_coll("Login")].is_default, "default keyring changed");
    id = ss_simple(RQ_SET_DEFAULT, A.colls[st_coll("Login")].path, NULL);
    st_req_wait(id, 5000);
    // delete an item (confirm dialog)
    side_select(-1);
    it = st_find_label("example.org (work)");
    char path[512];
    snprintf(path, sizeof path, "%s", it ? it->path : "");
    st_select(it);
    run_command(CMD_DELETE, -1);
    CHECK(A.dlg.open && A.dlg.danger, "delete asks first");
    dialog_close(1);
    CHECK(st_wait(c_no_item, path, 5000), "deleted");
    // a change made by another program shows up by itself
    if (in_path("secret-tool")) {
        char *argv[] = {"secret-tool", "store", "--label=From outside", "service", "outside.test", NULL};
        int fds[2];
        if (pipe(fds) == 0) {
            posix_spawn_file_actions_t fa;
            posix_spawn_file_actions_init(&fa);
            posix_spawn_file_actions_adddup2(&fa, fds[0], 0);
            posix_spawn_file_actions_addclose(&fa, fds[1]);
            pid_t pid;
            if (posix_spawnp(&pid, "secret-tool", &fa, spawn_attr(), argv, environ) == 0) {
                if (write(fds[1], "outside", 7) < 0) {}
                close(fds[1]);
                close(fds[0]);
                CHECK(st_wait(c_label, "From outside", 5000), "item added by another program appears");
            } else { close(fds[0]); close(fds[1]); }
            posix_spawn_file_actions_destroy(&fa);
        }
    }
    // delete the Work keyring (gnome-keyring deletes without a prompt)
    int ws = st_coll("Work stuff");
    action_delete_coll(ws);
    CHECK(A.dlg.open, "keyring delete asks first");
    char wpath[512];
    snprintf(wpath, sizeof wpath, "%s", A.colls[ws].path);
    dialog_close(1);
    st_idle(800);
    CHECK(st_coll("Work stuff") < 0, "keyring deleted");
    CHECK(!st_find_label("Notes: Recovery codes"), "with its items");
}

static void st_ui(void) {
    printf("selftest ui\n");
    const char *dir = getenv("KM_SHOT_DIR");
    char shot[PATH_MAX];
#define SHOT(name) do { if (dir) { frame(); SDL_Surface *s_ = SDL_RenderReadPixels(A.ren, NULL); snprintf(shot, sizeof shot, "%s/%s.bmp", dir, name); if (s_) { SDL_SaveBMP(s_, shot); SDL_DestroySurface(s_); } } } while (0)
    side_select(-1);
    SHOT("list");
    // keyboard: move, search by typing, clear
    select_only(0);
    st_key(SDLK_DOWN, SDL_KMOD_NONE);
    CHECK(A.cursor == 1, "Down moves the cursor");
    st_key(SDLK_DOWN, SDL_KMOD_SHIFT);
    CHECK(A.nsel == 2, "Shift+Down extends the selection");
    st_key(SDLK_END, SDL_KMOD_NONE);
    CHECK(A.cursor == A.nview - 1 && A.nsel == 1, "End");
    int all = A.nview;
    st_type("g");
    st_type("i");
    st_type("t");
    CHECK(A.focus == FOCUS_SEARCH && !strcmp(A.search.buf, "git") && A.nview < all, "typing in the list searches");
    SHOT("search");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    CHECK(!A.search.buf[0] && A.nview == all, "Esc clears the search");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    CHECK(A.focus == FOCUS_LIST, "second Esc leaves the field");
    bool insp = A.insp_on;
    st_key(SDLK_SPACE, SDL_KMOD_NONE);
    CHECK(A.insp_on != insp, "Space toggles the details panel");
    st_key(SDLK_SPACE, SDL_KMOD_NONE);
    st_key(SDLK_F, SDL_KMOD_CTRL);
    CHECK(A.focus == FOCUS_SEARCH, "Ctrl+F focuses search");
    st_type(" ");
    CHECK(A.insp_on == insp && !strcmp(A.search.buf, " "), "space in the search field is text");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    // overlays render
    Item *gh = st_find_label("github.com");
    st_select(gh);
    reveal_toggle(gh);
    st_wait(c_revealed, NULL, 5000);
    SHOT("revealed");
    reveal_clear();
    menu_items(S(400), S(200));
    CHECK(A.menu.open && A.menu.n > 4, "item menu");
    SHOT("menu");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    CHECK(!A.menu.open, "Esc closes it");
    st_key(SDLK_P, SDL_KMOD_CTRL);
    CHECK(A.pal_open, "Ctrl+P opens the palette");
    st_type("pypi");
    SHOT("palette");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    CHECK(!A.pal_open && cursor_item() == st_find_label("Password for 'alex' on 'pypi'"), "palette jumps to the item");
    run_command(CMD_EDIT, -1);
    st_wait(c_sheet_secret, NULL, 5000);
    SHOT("edit");
    CHECK(g_sheet.kind == SHEET_ITEM, "Enter edits");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    CHECK(!sheet_open(), "Esc cancels");
    run_command(CMD_OPTIONS, -1);
    SHOT("options");
    g_sheet.page = 1;
    SHOT("keys");
    sheet_close();
    sheet_open_name(-1);
    SHOT("new-keyring");
    sheet_close();
    // small windows: the list keeps room, the details panel fits (narrower) or steps aside, and scrolls
    int ow = A.W, oh = A.H;
    A.insp_on = true;
    A.W = (int)roundf(S(720));
    A.H = (int)roundf(S(420));
    layout();
    CHECK(A.r_insp.w > 0 && A.r_list.w >= S(280) - 1, "720x420: list and details panel both fit");
    frame();
    CHECK(A.insp_content_h > A.r_insp.h, "details taller than the panel there: it scrolls");
    A.W = (int)roundf(S(560));
    layout();
    CHECK(A.r_insp.w == 0 && A.insp_squeezed && A.r_list.w >= S(280) - 1, "too narrow: the panel steps aside");
    A.W = ow;
    A.H = oh;
    layout();
    // shortcuts: parse, label, rebind, keep in settings
    Combo c;
    CHECK(combo_parse("Ctrl+Shift+C", &c) && c.key == SDLK_C && c.mods == (KM_CTRL | KM_SHIFT), "parse a shortcut");
    char lab[64];
    combo_label(&c, lab, sizeof lab);
    CHECK(!strcmp(lab, "Ctrl+Shift+C"), "label it (%s)", lab);
    int idx = -1;
    for (int i = 0; i < NKEYS; i++) if (g_keys[i].cmd == CMD_LOCK_ALL) idx = i;
    combo_parse("Ctrl+Alt+L", &c);
    key_rebind(idx, c);
    CHECK(key_command(SDLK_L, SDL_KMOD_CTRL | SDL_KMOD_ALT) == CMD_LOCK_ALL && key_command(SDLK_L, SDL_KMOD_CTRL) == CMD_NONE, "rebound");
    setenv("KM_SELFTEST_SAVE", "1", 1);
    A.clip_secs = 60;
    A.col_on[COL_CREATED] = true;
    A.colw[COL_ACCOUNT] = 222;
    settings_save();
    settings_defaults();
    CHECK(key_command(SDLK_L, SDL_KMOD_CTRL) == CMD_LOCK_ALL, "defaults back");
    settings_load();
    CHECK(key_command(SDLK_L, SDL_KMOD_CTRL | SDL_KMOD_ALT) == CMD_LOCK_ALL, "shortcut kept in settings.conf");
    CHECK(A.clip_secs == 60 && A.col_on[COL_CREATED] && fabsf(A.colw[COL_ACCOUNT] - 222) < 0.5f, "options kept");
    char sp[PATH_MAX], conf[PATH_MAX + 32];
    settings_dir(sp, sizeof sp);
    snprintf(conf, sizeof conf, "%s/settings.conf", sp);
    FILE *f = fopen(conf, "r");
    bool leak = false;
    if (f) {
        char line[1024];
        while (fgets(line, sizeof line, f)) leak |= strstr(line, "ghp_") || strstr(line, "github.com") || strstr(line, "p4ss");
        fclose(f);
    }
    CHECK(f && !leak, "settings.conf holds no item data");
    unsetenv("KM_SELFTEST_SAVE");
    // the first run takes File Explorer's look
    char fe[PATH_MAX + 64];
    config_home(sp, sizeof sp);
    snprintf(fe, sizeof fe, "%s/file-explorer", sp);
    mkdir_p(fe, 0755);
    snprintf(fe, sizeof fe, "%s/file-explorer/settings.conf", sp);
    write_file_atomic(fe, "theme=Nord\naccent=Purple\nfont_size=14\n");
    settings_defaults();
    settings_import_fe();
    CHECK(!strcmp(A.theme_name, "Nord") && (g_accent_custom & 0xFFFFFF) == 0x9D7CF4 && A.font_size == 14, "File Explorer's theme, accent, size");
    settings_defaults();
}

static int run_selftest(const char *which) {
    if (!st_wait(c_loaded, NULL, 15000)) { printf("FAIL: the keyring service didn't answer\n"); return 1; }
    bool all = !strcmp(which, "all");
    if (all || !strcmp(which, "model")) st_model();
    if (all || !strcmp(which, "secret")) st_secret();
    if (all || !strcmp(which, "edit")) st_edit();
    if (all || !strcmp(which, "ui")) st_ui();
    if (!all && strcmp(which, "model") && strcmp(which, "secret") && strcmp(which, "edit") && strcmp(which, "ui")) {
        printf("unknown suite %s (model, secret, edit, ui)\n", which);
        return 2;
    }
    printf("%d passed, %d failed\n", g_st_pass, g_st_fail);
    clip_quit();
    reveal_clear();
    return g_st_fail ? 1 : 0;
}
