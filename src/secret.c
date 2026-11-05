// secret.c - Secret Service client: one thread owns the D-Bus connection, runs the UI's requests (list, read,
// write, delete, lock, unlock), follows the service's change signals and prompts, and posts events to the main
// thread. The UI never blocks on the bus. Nothing is cached on disk; secrets only travel on request.

// --- what the main thread gets -------------------------------------------------

typedef struct {
    char *path, *coll, *label;
    Attr *attrs;
    int nattrs;
    u64 created, modified;
    bool locked;
} ItemInfo;

typedef struct {
    char *path, *label;
    bool locked, is_default;
    u64 created, modified;
    ItemInfo *items;
    int nitems;
} CollInfo;

enum {
    SE_STATE,       // service found / missing / failed (state, backend, msg)
    SE_COLLECTIONS, // everything, replaces what the UI has (colls)
    SE_COLLECTION,  // one keyring and its items (colls[0])
    SE_ITEMS,       // added or changed items (items)
    SE_ITEM_GONE,   // path
    SE_COLL_GONE,   // path
    SE_SECRET,      // a secret that was asked for (req, path, secret, ctype, purpose)
    SE_DONE,        // a request finished (req, ok, msg)
    SE_PROMPT,      // the service shows (state 1) or closed (state 0) a password window
};

enum { SS_CONNECTING, SS_READY, SS_MISSING, SS_FAILED };

typedef struct {
    int type, state, purpose;
    u32 req;
    bool ok;
    char backend[64], msg[256];
    CollInfo *colls;
    int ncolls;
    ItemInfo *items;
    int nitems;
    char *path, *ctype;
    SecBuf secret;
    f64 ms; // how long a full load took
} SsEvent;

static void ss_event(SsEvent *ev); // app.c (main thread): takes ownership of ev

static void item_info_free(ItemInfo *it) {
    free(it->path);
    free(it->coll);
    free(it->label);
    attrs_free(it->attrs, it->nattrs);
}

static void coll_info_free(CollInfo *c) {
    free(c->path);
    free(c->label);
    for (int i = 0; i < c->nitems; i++) item_info_free(&c->items[i]);
    free(c->items);
}

static void ss_event_free(SsEvent *ev) {
    for (int i = 0; i < ev->ncolls; i++) coll_info_free(&ev->colls[i]);
    free(ev->colls);
    for (int i = 0; i < ev->nitems; i++) item_info_free(&ev->items[i]);
    free(ev->items);
    free(ev->path);
    free(ev->ctype);
    sec_free(&ev->secret);
    free(ev);
}

static void ss_event_main(void *arg) { ss_event(arg); }

static void ss_post(SsEvent *ev) { main_post(ss_event_main, ev); }

static SsEvent *ev_new(int type) {
    SsEvent *ev = xcalloc(1, sizeof *ev);
    ev->type = type;
    return ev;
}

static void post_done(u32 req, bool ok, const char *fmt, ...) {
    SsEvent *ev = ev_new(SE_DONE);
    ev->req = req;
    ev->ok = ok;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev->msg, sizeof ev->msg, fmt, ap);
    va_end(ap);
    ss_post(ev);
}

// --- what the main thread asks for -------------------------------------------------

enum {
    RQ_RELOAD,      // everything again
    RQ_SECRET,      // path, purpose
    RQ_UNLOCK,      // paths (keyrings or items)
    RQ_LOCK,        // paths (keyrings)
    RQ_SET_ITEM,    // path; label / attrs / secret when their set_ flag is on
    RQ_CREATE_ITEM, // target (keyring), label, attrs, secret, ctype
    RQ_DELETE,      // paths (items)
    RQ_TRANSFER,    // paths (items) -> target keyring; move deletes the originals
    RQ_CREATE_COLL, // label
    RQ_DELETE_COLL, // path
    RQ_RENAME_COLL, // path, label
    RQ_SET_DEFAULT, // path
    RQ_DISMISS,     // close the service's password window
    RQ_TEST_CREATE, // self-tests only (gnome-keyring): keyring label + master password in secret, no prompt
    RQ_TEST_UNLOCK, // self-tests only: path + master password
};

typedef struct SsReq {
    int op, purpose;
    u32 id;
    char *path, *target, *label, *ctype;
    char **paths;
    int npaths;
    Attr *attrs;
    int nattrs;
    bool set_label, set_attrs, set_secret, move;
    SecBuf secret;
    struct SsReq *next;
} SsReq;

static void req_free(SsReq *r) {
    if (!r) return;
    free(r->path);
    free(r->target);
    free(r->label);
    free(r->ctype);
    strv_free(r->paths, r->npaths);
    attrs_free(r->attrs, r->nattrs);
    sec_free(&r->secret);
    free(r);
}

static struct {
    pthread_mutex_t mtx;
    SsReq *head, *tail;
    int wake_fd;
    atomic_uint next_id;
    bool test_bus; // the self-test harness's private bus: test-only requests allowed
} g_ss = {PTHREAD_MUTEX_INITIALIZER, NULL, NULL, -1, 1, false};

static SsReq *ss_req(int op) {
    SsReq *r = xcalloc(1, sizeof *r);
    r->op = op;
    r->id = atomic_fetch_add(&g_ss.next_id, 1);
    return r;
}

