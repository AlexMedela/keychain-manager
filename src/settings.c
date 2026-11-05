// settings.c - persistence: settings.conf in $XDG_CONFIG_HOME/keychain-manager (UI preferences only, never secrets
// or item data); the first run takes File Explorer's theme, accent and font so the two apps match

static void settings_dir(char *out, size_t cap) {
    char c[PATH_MAX];
    config_home(c, sizeof c);
    snprintf(out, cap, "%s/keychain-manager", c);
}

static void themes_dir(char *out, size_t cap) {
    char d[PATH_MAX];
    settings_dir(d, sizeof d);
    snprintf(out, cap, "%s/themes", d);
}

static void settings_defaults(void) {
    snprintf(A.theme_name, sizeof A.theme_name, "Dark");
    A.ui_font[0] = 0;
    g_accent_custom = 0;
    A.font_size = 13;
    A.zoom = 1.f;
    A.win_w = 1180;
    A.win_h = 720;
    A.maximized = false;
    A.side_w = 240;
    A.insp_on = true;
    A.insp_w = 340;
    for (int c = 0; c < COL_COUNT; c++) { A.colw[c] = g_coldef[c].def_w; A.col_on[c] = c != COL_CREATED && c != COL_KEYRING; }
    A.colw[COL_NAME] = 160;
    A.sort_col = COL_NAME;
    A.sort_desc = false;
    A.clip_secs = 30;
    A.idle_lock_min = 0;
    A.alt_rows = false;
    A.smooth_scroll = true;
    A.title_bar = TITLEBAR_AUTO;
    A.side_path[0] = 0;
    snprintf(A.renderer, sizeof A.renderer, "auto");
    keys_defaults();
}

static void accent_parse(const char *v) {
    u32 c;
    g_accent_custom = 0;
    for (int i = 0; i < ACCENT_COUNT; i++)
        if (str_ieq(v, g_accents[i].name)) g_accent_custom = 0x01000000u | g_accents[i].rgb;
    if (!g_accent_custom && parse_hex_color(v, &c)) g_accent_custom = 0x01000000u | c;
}

static void accent_name(char *out, size_t cap) {
    out[0] = 0;
    if (!g_accent_custom) return;
    for (int i = 0; i < ACCENT_COUNT; i++)
        if ((g_accent_custom & 0xFFFFFF) == g_accents[i].rgb) { snprintf(out, cap, "%s", g_accents[i].name); return; }
    snprintf(out, cap, "#%06X", (unsigned)(g_accent_custom & 0xFFFFFF));
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

// File Explorer's look (its settings.conf, read once at first run; never written).
static void settings_import_fe(void) {
    char c[PATH_MAX], path[PATH_MAX + 48];
    config_home(c, sizeof c);
    snprintf(path, sizeof path, "%s/file-explorer/settings.conf", c);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!strcmp(k, "theme") && v[0]) snprintf(A.theme_name, sizeof A.theme_name, "%s", v);
        else if (!strcmp(k, "accent")) accent_parse(v);
        else if (!strcmp(k, "font")) snprintf(A.ui_font, sizeof A.ui_font, "%s", v);
        else if (!strcmp(k, "font_size")) A.font_size = CLAMP(atoi(v), 9, 24);
        else if (!strcmp(k, "title_bar")) A.title_bar = !strcmp(v, "system") ? TITLEBAR_SYSTEM : !strcmp(v, "app") ? TITLEBAR_APP : TITLEBAR_AUTO;
        else if (!strcmp(k, "alt_rows")) A.alt_rows = atoi(v);
        else if (!strcmp(k, "smooth_scroll")) A.smooth_scroll = atoi(v);
    }
    fclose(f);
    // its own colour schemes too (themes/*.json): copied, so they show up here
    char from[PATH_MAX + 48], to[PATH_MAX];
    snprintf(from, sizeof from, "%s/file-explorer/themes", c);
    themes_dir(to, sizeof to);
    DIR *d = opendir(from);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        size_t l = strlen(de->d_name);
        if (l < 6 || strcmp(de->d_name + l - 5, ".json")) continue;
        char src[PATH_MAX * 2], dst[PATH_MAX * 2];
        snprintf(src, sizeof src, "%s/%s", from, de->d_name);
        snprintf(dst, sizeof dst, "%s/%s", to, de->d_name);
        if (access(dst, F_OK) == 0) continue;
        FILE *in = fopen(src, "r");
        if (!in) continue;
        static char buf[65536];
        size_t n = fread(buf, 1, sizeof buf - 1, in);
        fclose(in);
        buf[n] = 0;
        mkdir_p(to, 0755);
        write_file_atomic(dst, buf);
    }
    closedir(d);
}

