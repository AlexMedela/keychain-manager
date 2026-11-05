// dbus.c - the Secret Service thread's D-Bus connection: blocking and pipelined calls, message helpers, and the
// encrypted transfer session (dh-ietf1024-sha256-aes128-cbc-pkcs7, the same one libsecret uses)

#define SS_NAME "org.freedesktop.secrets"
#define SS_PATH "/org/freedesktop/secrets"
#define SS_IFACE "org.freedesktop.Secret.Service"
#define SS_COLL "org.freedesktop.Secret.Collection"
#define SS_ITEM "org.freedesktop.Secret.Item"
#define SS_PROMPT "org.freedesktop.Secret.Prompt"
#define DB_PROPS "org.freedesktop.DBus.Properties"
#define CALL_TIMEOUT 25000 // ms (prompts don't block calls: they answer with a signal later)

static DBusConnection *g_bus;

// --- connection ------------------------------------------------------------

static bool bus_open(char *err, size_t cap) {
    DBusError e;
    dbus_error_init(&e);
    g_bus = dbus_bus_get_private(DBUS_BUS_SESSION, &e);
    if (!g_bus) {
        snprintf(err, cap, "%s", e.message ? e.message : "no session bus");
        dbus_error_free(&e);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(g_bus, FALSE);
    return true;
}

static void bus_match(const char *rule) {
    dbus_bus_add_match(g_bus, rule, NULL); // (no error wanted: the reply would block)
}

static DBusMessage *msg_call(const char *path, const char *iface, const char *method) {
    return dbus_message_new_method_call(SS_NAME, path, iface, method);
}

// Send and wait for the reply (the message is consumed). NULL on error, with its text in err.
static DBusMessage *bus_call(DBusMessage *m, char *err, size_t cap) {
    DBusError e;
    dbus_error_init(&e);
    DBusMessage *r = dbus_connection_send_with_reply_and_block(g_bus, m, CALL_TIMEOUT, &e);
    dbus_message_unref(m);
    if (!r) {
        if (err) snprintf(err, cap, "%s", e.message ? e.message : e.name ? e.name : "D-Bus call failed");
        dbus_error_free(&e);
    }
    return r;
}

// The D-Bus error name of a failed call's text is lost by then; callers that need it compare against these.
static bool err_is(const char *err, const char *what) { return err && strstr(err, what) != NULL; }

// --- pipelined calls: send many, then collect the replies ----------------------
// One round-trip per call would make listing thousands of items take seconds; the service answers queued calls
// back to back, so sending them all first costs about one round-trip in total. A window keeps the number in
// flight bounded (the bus limits pending replies per connection).

#define PIPE_WINDOW 512

typedef void (*PipeReplyFn)(void *ctx, int i, DBusMessage *reply); // reply NULL on error
typedef DBusMessage *(*PipeMakeFn)(void *ctx, int i);

static void bus_pipeline(int n, PipeMakeFn make, PipeReplyFn got, void *ctx) {
    DBusPendingCall **pend = xcalloc((size_t)MIN(n, PIPE_WINDOW) + 1, sizeof *pend);
    int sent = 0, done = 0;
    while (done < n) {
        while (sent < n && sent - done < PIPE_WINDOW) {
            DBusMessage *m = make(ctx, sent);
            DBusPendingCall *p = NULL;
            if (!m || !dbus_connection_send_with_reply(g_bus, m, &p, CALL_TIMEOUT)) p = NULL;
            if (m) dbus_message_unref(m);
            pend[sent % PIPE_WINDOW] = p;
            sent++;
        }
        dbus_connection_flush(g_bus);
        DBusPendingCall *p = pend[done % PIPE_WINDOW];
        DBusMessage *r = NULL;
        if (p) {
            dbus_pending_call_block(p);
            r = dbus_pending_call_steal_reply(p);
            dbus_pending_call_unref(p);
        }
        if (r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR) { dbus_message_unref(r); r = NULL; }
        got(ctx, done, r);
        if (r) dbus_message_unref(r);
        done++;
    }
    free(pend);
}

// --- appending -------------------------------------------------------------

// D-Bus strings must be valid UTF-8 (libdbus aborts otherwise): anything else is sent as "?".
static const char *utf8_or(const char *s) { return s && dbus_validate_utf8(s, NULL) ? s : "?"; }

static void app_str(DBusMessageIter *it, const char *s) {
    s = utf8_or(s);
    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &s);
}

static void app_obj(DBusMessageIter *it, const char *p) {
    if (!p || !dbus_validate_path(p, NULL)) p = "/";
    dbus_message_iter_append_basic(it, DBUS_TYPE_OBJECT_PATH, &p);
}

