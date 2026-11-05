// model.c - the keyrings and items as the UI holds them (main thread): events from secret.c, what each item is
// (kind, account, where), the filtered + sorted view, selection that survives updates

// --- fuzzy matching (Ctrl+P) ----------------------------------------------------

// Subsequence match of q in s (case-insensitive), scored like fzf v1: rewards matches at word boundaries and
// consecutive runs, penalizes gaps and long strings. Returns -1 if q doesn't match. pos receives up to 32 indices.
static int fuzzy_match(const char *s, int n, const char *q, int qn, u16 *pos) {
    if (qn <= 0) return 0;
    int qi = 0, end = -1;
    for (int i = 0; i < n; i++) {
        if (ascii_lower((u8)s[i]) == ascii_lower((u8)q[qi]) && ++qi == qn) { end = i; break; }
    }
    if (end < 0) return -1;
    qi = qn - 1;
    int start = end;
    for (int i = end; i >= 0; i--) {
        if (ascii_lower((u8)s[i]) == ascii_lower((u8)q[qi]) && --qi < 0) { start = i; break; }
    }
    int score = 0, last = -2;
    qi = 0;
    for (int i = start; i <= end && qi < qn; i++) {
        if (ascii_lower((u8)s[i]) != ascii_lower((u8)q[qi])) continue;
        int b = 16;
        char prev = i ? s[i - 1] : ' ';
        if (prev == ' ' || prev == '/' || prev == ':') b += 30;
        else if (prev == '_' || prev == '-' || prev == '.' || prev == '@') b += 20;
        else if (prev >= 'a' && prev <= 'z' && s[i] >= 'A' && s[i] <= 'Z') b += 16;
        if (last == i - 1) b += 18;
        if (s[i] == q[qi]) b += 1;
        score += b;
        if (pos && qi < 32) pos[qi] = (u16)i;
        last = i;
        qi++;
    }
    score -= (end - start + 1 - qn);
    score -= n / 4;
    return MAX(score, 1);
}

// --- items and keyrings --------------------------------------------------------

static Item *item_by_path(const char *path) {
    if (!A.map_cap || !path) return NULL;
    u32 h = (u32)(str_hash(path) & (A.map_cap - 1));
    while (A.map[h]) {
        if (!strcmp(A.map[h]->path, path)) return A.map[h];
        h = (h + 1) & (A.map_cap - 1);
    }
    return NULL;
}

static void map_rebuild(void) {
    u32 cap = 64;
    while (cap < (u32)A.nitems * 2 + 2) cap *= 2;
    free(A.map);
    A.map = xcalloc(cap, sizeof *A.map);
    A.map_cap = cap;
    for (int i = 0; i < A.nitems; i++) {
        u32 h = (u32)(str_hash(A.items[i]->path) & (cap - 1));
        while (A.map[h]) h = (h + 1) & (cap - 1);
        A.map[h] = A.items[i];
    }
}

static const char *attr_get(const Item *it, const char *k) {
    for (int i = 0; i < it->nattrs; i++)
        if (!strcmp(it->attrs[i].k, k)) return it->attrs[i].v;
    return NULL;
}

static int coll_by_path(const char *path) {
    for (int i = 0; i < A.ncolls; i++)
        if (path && !strcmp(A.colls[i].path, path)) return i;
    return -1;
}

static bool icontains(const char *hay, const char *needle) {
    if (!hay || !needle) return false;
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (str_iprefix(hay, needle, n)) return true;
    return false;
}

static const char *first_attr(const Item *it, const char *const *keys) {
    for (int i = 0; keys[i]; i++) {
        const char *v = attr_get(it, keys[i]);
        if (v && v[0]) return v;
    }
    return NULL;
}