// Hand a request to the thread (it owns it from here). Returns its id (SE_DONE / SE_SECRET carry it back).
static u32 ss_submit(SsReq *r) {
    u32 id = r->id;
    pthread_mutex_lock(&g_ss.mtx);
    if (g_ss.tail) g_ss.tail->next = r;
    else g_ss.head = r;
    g_ss.tail = r;
    pthread_mutex_unlock(&g_ss.mtx);
    u64 one = 1;
    if (g_ss.wake_fd >= 0 && write(g_ss.wake_fd, &one, sizeof one) < 0) {} // (eventfd: can't fill up)
    return id;
}

static SsReq *ss_take(void) {
    pthread_mutex_lock(&g_ss.mtx);
    SsReq *r = g_ss.head;
    if (r) {
        g_ss.head = r->next;
        if (!g_ss.head) g_ss.tail = NULL;
        r->next = NULL;
    }
    pthread_mutex_unlock(&g_ss.mtx);
    return r;
}

// --- reading the service (thread side) ---------------------------------------------

static char g_backend[64];
static bool g_ready;

// Which program answers for org.freedesktop.secrets, by the name its process goes by.
static void detect_backend(void) {
    snprintf(g_backend, sizeof g_backend, "Secret Service");
    DBusMessage *m = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                                  "GetConnectionUnixProcessID");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_str(&it, SS_NAME);
    DBusMessage *r = bus_call(m, NULL, 0);
    if (!r) return;
    dbus_uint32_t pid = 0;
    if (dbus_message_iter_init(r, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32) dbus_message_iter_get_basic(&it, &pid);
    dbus_message_unref(r);
    char path[64], comm[64] = "";
    snprintf(path, sizeof path, "/proc/%u/comm", (unsigned)pid);
    FILE *f = fopen(path, "r");
    if (f) {
        if (fgets(comm, sizeof comm, f)) comm[strcspn(comm, "\n")] = 0;
        fclose(f);
    }
    if (!strncmp(comm, "gnome-keyring", 13)) snprintf(g_backend, sizeof g_backend, "GNOME Keyring");
    else if (strstr(comm, "kwallet") || strstr(comm, "ksecretd")) snprintf(g_backend, sizeof g_backend, "KWallet");
    else if (strstr(comm, "keepassxc") || strstr(comm, "KeePassXC")) snprintf(g_backend, sizeof g_backend, "KeePassXC");
    else if (strstr(comm, "oo7")) snprintf(g_backend, sizeof g_backend, "oo7");
    else if (comm[0]) snprintf(g_backend, sizeof g_backend, "%s", comm);
}

static DBusMessage *get_all_msg(const char *path, const char *iface) {
    DBusMessage *m = msg_call(path, DB_PROPS, "GetAll");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_str(&it, iface);
    return m;
}

static void item_prop(void *ctx, const char *name, DBusMessageIter *v) {
    ItemInfo *it = ctx;
    if (!strcmp(name, "Label")) { free(it->label); it->label = it_dup_str(v); }
    else if (!strcmp(name, "Locked")) it->locked = it_bool(v);
    else if (!strcmp(name, "Created")) it->created = it_u64(v);
    else if (!strcmp(name, "Modified")) it->modified = it_u64(v);
    else if (!strcmp(name, "Attributes")) { attrs_free(it->attrs, it->nattrs); it->nattrs = it_attrs(v, &it->attrs); }
}

typedef struct {
    char *path, *label;
    bool locked;
    u64 created, modified;
    char **items;
    int nitems;
} CollProps;

static void coll_prop(void *ctx, const char *name, DBusMessageIter *v) {
    CollProps *c = ctx;
    if (!strcmp(name, "Label")) { free(c->label); c->label = it_dup_str(v); }
    else if (!strcmp(name, "Locked")) c->locked = it_bool(v);
    else if (!strcmp(name, "Created")) c->created = it_u64(v);
    else if (!strcmp(name, "Modified")) c->modified = it_u64(v);
    else if (!strcmp(name, "Items")) { strv_free(c->items, c->nitems); c->nitems = it_objs(v, &c->items); }
}

// The keyring an item belongs to: items live under their collection's object path.
static char *coll_of_item(const char *item) {
    const char *s = strrchr(item, '/');
    return s && s > item ? strndup(item, (size_t)(s - item)) : strdup(item);
}

typedef struct { ItemInfo *items; } ItemsCtx;

static DBusMessage *items_make(void *ctx, int i) { return get_all_msg(((ItemsCtx *)ctx)->items[i].path, SS_ITEM); }

static void items_got(void *ctx, int i, DBusMessage *r) {
    ItemInfo *it = &((ItemsCtx *)ctx)->items[i];
    if (!r) { if (!it->label) it->label = strdup(""); it->locked = true; return; }
    DBusMessageIter ri;
    dbus_message_iter_init(r, &ri);
    it_props(&ri, item_prop, it);
    if (!it->label) it->label = strdup("");
}

// Properties of many items at once (pipelined). paths are taken over.
static void fetch_items(ItemInfo *items, int n) {
    ItemsCtx c = {items};
    bus_pipeline(n, items_make, items_got, &c);
}