static void app_bool(DBusMessageIter *it, bool v) {
    dbus_bool_t b = v;
    dbus_message_iter_append_basic(it, DBUS_TYPE_BOOLEAN, &b);
}

static void app_bytes(DBusMessageIter *it, const u8 *p, size_t n) {
    DBusMessageIter a;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "y", &a);
    if (n) dbus_message_iter_append_fixed_array(&a, DBUS_TYPE_BYTE, &p, (int)n);
    dbus_message_iter_close_container(it, &a);
}

static void app_objs(DBusMessageIter *it, char **paths, int n) {
    DBusMessageIter a;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "o", &a);
    for (int i = 0; i < n; i++) app_obj(&a, paths[i]);
    dbus_message_iter_close_container(it, &a);
}

typedef struct { char *k, *v; } Attr;

static void app_attrs(DBusMessageIter *it, const Attr *at, int n) {
    DBusMessageIter a, e;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{ss}", &a);
    for (int i = 0; i < n; i++) {
        dbus_message_iter_open_container(&a, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        app_str(&e, at[i].k);
        app_str(&e, at[i].v);
        dbus_message_iter_close_container(&a, &e);
    }
    dbus_message_iter_close_container(it, &a);
}

static void app_var_str(DBusMessageIter *it, const char *s) {
    DBusMessageIter v;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "s", &v);
    app_str(&v, s);
    dbus_message_iter_close_container(it, &v);
}

static void app_var_attrs(DBusMessageIter *it, const Attr *at, int n) {
    DBusMessageIter v;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "a{ss}", &v);
    app_attrs(&v, at, n);
    dbus_message_iter_close_container(it, &v);
}

// One entry of an a{sv} dictionary whose value is a string or (attrs != NULL) an a{ss}.
static void app_prop(DBusMessageIter *dict, const char *key, const char *s, const Attr *attrs, int nattrs) {
    DBusMessageIter e;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    app_str(&e, key);
    if (attrs || !s) app_var_attrs(&e, attrs, nattrs);
    else app_var_str(&e, s);
    dbus_message_iter_close_container(dict, &e);
}

// --- reading ---------------------------------------------------------------

static char *it_dup_str(DBusMessageIter *it) {
    int t = dbus_message_iter_get_arg_type(it);
    if (t != DBUS_TYPE_STRING && t != DBUS_TYPE_OBJECT_PATH) return strdup("");
    const char *s = NULL;
    dbus_message_iter_get_basic(it, &s);
    return strdup(s ? s : "");
}

// An "ao" (or a variant holding one) -> malloc'd array of strdup'd paths.
static int it_objs(DBusMessageIter *it, char ***out) {
    DBusMessageIter v, a;
    if (dbus_message_iter_get_arg_type(it) == DBUS_TYPE_VARIANT) { dbus_message_iter_recurse(it, &v); it = &v; }
    *out = NULL;
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_ARRAY) return 0;
    int n = 0, cap = 0;
    char **p = NULL;
    dbus_message_iter_recurse(it, &a);
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_OBJECT_PATH) {
        if (n == cap) p = xrealloc(p, (size_t)(cap = cap ? cap * 2 : 16) * sizeof *p);
        p[n++] = it_dup_str(&a);
        dbus_message_iter_next(&a);
    }
    *out = p;
    return n;
}

static void strv_free(char **v, int n) {
    for (int i = 0; i < n; i++) free(v[i]);
    free(v);
}

static int it_attrs(DBusMessageIter *it, Attr **out) {
    DBusMessageIter a, e;
    *out = NULL;
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_ARRAY) return 0;
    int n = 0, cap = 0;
    Attr *at = NULL;
    dbus_message_iter_recurse(it, &a);
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
        dbus_message_iter_recurse(&a, &e);
        if (n == cap) at = xrealloc(at, (size_t)(cap = cap ? cap * 2 : 8) * sizeof *at);
        at[n].k = it_dup_str(&e);
        dbus_message_iter_next(&e);
        at[n].v = it_dup_str(&e);
        n++;
        dbus_message_iter_next(&a);
    }
    *out = at;
    return n;
}

static void attrs_free(Attr *at, int n) {
    for (int i = 0; i < n; i++) { free(at[i].k); free(at[i].v); }
    free(at);
}

static Attr *attrs_dup(const Attr *at, int n) {
    if (!n) return NULL;
    Attr *o = xmalloc((size_t)n * sizeof *o);
    for (int i = 0; i < n; i++) { o[i].k = strdup(at[i].k); o[i].v = strdup(at[i].v); }
    return o;
}