// What an item is, from its schema and attributes: kind label, icon, account, where it's used.
static void item_derive(Item *it) {
    static const char *const acct_keys[] = {"username", "user", "account", "login", "email", "user_name", "username_value",
                                            "UserName", "Username", "uid", NULL};
    static const char *const where_keys[] = {"server", "domain", "host", "origin_url", "signon_realm", "url", "service",
                                             "application", "app_id", "appid", "Url", "URL", "Title", NULL};
    const char *schema = attr_get(it, "xdg:schema");
    const char *app = attr_get(it, "application");
    const char *unique = attr_get(it, "unique");
    const char *acc = first_attr(it, acct_keys), *where = first_attr(it, where_keys);
    const char *kind = NULL;
    int icon = ICON_KEY;
    if (!schema) schema = "";
    if (!strncmp(schema, "org.freedesktop.NetworkManager", 30) || attr_get(it, "connection-uuid")) {
        kind = "Network connection";
        icon = ICON_WIFI;
        const char *sk = attr_get(it, "setting-name");
        if (!where && sk) where = sk;
    } else if (!strcmp(schema, "org.gnome.keyring.NetworkPassword")) {
        const char *proto = attr_get(it, "protocol");
        kind = "Network password";
        icon = proto && (!strcmp(proto, "http") || !strcmp(proto, "https")) ? ICON_HTTP : ICON_DATABASE;
        if (proto && (!strcmp(proto, "imap") || !strcmp(proto, "smtp") || !strcmp(proto, "pop"))) icon = ICON_MAIL;
        if (proto && (!strcmp(proto, "sftp") || !strcmp(proto, "ssh"))) icon = ICON_CONSOLE;
    } else if (icontains(schema, "chrome") || icontains(schema, "chromium") ||
               (app && (icontains(app, "chrom") || icontains(app, "brave") || icontains(app, "vivaldi") || icontains(app, "edge") ||
                        icontains(app, "opera") || icontains(app, "firefox")))) {
        kind = "Browser storage key";
        icon = ICON_BROWSER;
        if (app) where = app;
    } else if (!strcmp(schema, "org.git.Password") || icontains(schema, "git") || (it->label && !strncmp(it->label, "Git: ", 5))) {
        kind = "Git credential";
        icon = ICON_GIT;
    } else if (!strcmp(schema, "org.gnome.keyring.Note")) {
        kind = "Note";
        icon = ICON_CERTIFICATE;
    } else if (unique && !strncmp(unique, "ssh-store:", 10)) {
        kind = "SSH key passphrase";
        icon = ICON_CONSOLE;
        where = unique + 10;
    } else if (attr_get(it, "keygrip") || icontains(schema, "gnupg") || icontains(schema, "gpg")) {
        kind = "GnuPG passphrase";
        icon = ICON_CERTIFICATE;
    } else if (icontains(schema, "evolution") || icontains(schema, "geary") || icontains(schema, "mail") ||
               icontains(schema, "thunderbird") || (app && icontains(app, "mail"))) {
        kind = "Mail account";
        icon = ICON_MAIL;
    } else if (icontains(schema, "docker") || (it->label && icontains(it->label, "docker"))) {
        kind = "Docker credential";
        icon = ICON_DOCKER;
    } else if ((app && (icontains(app, "vscode") || !strcmp(app, "code") || icontains(app, "visual studio code"))) ||
               icontains(schema, "vscode") || (it->label && icontains(it->label, "vscode"))) {
        kind = "VS Code secret";
        icon = ICON_VSCODE;
    } else if (app && icontains(app, "python keyring")) {
        kind = "Python keyring";
        icon = ICON_PYTHON;
    } else if (icontains(schema, "kwallet") || attr_get(it, "kwallet-folder")) {
        kind = "KWallet entry";
        icon = ICON_KEYRING;
        const char *f = attr_get(it, "kwallet-folder");
        if (f && !where) where = f;
    } else if (!strcmp(schema, "org.freedesktop.Secret.Generic") || !schema[0]) {
        kind = "Password";
        if (app && !where) where = app;
    }
    char kbuf[64];
    if (!kind) { // an application's own schema: its last part ("org.gnome.Foo.Bar" -> "Bar")
        const char *dot = strrchr(schema, '.');
        snprintf(kbuf, sizeof kbuf, "%s", dot && dot[1] ? dot + 1 : schema);
        kind = kbuf;
        icon = ICON_APP;
    }
    snprintf(it->kind, sizeof it->kind, "%s", kind);
    snprintf(it->account, sizeof it->account, "%s", acc ? acc : "");
    snprintf(it->where, sizeof it->where, "%s", where ? where : "");
    it->icon = icon;
    // search text: label, kind, then the attribute values (lower case), \x01-separated
    size_t cap = strlen(it->label) + 2;
    for (int i = 0; i < it->nattrs; i++) cap += strlen(it->attrs[i].v) + 2;
    cap += strlen(it->kind) + 2;
    free(it->hay);
    it->hay = xmalloc(cap);
    size_t o = (size_t)snprintf(it->hay, cap, "%s\x01%s", it->label, it->kind);
    for (int i = 0; i < it->nattrs; i++) // (values only: every item has "xdg:schema" and the like)
        if (strcmp(it->attrs[i].k, "xdg:schema")) o += (size_t)snprintf(it->hay + o, cap - o, "\x01%s", it->attrs[i].v);
    for (char *c = it->hay; *c; c++) *c = (char)ascii_lower((u8)*c);
}