typedef struct { CollProps *c; } CollCtx;

static DBusMessage *colls_make(void *ctx, int i) { return get_all_msg(((CollCtx *)ctx)->c[i].path, SS_COLL); }

static void colls_got(void *ctx, int i, DBusMessage *r) {
    if (!r) return;
    DBusMessageIter ri;
    dbus_message_iter_init(r, &ri);
    it_props(&ri, coll_prop, &((CollCtx *)ctx)->c[i]);
}

static char *read_default_alias(void) {
    DBusMessage *m = msg_call(SS_PATH, SS_IFACE, "ReadAlias");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_str(&it, "default");
    DBusMessage *r = bus_call(m, NULL, 0);
    char *p = NULL;
    if (r) {
        dbus_message_iter_init(r, &it);
        p = it_dup_str(&it);
        dbus_message_unref(r);
    }
    return p ? p : strdup("/");
}

// Collections (all, or just `only`) with their items' properties.
static CollInfo *load_colls(const char *only, int *nout, char *err, size_t cap) {
    *nout = 0;
    char **paths = NULL;
    int np = 0;
    if (only) {
        paths = xmalloc(sizeof *paths);
        paths[0] = strdup(only);
        np = 1;
    } else {
        DBusMessage *m = msg_call(SS_PATH, DB_PROPS, "Get");
        DBusMessageIter it;
        dbus_message_iter_init_append(m, &it);
        app_str(&it, SS_IFACE);
        app_str(&it, "Collections");
        DBusMessage *r = bus_call(m, err, cap);
        if (!r) return NULL;
        dbus_message_iter_init(r, &it);
        np = it_objs(&it, &paths);
        dbus_message_unref(r);
    }
    char *def = read_default_alias();
    CollProps *cp = xcalloc((size_t)MAX(np, 1), sizeof *cp);
    for (int i = 0; i < np; i++) cp[i].path = paths[i];
    free(paths);
    CollCtx cc = {cp};
    bus_pipeline(np, colls_make, colls_got, &cc);
    int total = 0;
    for (int i = 0; i < np; i++) total += cp[i].nitems;
    ItemInfo *all = xcalloc((size_t)MAX(total, 1), sizeof *all);
    int k = 0;
    for (int i = 0; i < np; i++)
        for (int j = 0; j < cp[i].nitems; j++) {
            all[k].path = cp[i].items[j];
            all[k].coll = strdup(cp[i].path);
            cp[i].items[j] = NULL;
            k++;
        }
    fetch_items(all, total);
    CollInfo *out = xcalloc((size_t)MAX(np, 1), sizeof *out);
    k = 0;
    for (int i = 0; i < np; i++) {
        CollInfo *c = &out[i];
        c->path = cp[i].path;
        c->label = cp[i].label ? cp[i].label : strdup("");
        c->locked = cp[i].locked;
        c->created = cp[i].created;
        c->modified = cp[i].modified;
        c->is_default = !strcmp(def, c->path);
        c->nitems = cp[i].nitems;
        c->items = xcalloc((size_t)MAX(c->nitems, 1), sizeof *c->items);
        memcpy(c->items, all + k, (size_t)c->nitems * sizeof *c->items);
        k += c->nitems;
        free(cp[i].items);
    }
    free(all);
    free(cp);
    free(def);
    *nout = np;
    return out;
}

static void post_state(int state, const char *msg) {
    SsEvent *ev = ev_new(SE_STATE);
    ev->state = state;
    snprintf(ev->backend, sizeof ev->backend, "%s", g_backend);
    snprintf(ev->msg, sizeof ev->msg, "%s", msg ? msg : "");
    ss_post(ev);
}

static void reload_all(void) {
    char err[256] = "";
    u64 t0 = now_ns();
    int n = 0;
    CollInfo *c = load_colls(NULL, &n, err, sizeof err);
    if (!c) { post_state(SS_FAILED, err); return; }
    SsEvent *ev = ev_new(SE_COLLECTIONS);
    ev->colls = c;
    ev->ncolls = n;
    ev->ms = (f64)(now_ns() - t0) / 1e6;
    ss_post(ev);
}

static void reload_coll(const char *path) {
    int n = 0;
    CollInfo *c = load_colls(path, &n, NULL, 0);
    if (!c) return;
    SsEvent *ev = ev_new(SE_COLLECTION);
    ev->colls = c;
    ev->ncolls = n;
    ss_post(ev);
}

// (Re)connect: a session for secrets, which service it is, and everything it has.
static bool ss_connect(void) {
    char err[256] = "";
    g_ready = false;
    g_backend[0] = 0;
    if (!session_open(err, sizeof err)) {
        bool missing = err_is(err, "ServiceUnknown") || err_is(err, "not provided by any .service") || err_is(err, "was not provided") ||
                       err_is(err, "NameHasNoOwner") || err_is(err, "No such name");
        post_state(missing ? SS_MISSING : SS_FAILED, err);
        return false;
    }
    detect_backend();
    g_ready = true;
    post_state(SS_READY, g_sess.aes ? "encrypted" : "plain");
    reload_all();
    return true;
}