static void settings_load(void) {
    char dir[PATH_MAX], path[PATH_MAX + 32];
    settings_dir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/settings.conf", dir);
    FILE *f = fopen(path, "r");
    if (!f) { // first run
        settings_import_fe();
        A.fe_imported = true;
        A.settings_dirty = true;
        return;
    }
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!strcmp(k, "theme")) snprintf(A.theme_name, sizeof A.theme_name, "%s", v);
        else if (!strcmp(k, "accent")) accent_parse(v);
        else if (!strcmp(k, "font")) snprintf(A.ui_font, sizeof A.ui_font, "%s", v);
        else if (!strcmp(k, "font_size")) A.font_size = CLAMP(atoi(v), 9, 24);
        else if (!strcmp(k, "zoom")) A.zoom = CLAMP((f32)atof(v), 0.6f, 2.5f);
        else if (!strcmp(k, "window")) sscanf(v, "%dx%d", &A.win_w, &A.win_h);
        else if (!strcmp(k, "maximized")) A.maximized = atoi(v);
        else if (!strcmp(k, "sidebar_width")) A.side_w = (f32)atof(v);
        else if (!strcmp(k, "details")) A.insp_on = atoi(v);
        else if (!strcmp(k, "details_width")) A.insp_w = (f32)atof(v);
        else if (!strcmp(k, "clear_clipboard")) A.clip_secs = CLAMP(atoi(v), 0, 3600);
        else if (!strcmp(k, "idle_lock")) A.idle_lock_min = CLAMP(atoi(v), 0, 24 * 60);
        else if (!strcmp(k, "alt_rows")) A.alt_rows = atoi(v);
        else if (!strcmp(k, "smooth_scroll")) A.smooth_scroll = atoi(v);
        else if (!strcmp(k, "title_bar")) A.title_bar = !strcmp(v, "system") ? TITLEBAR_SYSTEM : !strcmp(v, "app") ? TITLEBAR_APP : TITLEBAR_AUTO;
        else if (!strcmp(k, "renderer")) snprintf(A.renderer, sizeof A.renderer, "%s", v);
        else if (!strcmp(k, "keyring")) snprintf(A.side_path, sizeof A.side_path, "%s", v);
        else if (!strcmp(k, "key")) keys_load_line(v);
        else if (!strcmp(k, "sort")) {
            for (int c = 0; c < COL_COUNT; c++)
                if (!strncmp(v, g_coldef[c].key, strlen(g_coldef[c].key))) { A.sort_col = c; A.sort_desc = strstr(v, ":desc") != NULL; }
        } else if (!strcmp(k, "name_width")) A.colw[COL_NAME] = CLAMP((f32)atof(v), 120.f, 1200.f);
        else if (!strcmp(k, "columns")) { // account:180,where:200 (shown columns + widths; name is always there)
            for (int c = COL_ACCOUNT; c < COL_COUNT; c++) A.col_on[c] = false;
            char buf[512];
            snprintf(buf, sizeof buf, "%s", v);
            for (char *s = buf, *tok; (tok = strsep(&s, ","));) {
                char *colon = strchr(tok, ':');
                if (colon) *colon = 0;
                for (int c = COL_ACCOUNT; c < COL_COUNT; c++)
                    if (!strcmp(tok, g_coldef[c].key)) {
                        A.col_on[c] = true;
                        if (colon) A.colw[c] = CLAMP((f32)atof(colon + 1), 60.f, 600.f);
                    }
            }
        }
    }
    fclose(f);
    A.win_w = CLAMP(A.win_w, 720, 8000);
    A.win_h = CLAMP(A.win_h, 420, 8000);
}

static void settings_save(void) {
    if (A.selftest && !getenv("KM_SELFTEST_SAVE")) { A.settings_dirty = false; return; }
    char dir[PATH_MAX], path[PATH_MAX + 32];
    settings_dir(dir, sizeof dir);
    mkdir_p(dir, 0700);
    snprintf(path, sizeof path, "%s/settings.conf", dir);
    if (A.win && !A.screenshot) {
        A.maximized = window_maximized();
        if (!A.maximized) {
            int w, h;
            SDL_GetWindowSize(A.win, &w, &h);
            A.win_w = w;
            A.win_h = h;
        }
    }
    char *text = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&text, &len);
    if (!f) return;
    char acc[32];
    accent_name(acc, sizeof acc);
    fprintf(f, "# Keychain Manager settings (UI only: nothing about your keyrings is stored here)\n");
    fprintf(f, "theme=%s\naccent=%s\nfont=%s\nfont_size=%d\nzoom=%.2f\n", g_nthemes ? g_themes[g_theme_cur].name : A.theme_name, acc,
            A.ui_font, A.font_size, A.zoom);
    fprintf(f, "window=%dx%d\nmaximized=%d\nsidebar_width=%.0f\ndetails=%d\ndetails_width=%.0f\n", A.win_w, A.win_h, A.maximized,
            A.side_w, A.insp_on, A.insp_w);
    fprintf(f, "clear_clipboard=%d\nidle_lock=%d\nalt_rows=%d\nsmooth_scroll=%d\ntitle_bar=%s\nrenderer=%s\n", A.clip_secs,
            A.idle_lock_min, A.alt_rows, A.smooth_scroll,
            A.title_bar == TITLEBAR_APP ? "app" : A.title_bar == TITLEBAR_SYSTEM ? "system" : "auto", A.renderer);
    fprintf(f, "name_width=%.0f\n", A.colw[COL_NAME]);
    fprintf(f, "sort=%s%s\ncolumns=", g_coldef[A.sort_col].key, A.sort_desc ? ":desc" : "");
    bool first = true;
    for (int c = COL_ACCOUNT; c < COL_COUNT; c++) {
        if (!A.col_on[c]) continue;
        fprintf(f, "%s%s:%.0f", first ? "" : ",", g_coldef[c].key, A.colw[c]);
        first = false;
    }
    fprintf(f, "\nkeyring=%s\n", A.side_sel >= 0 && A.side_sel < A.ncolls ? A.colls[A.side_sel].path : "");
    keys_save(f);
    fclose(f);
    if (!file_equals(path, text)) write_file_atomic(path, text);
    free(text);
    A.settings_dirty = false;
    A.settings_saved = now_ns();
}