// Walk an a{sv} of properties (the reply of Properties.GetAll): fn gets each name and the variant's contents.
typedef void (*PropFn)(void *ctx, const char *name, DBusMessageIter *val);

static void it_props(DBusMessageIter *it, PropFn fn, void *ctx) {
    DBusMessageIter a, e, v;
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_ARRAY) return;
    dbus_message_iter_recurse(it, &a);
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
        dbus_message_iter_recurse(&a, &e);
        const char *name = NULL;
        if (dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&e, &name);
            dbus_message_iter_next(&e);
            if (dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_VARIANT) {
                dbus_message_iter_recurse(&e, &v);
                fn(ctx, name, &v);
            }
        }
        dbus_message_iter_next(&a);
    }
}

static bool it_bool(DBusMessageIter *it) {
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_BOOLEAN) return false;
    dbus_bool_t b = 0;
    dbus_message_iter_get_basic(it, &b);
    return b;
}

static u64 it_u64(DBusMessageIter *it) {
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_UINT64) return 0;
    dbus_uint64_t v = 0;
    dbus_message_iter_get_basic(it, &v);
    return v;
}

static bool it_bytes(DBusMessageIter *it, const u8 **p, int *n) {
    DBusMessageIter a;
    *p = NULL;
    *n = 0;
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_ARRAY) return false;
    dbus_message_iter_recurse(it, &a);
    if (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_BYTE) dbus_message_iter_get_fixed_array(&a, p, n);
    return true;
}

// --- transfer session --------------------------------------------------------
// Secrets travel AES-128-CBC encrypted with a key agreed by Diffie-Hellman (RFC 2409's 1024-bit group, generator 2)
// and stretched by HKDF-SHA256 (no salt, no info), as in the Secret Service spec. "plain" only if the service
// refuses that (it's the local bus, but other processes of the user could still watch it).

typedef struct {
    char path[256];
    bool aes;
    u8 key[16];
} Session;

static Session g_sess;

static void hkdf_sha256_16(const u8 *ikm, size_t n, u8 out[16]) {
    u8 salt[32] = {0}, prk[32], t[32];
    unsigned int l = 0;
    HMAC(EVP_sha256(), salt, sizeof salt, ikm, n, prk, &l);
    u8 one = 1;
    HMAC(EVP_sha256(), prk, sizeof prk, &one, 1, t, &l); // T(1) = HMAC(PRK, info || 0x01), info empty
    memcpy(out, t, 16);
    explicit_bzero(prk, sizeof prk);
    explicit_bzero(t, sizeof t);
}

static bool session_open(char *err, size_t cap) {
    memset(&g_sess, 0, sizeof g_sess);
    BIGNUM *p = BN_get_rfc2409_prime_1024(NULL), *g = BN_new(), *priv = BN_new(), *pub = BN_new();
    BN_CTX *bc = BN_CTX_new();
    u8 rnd[128], pubb[128];
    bool ok = false;
    if (getrandom(rnd, sizeof rnd, 0) == (ssize_t)sizeof rnd && p && g && priv && pub && bc) {
        BN_set_word(g, 2);
        BN_bin2bn(rnd, sizeof rnd, priv);
        BN_mod(priv, priv, p, bc);
        BN_mod_exp(pub, g, priv, p, bc);
        int pn = BN_bn2binpad(pub, pubb, sizeof pubb);
        DBusMessage *m = msg_call(SS_PATH, SS_IFACE, "OpenSession");
        DBusMessageIter it, v;
        dbus_message_iter_init_append(m, &it);
        app_str(&it, "dh-ietf1024-sha256-aes128-cbc-pkcs7");
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "ay", &v);
        app_bytes(&v, pubb, (size_t)pn);
        dbus_message_iter_close_container(&it, &v);
        DBusMessage *r = bus_call(m, err, cap);
        if (r) {
            DBusMessageIter ri, rv;
            dbus_message_iter_init(r, &ri);
            const u8 *sp = NULL;
            int sn = 0;
            if (dbus_message_iter_get_arg_type(&ri) == DBUS_TYPE_VARIANT) {
                dbus_message_iter_recurse(&ri, &rv);
                it_bytes(&rv, &sp, &sn);
            }
            dbus_message_iter_next(&ri);
            char *path = it_dup_str(&ri);
            if (sp && sn > 0 && sn <= 128 && path[0]) {
                BIGNUM *spub = BN_bin2bn(sp, sn, NULL), *shared = BN_new();
                u8 sb[128];
                if (spub && shared && BN_mod_exp(shared, spub, priv, p, bc) && BN_bn2binpad(shared, sb, sizeof sb) == sizeof sb) {
                    hkdf_sha256_16(sb, sizeof sb, g_sess.key);
                    snprintf(g_sess.path, sizeof g_sess.path, "%s", path);
                    g_sess.aes = true;
                    ok = true;
                }
                explicit_bzero(sb, sizeof sb);
                BN_clear_free(shared);
                BN_free(spub);
            }
            free(path);
            dbus_message_unref(r);
        }
    }
    explicit_bzero(rnd, sizeof rnd);
    BN_CTX_free(bc);
    BN_clear_free(priv);
    BN_free(pub);
    BN_free(g);
    BN_free(p);
    if (ok) return true;
    if (err_is(err, "ServiceUnknown") || err_is(err, "not provided by any .service") || err_is(err, "was not provided")) return false;
    DBusMessage *m = msg_call(SS_PATH, SS_IFACE, "OpenSession"); // the service doesn't do the encrypted one
    DBusMessageIter it;
    dbus_message_iter_init_append(m, &it);
    app_str(&it, "plain");
    app_var_str(&it, "");
    DBusMessage *r = bus_call(m, err, cap);
    if (!r) return false;
    DBusMessageIter ri;
    dbus_message_iter_init(r, &ri);
    dbus_message_iter_next(&ri);
    char *path = it_dup_str(&ri);
    snprintf(g_sess.path, sizeof g_sess.path, "%s", path);
    free(path);
    dbus_message_unref(r);
    return g_sess.path[0] != 0;
}