// --- prompts ----------------------------------------------------------------------
// Unlocking, creating or deleting a keyring may need the user's password: the service answers with a prompt
// object, we ask it to show its window, and it signals Completed later. Meanwhile other requests keep running.

typedef struct Prompt {
    char path[256];
    SsReq *then;   // run again when the prompt succeeds (a secret behind a locked keyring), or NULL
    char reload[256]; // keyring to reload afterwards ("" = everything)
    u32 req;
    struct Prompt *next;
} Prompt;

static Prompt *g_prompts;

static void ss_run(SsReq *r);

// prompt: an object path from the service ("/" = none needed). Returns false if showing it failed.
static bool prompt_start(const char *prompt, u32 req, SsReq *then, const char *reload) {
    char rule[512];
    snprintf(rule, sizeof rule, "type='signal',interface='" SS_PROMPT "',member='Completed',path='%s'", prompt);
    bus_match(rule);
    Prompt *p = xcalloc(1, sizeof *p);
    snprintf(p->path, sizeof p->path, "%s", prompt);
    snprintf(p->reload, sizeof p->reload, "%s", reload ? reload : "");
    p->then = then;
    p->req = req;
    p->next = g_prompts;
    g_prompts = p;
    DBusMessage *m = msg_call(prompt, SS_PROMPT, "Prompt");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_str(&it, ""); // window id: none (Wayland has no portable one; the service centres its window)
    char err[256] = "";
    DBusMessage *r = bus_call(m, err, sizeof err);
    if (!r) {
        g_prompts = p->next;
        req_free(p->then);
        free(p);
        post_done(req, false, "The keyring couldn't ask for the password: %s", err);
        return false;
    }
    dbus_message_unref(r);
    SsEvent *ev = ev_new(SE_PROMPT);
    ev->state = 1;
    ss_post(ev);
    return true;
}

static void prompt_completed(DBusMessage *sig) {
    const char *path = dbus_message_get_path(sig);
    Prompt **pp = &g_prompts, *p = NULL;
    for (; *pp; pp = &(*pp)->next)
        if (path && !strcmp((*pp)->path, path)) { p = *pp; *pp = p->next; break; }
    if (!p) return;
    char rule[512];
    snprintf(rule, sizeof rule, "type='signal',interface='" SS_PROMPT "',member='Completed',path='%s'", p->path);
    dbus_bus_remove_match(g_bus, rule, NULL);
    DBusMessageIter it;
    bool dismissed = true;
    if (dbus_message_iter_init(sig, &it)) dismissed = it_bool(&it);
    if (!g_prompts) {
        SsEvent *ev = ev_new(SE_PROMPT);
        ev->state = 0;
        ss_post(ev);
    }
    if (p->reload[0]) reload_coll(p->reload);
    else reload_all();
    if (dismissed) { post_done(p->req, false, "Cancelled"); req_free(p->then); }
    else if (p->then) ss_run(p->then);
    else post_done(p->req, true, "");
    free(p);
}

// The prompt path out of a reply whose last argument is it ("/" when none is needed).
static char *reply_prompt(DBusMessage *r) {
    DBusMessageIter it;
    char *last = NULL;
    if (!dbus_message_iter_init(r, &it)) return strdup("/");
    do {
        if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_OBJECT_PATH) { free(last); last = it_dup_str(&it); }
    } while (dbus_message_iter_next(&it));
    return last ? last : strdup("/");
}

// --- requests (thread side) --------------------------------------------------------------

static void post_items(ItemInfo *items, int n) {
    SsEvent *ev = ev_new(SE_ITEMS);
    ev->items = items;
    ev->nitems = n;
    ss_post(ev);
}

static void refresh_items(char **paths, int n) {
    if (!n) return;
    ItemInfo *it = xcalloc((size_t)n, sizeof *it);
    for (int i = 0; i < n; i++) { it[i].path = strdup(paths[i]); it[i].coll = coll_of_item(paths[i]); }
    fetch_items(it, n);
    post_items(it, n);
}

static void refresh_item(const char *path) { char *p = (char *)path; refresh_items(&p, 1); }

static bool get_secret(const char *path, SecBuf *out, char **ctype, char *err, size_t cap) {
    DBusMessage *m = msg_call(path, SS_ITEM, "GetSecret");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_obj(&it, g_sess.path);
    DBusMessage *r = bus_call(m, err, cap);
    if (!r) return false;
    dbus_message_iter_init(r, &it);
    bool ok = it_secret(&it, out, ctype);
    if (!ok) snprintf(err, cap, "The secret couldn't be decrypted");
    dbus_message_unref(r);
    return ok;
}

static bool set_prop(const char *path, const char *iface, const char *name, const char *s, const Attr *attrs, int nattrs, char *err, size_t cap) {
    DBusMessage *m = msg_call(path, DB_PROPS, "Set");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_str(&it, iface);
    app_str(&it, name);
    if (s) app_var_str(&it, s);
    else app_var_attrs(&it, attrs, nattrs);
    DBusMessage *r = bus_call(m, err, cap);
    if (r) dbus_message_unref(r);
    return r != NULL;
}