static void item_free(Item *it) {
    free(it->path);
    free(it->coll_path);
    free(it->label);
    free(it->hay);
    attrs_free(it->attrs, it->nattrs);
    free(it);
}

// Fill (or refresh) an item from what the service said; takes the strings out of ii.
static void item_take(Item *it, ItemInfo *ii) {
    free(it->label);
    it->label = ii->label ? ii->label : strdup("");
    ii->label = NULL;
    attrs_free(it->attrs, it->nattrs);
    it->attrs = ii->attrs;
    it->nattrs = ii->nattrs;
    ii->attrs = NULL;
    ii->nattrs = 0;
    if (!it->coll_path || (ii->coll && strcmp(it->coll_path, ii->coll))) {
        free(it->coll_path);
        it->coll_path = ii->coll ? strdup(ii->coll) : strdup("");
    }
    it->created = ii->created;
    it->modified = ii->modified;
    it->locked = ii->locked;
    item_derive(it);
}

static Item *item_add(ItemInfo *ii) {
    Item *it = xcalloc(1, sizeof *it);
    it->path = ii->path;
    ii->path = NULL;
    item_take(it, ii);
    if (A.nitems == A.cap_items) A.items = xrealloc(A.items, (size_t)(A.cap_items = A.cap_items ? A.cap_items * 2 : 256) * sizeof *A.items);
    A.items[A.nitems++] = it;
    return it;
}

static void item_remove(const char *path) {
    for (int i = 0; i < A.nitems; i++) {
        if (strcmp(A.items[i]->path, path)) continue;
        if (A.rev_item && !strcmp(A.rev_item, path)) reveal_clear();
        Item *gone = A.items[i];
        for (int k = 0; k < A.nview; k++) { // out of the view (the cursor stays at the same row)
            if (A.view[k] != gone) continue;
            memmove(&A.view[k], &A.view[k + 1], (size_t)(A.nview - k - 1) * sizeof *A.view);
            A.nview--;
            if (A.cursor > k) A.cursor--;
            else if (A.cursor == k) { A.cursor_hint = k; A.cursor = -1; }
            break;
        }
        for (int k = 0; k < A.pal_n; k++) // and out of the palette's results
            if (A.pal[k].item == gone) {
                memmove(&A.pal[k], &A.pal[k + 1], (size_t)(A.pal_n - k - 1) * sizeof *A.pal);
                A.pal_n--;
                A.pal_sel = MIN(A.pal_sel, MAX(0, A.pal_n - 1));
                k--;
            }
        item_free(gone);
        memmove(&A.items[i], &A.items[i + 1], (size_t)(A.nitems - i - 1) * sizeof *A.items);
        A.nitems--;
        break;
    }
}

static void colls_count(void) {
    for (int i = 0; i < A.ncolls; i++) A.colls[i].nitems = 0;
    for (int i = 0; i < A.nitems; i++) {
        Item *it = A.items[i];
        it->coll = coll_by_path(it->coll_path);
        if (it->coll >= 0) A.colls[it->coll].nitems++;
    }
}