// Append a Secret struct (session, parameters, value, content type) holding plaintext.
static bool app_secret(DBusMessageIter *it, const SecBuf *plain, const char *ctype) {
    DBusMessageIter s;
    dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, NULL, &s);
    app_obj(&s, g_sess.path);
    bool ok = true;
    if (g_sess.aes) {
        u8 iv[16];
        if (getrandom(iv, sizeof iv, 0) != (ssize_t)sizeof iv) ok = false;
        SecBuf enc = {0};
        sec_alloc(&enc, plain->n + 16);
        int l1 = 0, l2 = 0;
        EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
        if (!ok || !c || !EVP_EncryptInit_ex(c, EVP_aes_128_cbc(), NULL, g_sess.key, iv) ||
            !EVP_EncryptUpdate(c, enc.p, &l1, plain->p ? plain->p : (const u8 *)"", (int)plain->n) ||
            !EVP_EncryptFinal_ex(c, enc.p + l1, &l2))
            ok = false;
        EVP_CIPHER_CTX_free(c);
        app_bytes(&s, iv, sizeof iv);
        app_bytes(&s, enc.p, ok ? (size_t)(l1 + l2) : 0);
        sec_free(&enc);
    } else {
        app_bytes(&s, NULL, 0);
        app_bytes(&s, plain->p, plain->n);
    }
    app_str(&s, ctype && ctype[0] ? ctype : "text/plain");
    dbus_message_iter_close_container(it, &s);
    return ok;
}

// Read a Secret struct into plaintext (+ its content type, malloc'd).
static bool it_secret(DBusMessageIter *it, SecBuf *out, char **ctype) {
    DBusMessageIter s;
    *ctype = NULL;
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_STRUCT) return false;
    dbus_message_iter_recurse(it, &s);
    dbus_message_iter_next(&s); // session
    const u8 *par, *val;
    int pn, vn;
    it_bytes(&s, &par, &pn);
    dbus_message_iter_next(&s);
    it_bytes(&s, &val, &vn);
    dbus_message_iter_next(&s);
    *ctype = it_dup_str(&s);
    if (!g_sess.aes) { sec_set(out, val, (size_t)MAX(vn, 0)); return true; }
    if (pn != 16 || vn <= 0 || vn % 16) return false;
    SecBuf dec = {0};
    sec_alloc(&dec, (size_t)vn);
    int l1 = 0, l2 = 0;
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    bool ok = c && EVP_DecryptInit_ex(c, EVP_aes_128_cbc(), NULL, g_sess.key, par) && EVP_DecryptUpdate(c, dec.p, &l1, val, vn) &&
              EVP_DecryptFinal_ex(c, dec.p + l1, &l2);
    EVP_CIPHER_CTX_free(c);
    if (ok) {
        dec.n = (size_t)(l1 + l2);
        dec.p[dec.n] = 0;
        sec_move(out, &dec);
    }
    sec_free(&dec);
    return ok;
}