// Unlock what the request needs, then run it again (it owns r).
static bool unlock_then(SsReq *r, char **paths, int n) {
    DBusMessage *m = msg_call(SS_PATH, SS_IFACE, "Unlock");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_objs(&it, paths, n);
    char err[256] = "";
    DBusMessage *rep = bus_call(m, err, sizeof err);
    if (!rep) { post_done(r->id, false, "Couldn't unlock: %s", err); req_free(r); return false; }
    char *pr = reply_prompt(rep);
    dbus_message_unref(rep);
    bool ok = true;
    if (strcmp(pr, "/")) ok = prompt_start(pr, r->id, r, NULL);
    else ss_run(r);
    free(pr);
    return ok;
}

static void do_secret(SsReq *r) {
    SsEvent *ev = ev_new(SE_SECRET);
    char err[256] = "";
    ev->req = r->id;
    ev->purpose = r->purpose;
    ev->path = strdup(r->path);
    ev->ok = get_secret(r->path, &ev->secret, &ev->ctype, err, sizeof err);
    if (!ev->ok && (err_is(err, "IsLocked") || err_is(err, "locked")) && r->purpose >= 0) {
        ss_event_free(ev);
        r->purpose = -1 - r->purpose; // (only once: a second failure reports)
        char *p = r->path;
        unlock_then(r, &p, 1);
        return;
    }
    if (r->purpose < 0) ev->purpose = -1 - r->purpose;
    snprintf(ev->msg, sizeof ev->msg, "%s", err);
    ss_post(ev);
    req_free(r);
}

static void do_lock(SsReq *r, bool lock) {
    if (!lock) { // unlock: one call for all, maybe one prompt
        DBusMessage *m = msg_call(SS_PATH, SS_IFACE, "Unlock");
        DBusMessageIter it;
        dbus_message_iter_init_append(m, &it);
        app_objs(&it, r->paths, r->npaths);
        char err[256] = "";
        DBusMessage *rep = bus_call(m, err, sizeof err);
        if (!rep) { post_done(r->id, false, "Couldn't unlock: %s", err); return; }
        char *pr = reply_prompt(rep);
        dbus_message_unref(rep);
        if (strcmp(pr, "/")) prompt_start(pr, r->id, NULL, r->npaths == 1 ? r->paths[0] : NULL);
        else {
            for (int i = 0; i < r->npaths; i++) reload_coll(r->paths[i]);
            post_done(r->id, true, "");
        }
        free(pr);
        return;
    }
    DBusMessage *m = msg_call(SS_PATH, SS_IFACE, "Lock");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_objs(&it, r->paths, r->npaths);
    char err[256] = "";
    DBusMessage *rep = bus_call(m, err, sizeof err);
    if (!rep) { post_done(r->id, false, "Couldn't lock: %s", err); return; }
    char *pr = reply_prompt(rep);
    dbus_message_unref(rep);
    if (strcmp(pr, "/")) prompt_start(pr, r->id, NULL, NULL);
    else {
        for (int i = 0; i < r->npaths; i++) reload_coll(r->paths[i]);
        post_done(r->id, true, "");
    }
    free(pr);
}

static void do_set_item(SsReq *r) {
    char err[256] = "";
    bool ok = true;
    if (ok && r->set_label) ok = set_prop(r->path, SS_ITEM, "Label", r->label ? r->label : "", NULL, 0, err, sizeof err);
    if (ok && r->set_attrs) ok = set_prop(r->path, SS_ITEM, "Attributes", NULL, r->attrs, r->nattrs, err, sizeof err);
    if (ok && r->set_secret) {
        DBusMessage *m = msg_call(r->path, SS_ITEM, "SetSecret");
        DBusMessageIter it;
        dbus_message_iter_init_append(m, &it);
        if (!app_secret(&it, &r->secret, r->ctype)) { dbus_message_unref(m); ok = false; snprintf(err, sizeof err, "encryption failed"); }
        else {
            DBusMessage *rep = bus_call(m, err, sizeof err);
            ok = rep != NULL;
            if (rep) dbus_message_unref(rep);
        }
    }
    refresh_item(r->path);
    post_done(r->id, ok, ok ? "" : "Couldn't save: %s", err);
}

// CreateItem in keyring `coll`; the new item's path in *out (NULL if a prompt took over or it failed).
static bool create_item(const char *coll, const char *label, const Attr *attrs, int nattrs, const SecBuf *sec, const char *ctype,
                        u32 req, char **out, char *err, size_t cap) {
    *out = NULL;
    DBusMessage *m = msg_call(coll, SS_COLL, "CreateItem");
    DBusMessageIter it, d;
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &d);
    app_prop(&d, SS_ITEM ".Label", label ? label : "", NULL, 0);
    app_prop(&d, SS_ITEM ".Attributes", NULL, attrs, nattrs);
    dbus_message_iter_close_container(&it, &d);
    if (!app_secret(&it, sec, ctype)) { dbus_message_unref(m); snprintf(err, cap, "encryption failed"); return false; }
    app_bool(&it, false); // never silently replace an item with the same attributes
    DBusMessage *r = bus_call(m, err, cap);
    if (!r) return false;
    DBusMessageIter ri;
    dbus_message_iter_init(r, &ri);
    char *item = it_dup_str(&ri);
    char *pr = reply_prompt(r);
    dbus_message_unref(r);
    if (strcmp(item, "/")) *out = item;
    else free(item);
    if (strcmp(pr, "/") && !*out) prompt_start(pr, req, NULL, coll);
    free(pr);
    return true;
}