static void coll_set(Coll *c, CollInfo *ci) {
    free(c->path);
    free(c->label);
    c->path = ci->path;
    c->label = ci->label;
    ci->path = ci->label = NULL;
    c->locked = ci->locked;
    c->is_default = ci->is_default;
    c->created = ci->created;
    c->modified = ci->modified;
}

static int cmp_coll(const void *a, const void *b) { // the default keyring first, then by name; "session" last
    const Coll *x = a, *y = b;
    bool xs = !strcmp(path_basename(x->path), "session"), ys = !strcmp(path_basename(y->path), "session");
    if (x->is_default != y->is_default) return x->is_default ? -1 : 1;
    if (xs != ys) return xs ? 1 : -1;
    return natcmp(x->label, y->label);
}

static void colls_sort(void) {
    char keep[512] = "";
    if (A.side_sel >= 0 && A.side_sel < A.ncolls) snprintf(keep, sizeof keep, "%s", A.colls[A.side_sel].path);
    qsort(A.colls, (size_t)A.ncolls, sizeof *A.colls, cmp_coll);
    if (keep[0]) A.side_sel = coll_by_path(keep);
}

// --- the view: what the list shows -----------------------------------------------

static const char *coll_label(int c) {
    if (c < 0 || c >= A.ncolls) return "";
    return A.colls[c].label[0] ? A.colls[c].label : path_basename(A.colls[c].path);
}

static int cmp_dir; // sort comparator state (qsort has no context argument)
static int cmp_col;

static int cmp_u64(u64 a, u64 b) { return a < b ? -1 : a > b; }

static int cmp_view(const void *pa, const void *pb) {
    const Item *a = *(Item *const *)pa, *b = *(Item *const *)pb;
    int c = 0;
    switch (cmp_col) {
    case COL_ACCOUNT: c = natcmp(a->account, b->account); break;
    case COL_WHERE: c = natcmp(a->where, b->where); break;
    case COL_KIND: c = natcmp(a->kind, b->kind); break;
    case COL_KEYRING: c = natcmp(coll_label(a->coll), coll_label(b->coll)); break;
    case COL_MODIFIED: c = cmp_u64(a->modified, b->modified); break;
    case COL_CREATED: c = cmp_u64(a->created, b->created); break;
    default: break;
    }
    if (!c) {
        bool ae = !a->label[0], be = !b->label[0]; // nameless items at the end
        c = ae != be ? (ae ? 1 : -1) : natcmp(a->label, b->label);
        if (cmp_col != COL_NAME) return c; // (secondary order: always by name, ascending)
    }
    if (!c) c = strcmp(a->path, b->path);
    return cmp_dir * c;
}