static void do_create_item(SsReq *r) {
    char err[256] = "", *path = NULL;
    bool ok = create_item(r->target, r->label, r->attrs, r->nattrs, &r->secret, r->ctype, r->id, &path, err, sizeof err);
    if (ok && path) {
        refresh_item(path);
        SsEvent *ev = ev_new(SE_DONE); // (path: the UI selects the new item)
        ev->req = r->id;
        ev->ok = true;
        ev->path = path;
        ss_post(ev);
        return;
    }
    if (!ok) post_done(r->id, false, "Couldn't create it: %s", err);
}

// Delete one item; true when it's gone (or a prompt is handling it).
static bool delete_item(const char *path, u32 req, char *err, size_t cap) {
    DBusMessage *m = msg_call(path, SS_ITEM, "Delete");
    DBusMessage *r = bus_call(m, err, cap);
    if (!r) return false;
    char *pr = reply_prompt(r);
    dbus_message_unref(r);
    if (strcmp(pr, "/")) {
        char *coll = coll_of_item(path);
        prompt_start(pr, req, NULL, coll);
        free(coll);
    } else {
        SsEvent *ev = ev_new(SE_ITEM_GONE);
        ev->path = strdup(path);
        ss_post(ev);
    }
    free(pr);
    return true;
}

static void do_delete(SsReq *r) {
    char err[256] = "";
    int failed = 0;
    for (int i = 0; i < r->npaths; i++)
        if (!delete_item(r->paths[i], r->id, err, sizeof err)) failed++;
    if (failed) post_done(r->id, false, "%d item%s couldn't be deleted: %s", failed, failed == 1 ? "" : "s", err);
    else post_done(r->id, true, "");
}

// Copy (or move) items into another keyring: their secret, label and attributes go into new items there.
static void do_transfer(SsReq *r) {
    char err[256] = "";
    int failed = 0;
    ItemInfo *src = xcalloc((size_t)MAX(r->npaths, 1), sizeof *src);
    for (int i = 0; i < r->npaths; i++) { src[i].path = strdup(r->paths[i]); src[i].coll = coll_of_item(r->paths[i]); }
    fetch_items(src, r->npaths);
    for (int i = 0; i < r->npaths; i++) {
        SecBuf s = {0};
        char *ctype = NULL, *path = NULL;
        bool ok = get_secret(src[i].path, &s, &ctype, err, sizeof err) &&
                  create_item(r->target, src[i].label, src[i].attrs, src[i].nattrs, &s, ctype, r->id, &path, err, sizeof err) && path;
        if (ok && r->move) ok = delete_item(src[i].path, r->id, err, sizeof err);
        if (!ok) failed++;
        if (path) refresh_item(path);
        free(path);
        free(ctype);
        sec_free(&s);
    }
    for (int i = 0; i < r->npaths; i++) item_info_free(&src[i]);
    free(src);
    if (failed) post_done(r->id, false, "%d of %d couldn't be %s: %s", failed, r->npaths, r->move ? "moved" : "copied", err);
    else post_done(r->id, true, "");
}

static void do_create_coll(SsReq *r) {
    DBusMessage *m = msg_call(SS_PATH, SS_IFACE, "CreateCollection");
    DBusMessageIter it, d;
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &d);
    app_prop(&d, SS_COLL ".Label", r->label ? r->label : "", NULL, 0);
    dbus_message_iter_close_container(&it, &d);
    app_str(&it, "");
    char err[256] = "";
    DBusMessage *rep = bus_call(m, err, sizeof err);
    if (!rep) { post_done(r->id, false, "Couldn't create the keyring: %s", err); return; }
    DBusMessageIter ri;
    dbus_message_iter_init(rep, &ri);
    char *coll = it_dup_str(&ri), *pr = reply_prompt(rep);
    dbus_message_unref(rep);
    if (strcmp(coll, "/")) {
        reload_all();
        SsEvent *ev = ev_new(SE_DONE);
        ev->req = r->id;
        ev->ok = true;
        ev->path = coll;
        coll = NULL;
        ss_post(ev);
    } else if (strcmp(pr, "/")) prompt_start(pr, r->id, NULL, NULL);
    free(coll);
    free(pr);
}

static void do_delete_coll(SsReq *r) {
    DBusMessage *m = msg_call(r->path, SS_COLL, "Delete");
    char err[256] = "";
    DBusMessage *rep = bus_call(m, err, sizeof err);
    if (!rep) { post_done(r->id, false, "Couldn't delete the keyring: %s", err); return; }
    char *pr = reply_prompt(rep);
    dbus_message_unref(rep);
    if (strcmp(pr, "/")) prompt_start(pr, r->id, NULL, NULL);
    else {
        SsEvent *ev = ev_new(SE_COLL_GONE);
        ev->path = strdup(r->path);
        ss_post(ev);
        post_done(r->id, true, "");
    }
    free(pr);
}