// Every word of the query must appear (case-insensitive) in the label or an attribute.
static bool item_matches(const Item *it, const char *q) {
    if (!q[0]) return true;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", q);
    char *save = NULL;
    for (char *w = strtok_r(buf, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
        for (char *c = w; *c; c++) *c = (char)ascii_lower((u8)*c);
        if (!strstr(it->hay, w)) return false;
    }
    return true;
}

static Item *cursor_item(void) { return A.cursor >= 0 && A.cursor < A.nview ? A.view[A.cursor] : NULL; }

static void sel_count_update(void) {
    A.nsel = 0;
    for (int i = 0; i < A.nview; i++) A.nsel += A.view[i]->sel;
}

// Rebuild the view (filter + sort); the cursor stays on its item, selection on its items.
static void view_rebuild(void) {
    u64 t0 = now_ns();
    char keep[512] = "";
    Item *cur = cursor_item();
    if (A.select_path[0] && item_by_path(A.select_path)) snprintf(keep, sizeof keep, "%s", A.select_path); // (asked for)
    else if (cur) snprintf(keep, sizeof keep, "%s", cur->path);
    if (A.cap_view < A.nitems) A.view = xrealloc(A.view, (size_t)(A.cap_view = MAX(A.nitems, 64)) * sizeof *A.view);
    const char *q = A.search.buf;
    A.nview = 0;
    for (int i = 0; i < A.nitems; i++) {
        Item *it = A.items[i];
        bool in = A.side_sel < 0 || it->coll == A.side_sel;
        if (in && item_matches(it, q)) A.view[A.nview++] = it;
        else it->sel = false;
    }
    cmp_col = A.sort_col;
    cmp_dir = A.sort_desc ? -1 : 1;
    qsort(A.view, (size_t)A.nview, sizeof *A.view, cmp_view);
    A.cursor = -1;
    for (int i = 0; i < A.nview && keep[0]; i++)
        if (!strcmp(A.view[i]->path, keep)) A.cursor = i;
    if (A.cursor >= 0 && A.select_path[0] && !strcmp(keep, A.select_path)) { // a requested selection: just that item
        for (int i = 0; i < A.nview; i++) A.view[i]->sel = i == A.cursor;
        A.select_path[0] = 0;
        A.ensure_visible = true;
    }
    if (A.cursor < 0 && A.nview) A.cursor = MIN(A.cursor_hint, A.nview - 1);
    if (A.cursor < 0 && A.nview) A.cursor = 0;
    A.cursor_hint = MAX(A.cursor, 0);
    sel_count_update();
    if (!A.nsel && cur && A.cursor >= 0 && strcmp(cur->path, A.view[A.cursor]->path)) {} // (cursor moved to a neighbour)
    A.view_ms = (f32)((now_ns() - t0) / 1e6);
    A.redraw = true;
}

static void select_only(int i) {
    for (int k = 0; k < A.nview; k++) A.view[k]->sel = k == i;
    A.cursor = i;
    A.anchor = i;
    if (i >= 0) A.cursor_hint = i;
    A.ensure_visible = true;
    sel_count_update();
}

// The items an action is about: the selection, or the item under the cursor.
static int selected_items(Item ***out) {
    Item **v = xmalloc((size_t)MAX(A.nview, 1) * sizeof *v);
    int n = 0;
    for (int i = 0; i < A.nview; i++)
        if (A.view[i]->sel) v[n++] = A.view[i];
    if (!n && cursor_item()) v[n++] = cursor_item();
    *out = v;
    return n;
}

static void side_select(int s) {
    if (s != A.side_sel) {
        A.side_sel = s;
        A.scroll = A.scroll_target = 0;
        A.cursor = -1;
        A.cursor_hint = 0;
        for (int i = 0; i < A.nitems; i++) A.items[i]->sel = false;
        snprintf(A.side_path, sizeof A.side_path, "%s", s >= 0 && s < A.ncolls ? A.colls[s].path : "");
        A.settings_dirty = true;
    }
    view_rebuild();
}

// --- events from the Secret Service thread ------------------------------------------

static void model_replace_all(SsEvent *ev) {
    // keep Item objects for paths that are still there (selection, revealed secret), drop the rest
    char keep_side[512];
    snprintf(keep_side, sizeof keep_side, "%s", A.side_sel >= 0 && A.side_sel < A.ncolls ? A.colls[A.side_sel].path : A.side_path);
    for (int i = 0; i < A.ncolls; i++) { free(A.colls[i].path); free(A.colls[i].label); }
    A.ncolls = ev->ncolls;
    A.colls = xrealloc(A.colls, (size_t)MAX(A.ncolls, 1) * sizeof *A.colls);
    memset(A.colls, 0, (size_t)MAX(A.ncolls, 1) * sizeof *A.colls);
    for (int i = 0; i < A.nitems; i++) A.items[i]->seen = false;
    for (int c = 0; c < ev->ncolls; c++) {
        CollInfo *ci = &ev->colls[c];
        for (int k = 0; k < ci->nitems; k++) {
            Item *it = item_by_path(ci->items[k].path);
            if (it) { item_take(it, &ci->items[k]); it->seen = true; }
            else { it = item_add(&ci->items[k]); it->seen = true; }
        }
        coll_set(&A.colls[c], ci);
    }
    for (int i = A.nitems - 1; i >= 0; i--)
        if (!A.items[i]->seen) item_remove(A.items[i]->path);
    map_rebuild();
    colls_sort();
    A.side_sel = keep_side[0] ? coll_by_path(keep_side) : -1;
    if (keep_side[0] && A.side_sel < 0 && !A.loaded) A.side_sel = -1; // (remembered keyring is gone: all items)
    colls_count();
    A.loaded = true;
    A.load_ms = (f32)ev->ms;
    view_rebuild();
}

// One keyring again (lock / unlock / rename, or its items changed).
static void model_replace_coll(CollInfo *ci) {
    int c = coll_by_path(ci->path);
    if (c < 0) {
        A.colls = xrealloc(A.colls, (size_t)(A.ncolls + 1) * sizeof *A.colls);
        memset(&A.colls[A.ncolls], 0, sizeof *A.colls);
        c = A.ncolls++;
    }
    for (int i = 0; i < A.nitems; i++)
        if (!strcmp(A.items[i]->coll_path, ci->path)) A.items[i]->seen = false;
        else A.items[i]->seen = true;
    for (int k = 0; k < ci->nitems; k++) {
        Item *it = item_by_path(ci->items[k].path);
        if (it) item_take(it, &ci->items[k]);
        else it = item_add(&ci->items[k]);
        it->seen = true;
    }
    bool was_default = A.colls[c].is_default;
    coll_set(&A.colls[c], ci);
    A.colls[c].is_default = ci->is_default || was_default; // (a one-keyring reload doesn't know other aliases)
    for (int i = A.nitems - 1; i >= 0; i--)
        if (!A.items[i]->seen) item_remove(A.items[i]->path);
    map_rebuild();
    colls_sort();
    colls_count();
    view_rebuild();
}

static void on_request_done(SsEvent *ev); // app.c: requests the UI waits for

static void ss_event(SsEvent *ev) {
    switch (ev->type) {
    case SE_STATE:
        A.ss_state = ev->state;
        snprintf(A.backend, sizeof A.backend, "%s", ev->backend);
        snprintf(A.ss_msg, sizeof A.ss_msg, "%s", ev->msg);
        if (ev->state == SS_READY) A.encrypted = !strcmp(ev->msg, "encrypted");
        if (ev->state == SS_MISSING || ev->state == SS_FAILED) A.loaded = true; // (nothing more is coming)
        break;
    case SE_COLLECTIONS: model_replace_all(ev); break;
    case SE_COLLECTION:
        for (int i = 0; i < ev->ncolls; i++) model_replace_coll(&ev->colls[i]);
        break;
    case SE_ITEMS: {
        bool added = false;
        for (int i = 0; i < ev->nitems; i++) {
            Item *it = item_by_path(ev->items[i].path);
            if (it) item_take(it, &ev->items[i]);
            else { item_add(&ev->items[i]); added = true; }
        }
        if (added) { map_rebuild(); colls_count(); }
        view_rebuild();
        break;
    }
    case SE_ITEM_GONE:
        if (item_by_path(ev->path)) {
            item_remove(ev->path);
            map_rebuild();
            colls_count();
            view_rebuild();
        }
        break;
    case SE_COLL_GONE: {
        int c = coll_by_path(ev->path);
        if (c >= 0) {
            free(A.colls[c].path);
            free(A.colls[c].label);
            memmove(&A.colls[c], &A.colls[c + 1], (size_t)(A.ncolls - c - 1) * sizeof *A.colls);
            A.ncolls--;
            for (int i = A.nitems - 1; i >= 0; i--)
                if (!strcmp(A.items[i]->coll_path, ev->path)) item_remove(A.items[i]->path);
            map_rebuild();
            if (A.side_sel == c) A.side_sel = -1;
            else if (A.side_sel > c) A.side_sel--;
            colls_count();
            view_rebuild();
        }
        break;
    }
    case SE_SECRET: on_secret(ev); break;
    case SE_DONE: on_request_done(ev); break;
    case SE_PROMPT: A.prompt_open = ev->state; break;
    }
    ss_event_free(ev);
    A.redraw = true;
}