static void do_set_default(SsReq *r) {
    DBusMessage *m = msg_call(SS_PATH, SS_IFACE, "SetAlias");
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_str(&it, "default");
    app_obj(&it, r->path);
    char err[256] = "";
    DBusMessage *rep = bus_call(m, err, sizeof err);
    if (rep) dbus_message_unref(rep);
    reload_all();
    post_done(r->id, rep != NULL, rep ? "" : "Couldn't make it the default: %s", err);
}

// gnome-keyring's private interface, for self-tests only: keyrings with a known password, no prompt windows.
#define GK_IFACE "org.gnome.keyring.InternalUnsupportedGuiltRiddenInterface"

static void do_test(SsReq *r) {
    if (!g_ss.test_bus) { post_done(r->id, false, "test requests need the test bus"); return; }
    char err[256] = "";
    DBusMessage *m;
    DBusMessageIter it, d;
    if (r->op == RQ_TEST_CREATE) {
        m = msg_call(SS_PATH, GK_IFACE, "CreateWithMasterPassword");
        dbus_message_iter_init_append(m, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &d);
        app_prop(&d, SS_COLL ".Label", r->label ? r->label : "", NULL, 0);
        dbus_message_iter_close_container(&it, &d);
    } else {
        m = msg_call(SS_PATH, GK_IFACE, "UnlockWithMasterPassword");
        dbus_message_iter_init_append(m, &it);
        app_obj(&it, r->path);
    }
    app_secret(&it, &r->secret, "text/plain");
    DBusMessage *rep = bus_call(m, err, sizeof err);
    char *path = NULL;
    if (rep && r->op == RQ_TEST_CREATE) {
        dbus_message_iter_init(rep, &it);
        path = it_dup_str(&it);
    }
    if (rep) dbus_message_unref(rep);
    reload_all();
    SsEvent *ev = ev_new(SE_DONE);
    ev->req = r->id;
    ev->ok = rep != NULL;
    ev->path = path;
    snprintf(ev->msg, sizeof ev->msg, "%s", err);
    ss_post(ev);
}

// Run one request (owns it).
static void ss_run(SsReq *r) {
    if (!g_ready && r->op != RQ_RELOAD) { post_done(r->id, false, "No keyring service"); req_free(r); return; }
    char err[256] = "";
    switch (r->op) {
    case RQ_RELOAD: if (!g_ready) ss_connect(); else reload_all(); post_done(r->id, true, ""); break;
    case RQ_SECRET: do_secret(r); return; // (may keep r for after an unlock)
    case RQ_UNLOCK: do_lock(r, false); break;
    case RQ_LOCK: do_lock(r, true); break;
    case RQ_SET_ITEM: do_set_item(r); break;
    case RQ_CREATE_ITEM: do_create_item(r); break;
    case RQ_DELETE: do_delete(r); break;
    case RQ_TRANSFER: do_transfer(r); break;
    case RQ_CREATE_COLL: do_create_coll(r); break;
    case RQ_DELETE_COLL: do_delete_coll(r); break;
    case RQ_RENAME_COLL: {
        bool ok = set_prop(r->path, SS_COLL, "Label", r->label ? r->label : "", NULL, 0, err, sizeof err);
        reload_coll(r->path);
        post_done(r->id, ok, ok ? "" : "Couldn't rename the keyring: %s", err);
        break;
    }
    case RQ_SET_DEFAULT: do_set_default(r); break;
    case RQ_DISMISS:
        for (Prompt *p = g_prompts; p; p = p->next) {
            DBusMessage *rep = bus_call(msg_call(p->path, SS_PROMPT, "Dismiss"), NULL, 0);
            if (rep) dbus_message_unref(rep);
        }
        break;
    case RQ_TEST_CREATE: case RQ_TEST_UNLOCK: do_test(r); break;
    }
    req_free(r);
}

// --- signals ------------------------------------------------------------------
// Changes made by other programs (or by us) arrive as signals; item changes are collected over one round of the
// loop and fetched together.

typedef struct { char **v; int n, cap; } StrSet;

static void strset_add(StrSet *s, const char *p) {
    for (int i = 0; i < s->n; i++)
        if (!strcmp(s->v[i], p)) return;
    if (s->n == s->cap) s->v = xrealloc(s->v, (size_t)(s->cap = s->cap ? s->cap * 2 : 16) * sizeof *s->v);
    s->v[s->n++] = strdup(p);
}

static void strset_clear(StrSet *s) {
    strv_free(s->v, s->n);
    memset(s, 0, sizeof *s);
}

static StrSet g_dirty_items, g_dirty_colls;
static bool g_dirty_all;

static void on_signal(DBusMessage *m) {
    DBusMessageIter it;
    char *arg = NULL;
    if (dbus_message_iter_init(m, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_OBJECT_PATH) arg = it_dup_str(&it);
    if (dbus_message_is_signal(m, SS_COLL, "ItemCreated") || dbus_message_is_signal(m, SS_COLL, "ItemChanged")) {
        if (arg) strset_add(&g_dirty_items, arg);
    } else if (dbus_message_is_signal(m, SS_COLL, "ItemDeleted")) {
        if (arg) {
            SsEvent *ev = ev_new(SE_ITEM_GONE);
            ev->path = arg;
            arg = NULL;
            ss_post(ev);
        }
    } else if (dbus_message_is_signal(m, SS_IFACE, "CollectionChanged")) {
        if (arg) strset_add(&g_dirty_colls, arg);
    } else if (dbus_message_is_signal(m, SS_IFACE, "CollectionCreated") || dbus_message_is_signal(m, SS_IFACE, "CollectionDeleted")) {
        g_dirty_all = true;
    } else if (dbus_message_is_signal(m, DB_PROPS, "PropertiesChanged")) { // (some services only send these)
        const char *p = dbus_message_get_path(m);
        if (p && dbus_message_has_interface(m, DB_PROPS)) {
            const char *iface = NULL;
            if (dbus_message_iter_init(m, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) dbus_message_iter_get_basic(&it, &iface);
            if (iface && !strcmp(iface, SS_ITEM)) strset_add(&g_dirty_items, p);
            else if (iface && !strcmp(iface, SS_COLL)) strset_add(&g_dirty_colls, p);
        }
    } else if (dbus_message_is_signal(m, SS_PROMPT, "Completed")) {
        prompt_completed(m);
    } else if (dbus_message_is_signal(m, "org.freedesktop.DBus", "NameOwnerChanged")) {
        const char *name = NULL, *old = NULL, *nw = NULL;
        if (dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old, DBUS_TYPE_STRING, &nw, DBUS_TYPE_INVALID) &&
            name && !strcmp(name, SS_NAME)) {
            if (nw && nw[0]) ss_connect(); // the service (re)started: new session, everything again
            else { g_ready = false; post_state(SS_MISSING, "The keyring service stopped"); }
        }
    }
    free(arg);
}

static void flush_dirty(void) {
    if (g_dirty_all) {
        g_dirty_all = false;
        strset_clear(&g_dirty_colls);
        strset_clear(&g_dirty_items);
        reload_all();
        return;
    }
    for (int i = 0; i < g_dirty_colls.n; i++) reload_coll(g_dirty_colls.v[i]);
    if (g_dirty_items.n) refresh_items(g_dirty_items.v, g_dirty_items.n);
    strset_clear(&g_dirty_colls);
    strset_clear(&g_dirty_items);
}

// --- the thread -------------------------------------------------------------------

static void *ss_thread(void *arg) {
    (void)arg;
    char err[256] = "";
    if (!bus_open(err, sizeof err)) { post_state(SS_MISSING, err); return NULL; }
    bus_match("type='signal',interface='" SS_IFACE "'");
    bus_match("type='signal',interface='" SS_COLL "'");
    bus_match("type='signal',interface='" DB_PROPS "',member='PropertiesChanged',path_namespace='" SS_PATH "'");
    bus_match("type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0='" SS_NAME "'");
    ss_connect();
    int fd = -1;
    dbus_connection_get_unix_fd(g_bus, &fd);
    for (;;) {
        // what already arrived (signals queue up while calls wait for their replies)
        dbus_connection_read_write(g_bus, 0);
        DBusMessage *m;
        while ((m = dbus_connection_pop_message(g_bus))) {
            if (dbus_message_get_type(m) == DBUS_MESSAGE_TYPE_SIGNAL) on_signal(m);
            dbus_message_unref(m);
        }
        flush_dirty();
        SsReq *r;
        bool did = false;
        while ((r = ss_take())) { ss_run(r); did = true; }
        if (did) continue; // (requests may have queued signals)
        if (!dbus_connection_get_is_connected(g_bus)) { post_state(SS_FAILED, "Lost the session bus"); return NULL; }
        if (dbus_connection_get_dispatch_status(g_bus) == DBUS_DISPATCH_DATA_REMAINS) continue;
        struct pollfd pf[2] = {{fd, POLLIN, 0}, {g_ss.wake_fd, POLLIN, 0}};
        if (poll(pf, 2, -1) < 0 && errno != EINTR) break;
        if (pf[1].revents & POLLIN) {
            u64 v;
            if (read(g_ss.wake_fd, &v, sizeof v) < 0) {}
        }
    }
    return NULL;
}

static void ss_start(void) {
    dbus_threads_init_default();
    g_ss.wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    pthread_t t;
    pthread_create(&t, NULL, ss_thread, NULL);
    pthread_setname_np(t, "km-secret");
    pthread_detach(t);
}

// --- requests (main-thread helpers) ------------------------------------------------------

static char **strv_dup1(const char *s) {
    char **v = xmalloc(sizeof *v);
    v[0] = strdup(s);
    return v;
}

static u32 ss_get_secret(const char *item, int purpose) {
    SsReq *r = ss_req(RQ_SECRET);
    r->path = strdup(item);
    r->purpose = purpose;
    return ss_submit(r);
}

static u32 ss_lock(char **paths, int n, bool lock) {
    SsReq *r = ss_req(lock ? RQ_LOCK : RQ_UNLOCK);
    r->paths = xmalloc((size_t)MAX(n, 1) * sizeof *r->paths);
    for (int i = 0; i < n; i++) r->paths[i] = strdup(paths[i]);
    r->npaths = n;
    return ss_submit(r);
}

static u32 ss_simple(int op, const char *path, const char *label) {
    SsReq *r = ss_req(op);
    r->path = path ? strdup(path) : NULL;
    r->label = label ? strdup(label) : NULL;
    return ss_submit(r);
}
