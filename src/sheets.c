// sheets.c - modal sheets: the item editor (new / edit), the keyring name (new / rename), Options (General,
// Keyboard); plus the small widgets they share with render.c (fields, buttons, chips, switches)

// --- widgets -------------------------------------------------------------------

static void draw_chevron(f32 cx, f32 cy, f32 s, int dir, f32 t, Color c) {
    // dir: 0 left, 1 right, 2 up, 3 down
    f32 h = s * 0.5f, q = s * 0.25f;
    switch (dir) {
    case 0: draw_line(cx + q, cy - h, cx - q, cy, t, c); draw_line(cx - q, cy, cx + q, cy + h, t, c); break;
    case 1: draw_line(cx - q, cy - h, cx + q, cy, t, c); draw_line(cx + q, cy, cx - q, cy + h, t, c); break;
    case 2: draw_line(cx - h, cy + q, cx, cy - q, t, c); draw_line(cx, cy - q, cx + h, cy + q, t, c); break;
    case 3: draw_line(cx - h, cy - q, cx, cy + q, t, c); draw_line(cx, cy + q, cx + h, cy - q, t, c); break;
    }
}

static void draw_cross(f32 cx, f32 cy, f32 s, f32 t, Color c) {
    f32 h = s * 0.5f;
    draw_line(cx - h, cy - h, cx + h, cy + h, t, c);
    draw_line(cx - h, cy + h, cx + h, cy - h, t, c);
}

// An eye (shown) or an eye with a stroke through it (hidden).
static void draw_eye_icon(f32 cx, f32 cy, f32 hw, Color c, bool slash) {
    f32 t = MAX(1.f, roundf(S(1.4f))), hh = hw * 0.62f;
    int seg = 10;
    for (int side = -1; side <= 1; side += 2) // two arcs meeting at the corners
        for (int i = 0; i < seg; i++) {
            f32 a0 = (f32)i / seg, a1 = (f32)(i + 1) / seg;
            f32 x0 = cx - hw + 2 * hw * a0, x1 = cx - hw + 2 * hw * a1;
            f32 y0 = cy + side * hh * sinf(a0 * (f32)M_PI), y1 = cy + side * hh * sinf(a1 * (f32)M_PI);
            draw_line(x0, y0, x1, y1, t, c);
        }
    f32 r = roundf(hw * 0.36f);
    draw_rrect(cx - r, cy - r, r * 2, r * 2, r, c);
    if (slash) draw_line(cx - hw * 0.85f, cy + hw * 0.85f, cx + hw * 0.85f, cy - hw * 0.85f, t, c);
}

// Copy: two overlapping pages.
static void draw_copy_icon(f32 x, f32 y, f32 s, Color c, u32 bg) {
    f32 t = MAX(1.f, roundf(S(1.4f))), w = roundf(s * 0.62f), h = roundf(s * 0.72f), r = S(2);
    draw_rrect_outline(x, y, w, h, r, t, c);
    f32 fx = x + s - w, fy = y + s - h;
    draw_rrect(fx - t, fy - t, w + 2 * t, h + 2 * t, r + t, C(bg)); // (gap around the front page)
    draw_rrect_outline(fx, fy, w, h, r, t, c);
}

// A padlock, closed or open, in colour c (bg: unused, kept for the callers' layout).
static void draw_lock_icon(f32 cx, f32 cy, f32 s, Color c, bool open, u32 bg) {
    (void)bg;
    draw_icon_tinted(cx, cy, roundf(s * 1.15f), open ? ICON_LOCK_OPEN : ICON_LOCK_CLOSED, c);
}

static void te_draw(TextEdit *t, Font *f, Rect r, f32 pad, Color tc, bool focused) {
    ClipSave cs = draw_clip_push((SDL_Rect){(int)r.x + 1, (int)r.y, (int)r.w - 2, (int)r.h});
    static char dots[sizeof t->buf * 3];
    const char *s = t->buf;
    int n = (int)strlen(t->buf), caret = t->caret, lo = MIN(t->anchor, t->caret), hi = MAX(t->anchor, t->caret);
    if (t->secret) { // one dot per character, the caret and selection mapped onto the dots
        int k = 0, ci = 0, li = 0, hi2 = 0;
        for (int i = 0; i <= n; i++) {
            if (i == t->caret) ci = k;
            if (i == lo) li = k;
            if (i == hi) hi2 = k;
            if (i == n) break;
            if (((u8)t->buf[i] & 0xC0) == 0x80) continue;
            memcpy(dots + k, "\xE2\x80\xA2", 3);
            k += 3;
        }
        dots[k] = 0;
        s = dots;
        n = k;
        caret = ci;
        lo = li;
        hi = hi2;
    }
    f32 cx = text_width(f, s, caret);
    f32 avail = r.w - pad * 2 - S(2);
    if (cx - t->scroll > avail) t->scroll = cx - avail;
    if (cx - t->scroll < 0) t->scroll = cx;
    f32 tw = text_width(f, s, n);
    if (tw - t->scroll < avail) t->scroll = MAX(0.f, tw - avail);
    f32 x = r.x + pad - t->scroll, ty = text_y(f, r.y, r.h);
    if (lo != hi && focused) {
        f32 a = text_width(f, s, lo), b = text_width(f, s, hi);
        draw_rect(roundf(x + a), r.y + roundf(S(4)), roundf(b - a), r.h - roundf(S(8)), C(COL_SEL_FOCUS));
    }
    draw_text(f, x, ty, s, n, tc);
    if (focused) draw_rect(roundf(x + cx), r.y + roundf(S(5)), MAX(1.f, roundf(S(1.2f))), r.h - roundf(S(10)), C(COL_ACCENT));
    if (t->secret) explicit_bzero(dots, sizeof dots); // (only dots, but no need to keep the length around)
    draw_clip_pop(cs);
}

// Caret position for a click at x inside a field drawn at r with padding pad.
static int te_hit(TextEdit *t, Font *f, Rect r, f32 pad, f32 x) {
    f32 rel = x - (r.x + pad) + t->scroll, acc = 0;
    int n = (int)strlen(t->buf), i = 0;
    while (i < n) {
        int j = te_next(t, i);
        f32 w = t->secret ? text_width(f, "\xE2\x80\xA2", 3) : text_width(f, t->buf + i, j - i);
        if (acc + w * 0.5f > rel) break;
        acc += w;
        i = j;
    }
    return i;
}

static bool hovering(Rect r) { return A.mouse_in && in_rect(r, A.mx, A.my); }

// Button: primary (accent), danger (red), or plain; returns its rect.
static void draw_button(Rect r, const char *label, bool primary, bool danger, bool enabled) {
    bool hv = enabled && hovering(r);
    u32 bg = primary ? (danger ? (hv ? rgb_mix(COL_DANGER_FILL, 0xFFFFFF, 0.12f) : COL_DANGER_FILL) : (hv ? COL_ACCENT_HOVER : COL_ACCENT))
                     : (hv ? COL_CONTROL_HOVER : COL_CONTROL);
    if (!enabled) g_alpha = 0.5f;
    draw_rrect(r.x, r.y, r.w, r.h, S(7), C(bg));
    Font *f = &A.font;
    f32 tw = text_width(f, label, (int)strlen(label));
    draw_text(f, roundf(r.x + (r.w - tw) * 0.5f), text_y(f, r.y, r.h), label, (int)strlen(label), C(primary ? COL_ON_ACCENT : COL_TEXT));
    g_alpha = 1.f;
}

static f32 button_w(const char *label) { return MAX(roundf(S(88)), roundf(text_width(&A.font, label, (int)strlen(label)) + S(28))); }

static void draw_field_bg(Rect r, bool focused) {
    draw_rrect(r.x, r.y, r.w, r.h, S(6), C(focused ? COL_FIELD_HOVER : hovering(r) ? COL_FIELD_HOVER : COL_FIELD));
    if (focused) draw_rrect_outline(r.x, r.y, r.w, r.h, S(6), MAX(1.f, S(1.2f)), C(COL_ACCENT));
}

static void draw_chip(Rect r, const char *label, bool on) {
    bool hv = hovering(r);
    draw_rrect(r.x, r.y, r.w, r.h, S(6), C(on ? COL_SEL_FOCUS : hv ? COL_CONTROL_HOVER : COL_CONTROL));
    if (on) draw_rrect_outline(r.x, r.y, r.w, r.h, S(6), MAX(1.f, S(1.f)), CA(COL_ACCENT, 0.8f));
    Font *f = &A.font_small;
    f32 tw = text_width(f, label, (int)strlen(label));
    draw_text(f, roundf(r.x + (r.w - tw) * 0.5f), text_y(f, r.y, r.h), label, (int)strlen(label), C(on ? COL_SEL_TEXT : COL_TEXT2));
}

static f32 chip_w(const char *label) { return roundf(text_width(&A.font_small, label, (int)strlen(label)) + S(22)); }

static void draw_switch(Rect t, bool on) {
    f32 h = roundf(S(18)), w = roundf(S(32)), x = t.x + t.w - w, y = roundf(t.y + (t.h - h) * 0.5f);
    draw_rrect(x, y, w, h, h * 0.5f, C(on ? COL_ACCENT : COL_TRACK));
    f32 k = h - roundf(S(4)), kx = on ? x + w - k - S(2) : x + S(2);
    draw_rrect(kx, y + S(2), k, k, k * 0.5f, C(on ? COL_ON_ACCENT : COL_KNOB_OFF));
}

static int wrap_text(Font *f, const char *s, f32 maxw, int *starts, int *lens, int max) {
    int n = 0, len = (int)strlen(s), i = 0;
    while (i < len && n < max) {
        int best = -1, j = i;
        while (j <= len) {
            if (j == len || s[j] == ' ' || s[j] == '\n') {
                if (text_width(f, s + i, j - i) <= maxw || best < 0) best = j;
                else break;
                if (j == len || s[j] == '\n') break;
            }
            j++;
        }
        if (best < 0) best = len;
        starts[n] = i;
        lens[n] = best - i;
        n++;
        i = best + 1;
    }
    return n;
}

// --- sheet state -------------------------------------------------------------------

enum { SHEET_NONE, SHEET_ITEM, SHEET_NAME, SHEET_OPTIONS };
#define MAX_ATTR_ROWS 24

typedef struct { TextEdit k, v; } AttrRow;

// Hit targets registered while drawing (topmost last), used by the next click.
enum {
    H_NONE, H_FIELD, H_EYE, H_GEN, H_COLL, H_ATTR_DEL, H_ATTR_ADD, H_OK, H_CANCEL, H_BIN_REPLACE, H_TAB, H_THEME, H_ACCENT,
    H_FONT_DEC, H_FONT_INC, H_CLIP, H_IDLE, H_ALTROWS, H_SMOOTH, H_TITLEBAR, H_COLTOG, H_KEY, H_KEY_RESET, H_KEYS_RESET_ALL,
    H_SHEET
};
typedef struct { Rect r; int id, arg; } Hit;

static struct {
    // the secret field first: this struct is locked into memory (sheet_init) and wiped when the sheet closes
    TextEdit secret;
    int kind;
    // item editor
    char path[512];
    int coll;
    TextEdit label;
    AttrRow attrs[MAX_ATTR_ROWS];
    int nattrs;
    int focus; // field: 0 label, 1 secret, 2 + 2*i attribute key, 3 + 2*i value
    bool show_secret, secret_loaded, secret_binary, secret_changed, secret_failed;
    size_t bin_len;
    char ctype[64], orig_label[1024];
    u32 secret_req;
    Rect fields[2 + 2 * MAX_ATTR_ROWS];
    // keyring name
    int name_coll; // -1: new keyring
    // options
    int page, rebind;
    f32 scroll, content_h;
    Rect r, body;
    Hit hits[160];
    int nhits;
} g_sheet;

static void sheet_init(void) {
    mlock(&g_sheet, sizeof g_sheet);
    g_sheet.secret.secret = true;
}

static bool sheet_open(void) { return g_sheet.kind != SHEET_NONE; }

static void sheet_close(void) {
    te_clear(&g_sheet.secret);
    g_sheet.kind = SHEET_NONE;
    g_sheet.rebind = -1;
    g_sheet.secret_req = 0;
    A.redraw = true;
}

static void hit_add(Rect r, int id, int arg) {
    if (g_sheet.nhits < (int)ARRAY_COUNT(g_sheet.hits)) g_sheet.hits[g_sheet.nhits++] = (Hit){r, id, arg};
}

static TextEdit *sheet_field(int i) {
    if (g_sheet.kind == SHEET_NAME) return i == 0 ? &g_sheet.label : NULL;
    if (i == 0) return &g_sheet.label;
    if (i == 1) return g_sheet.secret_binary ? NULL : &g_sheet.secret;
    int k = (i - 2) / 2;
    if (k >= g_sheet.nattrs) return NULL;
    return (i - 2) % 2 ? &g_sheet.attrs[k].v : &g_sheet.attrs[k].k;
}

static int sheet_nfields(void) { return g_sheet.kind == SHEET_NAME ? 1 : g_sheet.kind == SHEET_ITEM ? 2 + 2 * g_sheet.nattrs : 0; }

static void sheet_focus(int i) {
    TextEdit *t = sheet_field(i);
    if (!t) return;
    g_sheet.focus = i;
    t->anchor = 0;
    t->caret = (int)strlen(t->buf);
    A.redraw = true;
}

static void attr_row_add(const char *k, const char *v) {
    if (g_sheet.nattrs >= MAX_ATTR_ROWS) return;
    AttrRow *r = &g_sheet.attrs[g_sheet.nattrs++];
    te_set(&r->k, k, 0, 0);
    te_set(&r->v, v, 0, 0);
}

// --- item editor ---------------------------------------------------------------

static void sheet_open_item(Item *it, int coll) {
    if (it && it->locked) { action_unlock_item(it); toast("Unlock the keyring to edit its items"); return; }
    if (!it && (coll < 0 || coll >= A.ncolls)) { toast("There's no keyring to put it in"); return; }
    A.menu.open = A.pal_open = false;
    te_clear(&g_sheet.secret);
    g_sheet.kind = SHEET_ITEM;
    g_sheet.coll = coll;
    g_sheet.nattrs = 0;
    g_sheet.show_secret = false;
    g_sheet.secret_binary = g_sheet.secret_changed = g_sheet.secret_failed = false;
    g_sheet.bin_len = 0;
    g_sheet.scroll = 0;
    snprintf(g_sheet.ctype, sizeof g_sheet.ctype, "text/plain");
    if (it) {
        snprintf(g_sheet.path, sizeof g_sheet.path, "%s", it->path);
        te_set(&g_sheet.label, it->label, 0, (int)strlen(it->label));
        snprintf(g_sheet.orig_label, sizeof g_sheet.orig_label, "%s", it->label);
        for (int i = 0; i < it->nattrs; i++) attr_row_add(it->attrs[i].k, it->attrs[i].v);
        g_sheet.secret_loaded = false;
        g_sheet.secret_req = ss_get_secret(it->path, PURPOSE_EDIT);
    } else { // a generic password, the attributes libsecret's tools use
        g_sheet.path[0] = 0;
        te_set(&g_sheet.label, "", 0, 0);
        g_sheet.orig_label[0] = 0;
        attr_row_add("xdg:schema", "org.freedesktop.Secret.Generic");
        attr_row_add("service", "");
        attr_row_add("username", "");
        g_sheet.secret_loaded = true;
        g_sheet.secret_req = 0;
    }
    sheet_focus(0);
}

static void sheet_secret_loaded(SsEvent *ev) {
    if (g_sheet.kind != SHEET_ITEM || ev->req != g_sheet.secret_req) return;
    g_sheet.secret_loaded = true;
    snprintf(g_sheet.ctype, sizeof g_sheet.ctype, "%s", ev->ctype && ev->ctype[0] ? ev->ctype : "text/plain");
    if (sec_is_text(&ev->secret) && ev->secret.n < sizeof g_sheet.secret.buf) {
        memcpy(g_sheet.secret.buf, ev->secret.p ? (const char *)ev->secret.p : "", ev->secret.n);
        g_sheet.secret.buf[ev->secret.n] = 0;
        g_sheet.secret.caret = g_sheet.secret.anchor = (int)ev->secret.n;
    } else {
        g_sheet.secret_binary = true; // kept as it is unless replaced
        g_sheet.bin_len = ev->secret.n;
    }
    A.redraw = true;
}

// A random password: 20 characters from letters, digits and a few symbols that survive most forms.
static void sheet_generate(void) {
    static const char set[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789-_.!@#%+=";
    u8 rnd[64];
    if (getrandom(rnd, sizeof rnd, 0) != (ssize_t)sizeof rnd) return;
    te_clear(&g_sheet.secret);
    int n = 0, k = 0;
    const int len = 20, m = (int)sizeof set - 1;
    while (n < len && k < (int)sizeof rnd) {
        u8 b = rnd[k++];
        if (b >= 256 - 256 % m) continue; // (no modulo bias)
        g_sheet.secret.buf[n++] = set[b % m];
    }
    explicit_bzero(rnd, sizeof rnd);
    g_sheet.secret.buf[n] = 0;
    g_sheet.secret.caret = g_sheet.secret.anchor = n;
    g_sheet.secret_binary = false;
    g_sheet.secret_changed = true;
    g_sheet.show_secret = true;
    A.redraw = true;
}

static void sheet_save_item(void) {
    if (!g_sheet.secret_loaded && g_sheet.path[0]) { toast("Still reading the secret\xE2\x80\xA6"); return; }
    Attr *at = xcalloc(MAX_ATTR_ROWS, sizeof *at);
    int n = 0;
    for (int i = 0; i < g_sheet.nattrs; i++) {
        const char *k = g_sheet.attrs[i].k.buf, *v = g_sheet.attrs[i].v.buf;
        if (!k[0] && !v[0]) continue;
        if (!k[0]) { attrs_free(at, n); toast("An attribute needs a name"); sheet_focus(2 + 2 * i); return; }
        bool dup = false;
        for (int j = 0; j < n; j++) dup |= !strcmp(at[j].k, k);
        if (dup) { attrs_free(at, n); toast("\xE2\x80\x9C%s\xE2\x80\x9D is there twice", k); sheet_focus(2 + 2 * i); return; }
        at[n].k = strdup(k);
        at[n].v = strdup(v);
        n++;
    }
    if (g_sheet.path[0]) {
        Item *it = item_by_path(g_sheet.path);
        SsReq *r = ss_req(RQ_SET_ITEM);
        r->path = strdup(g_sheet.path);
        r->set_label = strcmp(g_sheet.label.buf, g_sheet.orig_label) != 0;
        r->label = strdup(g_sheet.label.buf);
        bool same = it && it->nattrs == n;
        for (int i = 0; same && i < n; i++) {
            const char *v = attr_get(it, at[i].k);
            same = v && !strcmp(v, at[i].v);
        }
        r->set_attrs = !same;
        r->attrs = at;
        r->nattrs = n;
        if (g_sheet.secret_changed && !g_sheet.secret_binary) {
            r->set_secret = true;
            sec_set(&r->secret, g_sheet.secret.buf, strlen(g_sheet.secret.buf));
            r->ctype = strdup(g_sheet.ctype[0] ? g_sheet.ctype : "text/plain");
        }
        if (!r->set_label && !r->set_attrs && !r->set_secret) { req_free(r); sheet_close(); return; }
        pending_add(ss_submit(r), PW_SAVE, NULL);
    } else {
        if (g_sheet.coll < 0 || g_sheet.coll >= A.ncolls) { attrs_free(at, n); toast("Pick a keyring for it"); return; }
        SsReq *r = ss_req(RQ_CREATE_ITEM);
        r->target = strdup(A.colls[g_sheet.coll].path);
        r->label = strdup(g_sheet.label.buf);
        r->attrs = at;
        r->nattrs = n;
        sec_set(&r->secret, g_sheet.secret.buf, strlen(g_sheet.secret.buf));
        r->ctype = strdup("text/plain");
        pending_add(ss_submit(r), PW_CREATE_ITEM, NULL);
    }
    sheet_close();
}

// --- keyring name ----------------------------------------------------------------

static void sheet_open_name(int coll) {
    A.menu.open = A.pal_open = false;
    g_sheet.kind = SHEET_NAME;
    g_sheet.name_coll = coll;
    const char *cur = coll >= 0 && coll < A.ncolls ? A.colls[coll].label : "";
    te_set(&g_sheet.label, cur, 0, (int)strlen(cur));
    sheet_focus(0);
}

static void sheet_save_name(void) {
    const char *name = g_sheet.label.buf;
    if (!name[0]) { toast("Give it a name"); return; }
    if (g_sheet.name_coll >= 0 && g_sheet.name_coll < A.ncolls)
        pending_add(ss_simple(RQ_RENAME_COLL, A.colls[g_sheet.name_coll].path, name), PW_GENERIC, NULL);
    else pending_add(ss_simple(RQ_CREATE_COLL, NULL, name), PW_CREATE_COLL, NULL);
    sheet_close();
}

// --- options ----------------------------------------------------------------------

static void sheet_open_options(int page) {
    A.menu.open = A.pal_open = false;
    g_sheet.kind = SHEET_OPTIONS;
    g_sheet.page = page;
    g_sheet.rebind = -1;
    g_sheet.scroll = 0;
    A.redraw = true;
}

static const int g_clip_choices[] = {10, 30, 60, 120, 0};
static const int g_idle_choices[] = {0, 5, 15, 60};

static void font_size_set(int sz) {
    sz = CLAMP(sz, 9, 24);
    if (sz == A.font_size) return;
    A.font_size = sz;
    f32 z = A.zoom;
    A.zoom = -1; // (force zoom_set to rebuild the fonts)
    zoom_set(z);
    A.toast[0] = 0;
}

// --- sheet input -----------------------------------------------------------------

static void sheet_cancel(void) {
    if (g_sheet.kind == SHEET_OPTIONS && g_sheet.rebind >= 0) { g_sheet.rebind = -1; A.redraw = true; return; }
    sheet_close();
}

static void sheet_ok(void) {
    if (g_sheet.kind == SHEET_ITEM) sheet_save_item();
    else if (g_sheet.kind == SHEET_NAME) sheet_save_name();
    else sheet_close();
}

static bool sheet_key(SDL_Keycode key, SDL_Keymod mod) {
    if (!sheet_open()) return false;
    if (g_sheet.kind == SHEET_OPTIONS && g_sheet.rebind >= 0) { // Options > Keyboard: this press is the new shortcut
        if (key == SDLK_ESCAPE) { g_sheet.rebind = -1; return true; }
        if (key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LALT ||
            key == SDLK_RALT || key == SDLK_LGUI || key == SDLK_RGUI)
            return true;
        Combo c = {key, key_mods(mod)};
        if (key == SDLK_BACKSPACE && !c.mods) memset(&c, 0, sizeof c); // Backspace: no shortcut
        key_rebind(g_sheet.rebind, c);
        g_sheet.rebind = -1;
        return true;
    }
    if (key == SDLK_ESCAPE) { sheet_cancel(); return true; }
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !(mod & SDL_KMOD_SHIFT)) { sheet_ok(); return true; }
    int nf = sheet_nfields();
    if (key == SDLK_TAB && nf) {
        int i = g_sheet.focus;
        for (int k = 0; k < nf; k++) {
            i = (i + ((mod & SDL_KMOD_SHIFT) ? nf - 1 : 1)) % nf;
            if (sheet_field(i)) break;
        }
        sheet_focus(i);
        return true;
    }
    if (g_sheet.kind == SHEET_OPTIONS) {
        if (key == SDLK_UP || key == SDLK_DOWN) { g_sheet.scroll += (key == SDLK_UP ? -1 : 1) * S(40); A.redraw = true; }
        return true;
    }
    if (g_sheet.kind == SHEET_ITEM && (mod & SDL_KMOD_CTRL) && key == SDLK_G) { sheet_generate(); return true; }
    if (g_sheet.kind == SHEET_ITEM && (mod & SDL_KMOD_CTRL) && key == SDLK_R) { g_sheet.show_secret = !g_sheet.show_secret; return true; }
    TextEdit *t = sheet_field(g_sheet.focus);
    if (t) {
        bool used = te_key(t, key, mod);
        if (used && t == &g_sheet.secret && (key == SDLK_BACKSPACE || key == SDLK_DELETE || ((mod & SDL_KMOD_CTRL) && (key == SDLK_V || key == SDLK_X))))
            g_sheet.secret_changed = true;
        A.redraw = true;
        return true;
    }
    return true; // (the window underneath gets nothing while a sheet is open)
}

static void sheet_text(const char *s) {
    if (!sheet_open() || (g_sheet.kind == SHEET_OPTIONS)) return;
    TextEdit *t = sheet_field(g_sheet.focus);
    if (!t) return;
    te_insert(t, s);
    if (t == &g_sheet.secret) g_sheet.secret_changed = true;
    A.redraw = true;
}

static bool g_sheet_drag_sel; // dragging a selection inside a field

static void sheet_mouse_down(SDL_MouseButtonEvent *b, f32 x, f32 y) {
    if (b->button != SDL_BUTTON_LEFT) return;
    if (!in_rect(g_sheet.r, x, y)) { // outside the card: Options closes, editors stay (a stray click loses no typing)
        if (g_sheet.kind == SHEET_OPTIONS && g_sheet.rebind < 0) sheet_close();
        return;
    }
    Hit *h = NULL;
    for (int i = g_sheet.nhits - 1; i >= 0; i--)
        if (in_rect(g_sheet.hits[i].r, x, y)) { h = &g_sheet.hits[i]; break; }
    if (!h) return;
    A.redraw = true;
    switch (h->id) {
    case H_FIELD: {
        TextEdit *t = sheet_field(h->arg);
        if (!t) break;
        g_sheet.focus = h->arg;
        Font *f = h->arg == 1 && g_sheet.show_secret ? &A.font_mono : &A.font;
        bool sec = t->secret;
        if (h->arg == 1) t->secret = !g_sheet.show_secret;
        int c = te_hit(t, f, h->r, roundf(S(10)), x);
        t->secret = sec;
        if (b->clicks >= 2) { t->anchor = 0; t->caret = (int)strlen(t->buf); }
        else {
            t->caret = c;
            if (!(SDL_GetModState() & SDL_KMOD_SHIFT)) t->anchor = c;
            g_sheet_drag_sel = true;
        }
        break;
    }
    case H_EYE: g_sheet.show_secret = !g_sheet.show_secret; break;
    case H_GEN: sheet_generate(); break;
    case H_BIN_REPLACE:
        g_sheet.secret_binary = false;
        g_sheet.secret_changed = true;
        te_clear(&g_sheet.secret);
        sheet_focus(1);
        break;
    case H_COLL: g_sheet.coll = h->arg; break;
    case H_ATTR_DEL:
        if (h->arg >= 0 && h->arg < g_sheet.nattrs) {
            memmove(&g_sheet.attrs[h->arg], &g_sheet.attrs[h->arg + 1], (size_t)(g_sheet.nattrs - h->arg - 1) * sizeof(AttrRow));
            g_sheet.nattrs--;
            if (g_sheet.focus >= 2) g_sheet.focus = 0;
        }
        break;
    case H_ATTR_ADD:
        attr_row_add("", "");
        sheet_focus(2 + 2 * (g_sheet.nattrs - 1));
        break;
    case H_OK: sheet_ok(); break;
    case H_CANCEL: sheet_close(); break;
    case H_TAB: g_sheet.page = h->arg; g_sheet.scroll = 0; g_sheet.rebind = -1; break;
    case H_THEME:
        g_theme_cur = h->arg;
        snprintf(A.theme_name, sizeof A.theme_name, "%s", g_themes[h->arg].name);
        theme_apply();
        A.settings_dirty = true;
        break;
    case H_ACCENT:
        g_accent_custom = h->arg < 0 ? 0 : 0x01000000u | g_accents[h->arg].rgb;
        theme_apply();
        A.settings_dirty = true;
        break;
    case H_FONT_DEC: font_size_set(A.font_size - 1); A.settings_dirty = true; break;
    case H_FONT_INC: font_size_set(A.font_size + 1); A.settings_dirty = true; break;
    case H_CLIP: A.clip_secs = h->arg; A.settings_dirty = true; break;
    case H_IDLE: A.idle_lock_min = h->arg; A.idle_locked = false; A.settings_dirty = true; break;
    case H_ALTROWS: A.alt_rows = !A.alt_rows; A.settings_dirty = true; break;
    case H_SMOOTH: A.smooth_scroll = !A.smooth_scroll; A.settings_dirty = true; break;
    case H_TITLEBAR: A.title_bar = h->arg; titlebar_apply(); A.settings_dirty = true; break;
    case H_COLTOG: run_command(CMD_TOGGLE_COL, h->arg); break;
    case H_KEY: g_sheet.rebind = h->arg; break;
    case H_KEY_RESET: {
        Combo c;
        combo_parse(g_keys[h->arg].def, &c);
        key_rebind(h->arg, c);
        break;
    }
    case H_KEYS_RESET_ALL: keys_defaults(); A.settings_dirty = true; break;
    default: break;
    }
}

static void sheet_mouse_motion(f32 x) {
    if (!g_sheet_drag_sel) return;
    for (int i = 0; i < g_sheet.nhits; i++) {
        Hit *h = &g_sheet.hits[i];
        if (h->id != H_FIELD || h->arg != g_sheet.focus) continue;
        TextEdit *t = sheet_field(h->arg);
        if (!t) return;
        bool sec = t->secret;
        if (h->arg == 1) t->secret = !g_sheet.show_secret;
        t->caret = te_hit(t, h->arg == 1 && g_sheet.show_secret ? &A.font_mono : &A.font, h->r, roundf(S(10)), x);
        t->secret = sec;
        A.redraw = true;
        return;
    }
}

static void sheet_wheel(f32 dy) {
    if (g_sheet.kind == SHEET_OPTIONS || g_sheet.kind == SHEET_ITEM) {
        g_sheet.scroll -= dy * S(48);
        A.redraw = true;
    }
}

// --- sheet drawing ------------------------------------------------------------------

static void sheet_card(f32 w, f32 h, const char *title) {
    draw_rect(0, 0, (f32)A.W, (f32)A.H, CA(0x000000, 0.45f));
    w = roundf(MIN(w, A.W - S(40)));
    h = roundf(MIN(h, A.H - S(40)));
    f32 x = roundf((A.W - w) * 0.5f), y = roundf(MAX(S(20), (A.H - h) * 0.42f));
    g_sheet.r = (Rect){x, y, w, h};
    g_sheet.nhits = 0;
    hit_add(g_sheet.r, H_SHEET, 0);
    draw_rrect(x, y, w, h, S(12), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(12), 1, C(COL_POPUP_BORDER));
    Font *tf = &A.font_brand;
    draw_text(tf, x + roundf(S(22)), y + roundf(S(20)), title, (int)strlen(title), C(COL_TEXT_STRONG));
}

// Buttons at the bottom right of the card: Cancel + the main one.
static void sheet_buttons(const char *ok, const char *cancel, bool ok_enabled) {
    Rect r = g_sheet.r;
    f32 pad = roundf(S(22)), bh = roundf(S(32)), bw = button_w(ok);
    Rect okr = {r.x + r.w - pad - bw, r.y + r.h - pad - bh, bw, bh};
    draw_button(okr, ok, true, false, ok_enabled);
    if (ok_enabled) hit_add(okr, H_OK, 0);
    if (cancel) {
        f32 cw = button_w(cancel);
        Rect cr = {okr.x - S(8) - cw, okr.y, cw, bh};
        draw_button(cr, cancel, false, false, true);
        hit_add(cr, H_CANCEL, 0);
    }
}

static void field_draw(Rect r, int idx, TextEdit *t, const char *placeholder, Font *f) {
    bool focused = g_sheet.focus == idx;
    draw_field_bg(r, focused);
    te_draw(t, f, r, roundf(S(10)), C(COL_TEXT), focused);
    if (!t->buf[0] && placeholder) draw_text(&A.font, r.x + roundf(S(10)), text_y(&A.font, r.y, r.h), placeholder, (int)strlen(placeholder), C(COL_TEXT_FAINT));
    hit_add(r, H_FIELD, idx);
}

static void draw_label(f32 x, f32 y, const char *s) {
    draw_text(&A.font_small, x, y, s, (int)strlen(s), C(COL_TEXT_DIM));
}

static void draw_sheet_item(void) {
    bool editing = g_sheet.path[0] != 0;
    f32 fh = roundf(S(32)), gap = roundf(S(12)), pad = roundf(S(22));
    f32 attrs_h = g_sheet.nattrs * (fh + S(6)) + S(30);
    f32 want = S(70) + 2 * (S(20) + fh + gap) + (editing ? 0 : S(20) + S(28) + gap) + S(34) + attrs_h + S(76);
    sheet_card(S(620), want, editing ? "Edit item" : "New item");
    Rect R = g_sheet.r;
    f32 x = R.x + pad, w = R.w - pad * 2, y = R.y + roundf(S(62));
    f32 body_bottom = R.y + R.h - roundf(S(76));
    Font *f = &A.font, *fs = &A.font_small;
    // name
    draw_label(x, y, "Name");
    y += roundf(S(20));
    field_draw((Rect){x, y, w, fh}, 0, &g_sheet.label, "What it's for, e.g. \xE2\x80\x9C" "example.com\xE2\x80\x9D", f);
    y += fh + gap;
    // secret: field + show + generate
    draw_label(x, y, "Secret");
    y += roundf(S(20));
    f32 bs = fh, bx = x + w - bs;
    Rect gen = {bx, y, bs, bs}, eye = {bx - bs - S(6), y, bs, bs};
    Rect sf = {x, y, eye.x - S(6) - x, fh};
    if (!g_sheet.secret_loaded) {
        draw_field_bg(sf, false);
        draw_text(f, sf.x + S(10), text_y(f, sf.y, sf.h), "Reading\xE2\x80\xA6", 10, C(COL_TEXT_FAINT));
    } else if (g_sheet.secret_binary) {
        draw_field_bg(sf, false);
        char m[96];
        snprintf(m, sizeof m, "Binary data (%zu bytes), kept as it is", g_sheet.bin_len);
        draw_text_fit(f, sf.x + S(10), text_y(f, sf.y, sf.h), sf.w - S(100), m, (int)strlen(m), C(COL_TEXT_DIM), 0);
        const char *rl = "Replace";
        f32 rw = text_width(fs, rl, 7) + S(16);
        Rect rr = {sf.x + sf.w - rw - S(4), sf.y + S(4), rw, sf.h - S(8)};
        draw_chip(rr, rl, false);
        hit_add(rr, H_BIN_REPLACE, 0);
    } else {
        g_sheet.secret.secret = !g_sheet.show_secret;
        field_draw(sf, 1, &g_sheet.secret, editing ? "(empty)" : "The password or token", g_sheet.show_secret ? &A.font_mono : f);
        g_sheet.secret.secret = true;
    }
    bool eh = hovering(eye), gh = hovering(gen);
    draw_rrect(eye.x, eye.y, eye.w, eye.h, S(6), C(eh ? COL_CONTROL_HOVER : COL_CONTROL));
    draw_eye_icon(eye.x + bs * 0.5f, eye.y + bs * 0.5f, S(7.5f), C(COL_TEXT2), g_sheet.show_secret);
    hit_add(eye, H_EYE, 0);
    draw_rrect(gen.x, gen.y, gen.w, gen.h, S(6), C(gh ? COL_CONTROL_HOVER : COL_CONTROL));
    { // dice: a rounded square with five pips
        f32 d = roundf(S(14)), dx = roundf(gen.x + (bs - d) * 0.5f), dy = roundf(gen.y + (bs - d) * 0.5f), pr = roundf(S(1.6f));
        draw_rrect_outline(dx, dy, d, d, S(3), MAX(1.f, S(1.3f)), C(COL_TEXT2));
        f32 px[5] = {0.3f, 0.7f, 0.5f, 0.3f, 0.7f}, py[5] = {0.3f, 0.3f, 0.5f, 0.7f, 0.7f};
        for (int i = 0; i < 5; i++) draw_rrect(roundf(dx + d * px[i] - pr), roundf(dy + d * py[i] - pr), pr * 2, pr * 2, pr, C(COL_TEXT2));
    }
    hit_add(gen, H_GEN, 0);
    y += fh + gap;
    // keyring (new items)
    if (!editing) {
        draw_label(x, y, "Keyring");
        y += roundf(S(20));
        f32 cx = x;
        for (int c = 0; c < A.ncolls; c++) {
            if (A.colls[c].locked) continue;
            const char *l = coll_label(c);
            f32 cw = chip_w(l);
            if (cx + cw > x + w) break;
            Rect cr = {cx, y, cw, roundf(S(28))};
            draw_chip(cr, l, g_sheet.coll == c);
            hit_add(cr, H_COLL, c);
            cx += cw + S(6);
        }
        y += roundf(S(28)) + gap;
    }
    // attributes
    draw_label(x, y, "Attributes");
    const char *hint = "apps look their secrets up by these";
    draw_text(fs, x + text_width(fs, "Attributes", 10) + S(10), y, hint, (int)strlen(hint), C(COL_TEXT_FAINT));
    y += roundf(S(22));
    ClipSave cs = draw_clip_push((SDL_Rect){(int)R.x, (int)y, (int)R.w, (int)MAX(0.f, body_bottom - y)});
    f32 list_h = body_bottom - y, content = g_sheet.nattrs * (fh + S(6)) + S(30);
    g_sheet.scroll = CLAMP(g_sheet.scroll, 0.f, MAX(0.f, content - list_h));
    f32 ay = y - g_sheet.scroll, kw = roundf((w - S(40)) * 0.38f);
    for (int i = 0; i < g_sheet.nattrs; i++) {
        Rect kr = {x, ay, kw, fh}, vr = {x + kw + S(6), ay, w - kw - S(6) - S(34), fh}, dr = {x + w - S(28), ay + S(2), S(28), fh - S(4)};
        if (ay + fh > y && ay < body_bottom) {
            field_draw(kr, 2 + 2 * i, &g_sheet.attrs[i].k, "name", fs);
            field_draw(vr, 3 + 2 * i, &g_sheet.attrs[i].v, "value", f);
            bool dh = hovering(dr);
            if (dh) draw_rrect(dr.x, dr.y, dr.w, dr.h, S(6), C(COL_CONTROL_HOVER));
            draw_cross(dr.x + dr.w * 0.5f, dr.y + dr.h * 0.5f, S(8), MAX(1.f, S(1.4f)), C(dh ? COL_DANGER : COL_TEXT_DIM));
            hit_add(dr, H_ATTR_DEL, i);
        }
        ay += fh + S(6);
    }
    if (g_sheet.nattrs < MAX_ATTR_ROWS) {
        const char *al = "+ Add attribute";
        Rect ar = {x, ay + S(2), text_width(fs, al, (int)strlen(al)) + S(16), roundf(S(24))};
        if (hovering(ar)) draw_rrect(ar.x, ar.y, ar.w, ar.h, S(6), C(COL_CONTROL_HOVER));
        draw_text(fs, ar.x + S(8), text_y(fs, ar.y, ar.h), al, (int)strlen(al), C(COL_ACCENT));
        hit_add(ar, H_ATTR_ADD, 0);
    }
    draw_clip_pop(cs);
    // footer: where it goes + the buttons
    if (editing) {
        Item *it = item_by_path(g_sheet.path);
        char m[200];
        snprintf(m, sizeof m, "In \xE2\x80\x9C%.80s\xE2\x80\x9D", it ? coll_label(it->coll) : "");
        draw_text(fs, x, text_y(fs, R.y + R.h - S(22) - S(32), S(32)), m, (int)strlen(m), C(COL_TEXT_FAINT));
    }
    sheet_buttons(editing ? "Save" : "Create", "Cancel", g_sheet.secret_loaded || !editing);
}

static void draw_sheet_name(void) {
    bool rename = g_sheet.name_coll >= 0;
    sheet_card(S(460), S(rename ? 190 : 214), rename ? "Rename keyring" : "New keyring");
    Rect R = g_sheet.r;
    f32 pad = roundf(S(22)), x = R.x + pad, w = R.w - pad * 2, y = R.y + roundf(S(62));
    field_draw((Rect){x, y, w, roundf(S(32))}, 0, &g_sheet.label, "Name", &A.font);
    if (!rename) {
        const char *m = "The keyring service asks for its password next.";
        draw_text(&A.font_small, x, y + S(42), m, (int)strlen(m), C(COL_TEXT_DIM));
    }
    sheet_buttons(rename ? "Rename" : "Create", "Cancel", g_sheet.label.buf[0] != 0);
}

// One Options row: label on the left, the control drawn by the caller in the returned rect.
static Rect opt_row(f32 *y, f32 x, f32 w, const char *label, const char *sub, f32 h) {
    Font *f = &A.font, *fs = &A.font_small;
    f32 lw = roundf(S(190));
    if (sub) {
        draw_text(f, x, *y + roundf(S(2)), label, (int)strlen(label), C(COL_TEXT));
        draw_text_fit(fs, x, *y + roundf(S(2) + f->line_h), lw - S(10), sub, (int)strlen(sub), C(COL_TEXT_FAINT), 0);
    } else draw_text(f, x, text_y(f, *y, MIN(h, S(30))), label, (int)strlen(label), C(COL_TEXT));
    Rect r = {x + lw, *y, w - lw, h};
    *y += h + roundf(S(14));
    return r;
}

// Chips in a row (wrapping); returns the height used.
static f32 chips_row(Rect r, const char *const *labels, int n, int on, int hit_id, const int *args) {
    f32 cx = r.x, cy = r.y, ch = roundf(S(28));
    for (int i = 0; i < n; i++) {
        f32 cw = chip_w(labels[i]);
        if (cx + cw > r.x + r.w && cx > r.x) { cx = r.x; cy += ch + S(6); }
        Rect c = {cx, cy, cw, ch};
        draw_chip(c, labels[i], i == on);
        hit_add(c, hit_id, args ? args[i] : i);
        cx += cw + S(6);
    }
    return cy + ch - r.y;
}

static void draw_sheet_options(void) {
    sheet_card(S(700), S(640), "Options");
    Rect R = g_sheet.r;
    f32 pad = roundf(S(22));
    { // page tabs
        const char *tabs[] = {"General", "Keyboard"};
        f32 tx = R.x + R.w - pad, th = roundf(S(28));
        for (int i = 1; i >= 0; i--) {
            f32 tw = chip_w(tabs[i]);
            tx -= tw;
            Rect t = {tx, R.y + roundf(S(16)), tw, th};
            draw_chip(t, tabs[i], g_sheet.page == i);
            hit_add(t, H_TAB, i);
            tx -= S(6);
        }
    }
    f32 top = R.y + roundf(S(60)), bottom = R.y + R.h - roundf(S(70));
    draw_rect(R.x + 1, top - S(6), R.w - 2, 1, C(COL_POPUP_BORDER));
    ClipSave cs = draw_clip_push((SDL_Rect){(int)R.x, (int)top, (int)R.w, (int)(bottom - top)});
    g_sheet.scroll = CLAMP(g_sheet.scroll, 0.f, MAX(0.f, g_sheet.content_h - (bottom - top)));
    f32 x = R.x + pad, w = R.w - pad * 2, y = top + S(8) - g_sheet.scroll, y0 = y;
    Font *f = &A.font, *fs = &A.font_small;
    if (g_sheet.page == 0) {
        { // theme
            const char *names[MAX_THEMES];
            for (int i = 0; i < g_nthemes; i++) names[i] = g_themes[i].name;
            Rect r = opt_row(&y, x, w, "Theme", "Files in Options folder/themes", S(28));
            f32 used = chips_row(r, names, g_nthemes, g_theme_cur, H_THEME, NULL);
            y += used - S(28);
        }
        { // accent swatches (theme's own first)
            Rect r = opt_row(&y, x, w, "Accent colour", NULL, S(28));
            f32 sz = roundf(S(24)), sx = r.x;
            for (int i = -1; i < ACCENT_COUNT; i++) {
                u32 col = i < 0 ? g_themes[g_theme_cur].t.accent : g_accents[i].rgb;
                bool on = i < 0 ? !g_accent_custom : g_accent_custom && (g_accent_custom & 0xFFFFFF) == g_accents[i].rgb;
                Rect s = {sx, r.y + S(2), sz, sz};
                if (on) draw_rrect_outline(s.x - S(3), s.y - S(3), sz + S(6), sz + S(6), (sz + S(6)) * 0.5f, MAX(1.5f, S(2)), C(COL_TEXT));
                draw_rrect(s.x, s.y, sz, sz, sz * 0.5f, C(col));
                if (i < 0) draw_rect(roundf(s.x + sz * 0.5f - S(0.75f)), s.y + S(5), MAX(1.f, roundf(S(1.5f))), sz - S(10), C(COL_ON_ACCENT)); // "theme's"
                hit_add(s, H_ACCENT, i);
                sx += sz + S(10);
            }
        }
        { // text size
            Rect r = opt_row(&y, x, w, "Text size", NULL, S(28));
            Rect dec = {r.x, r.y, S(28), S(28)}, inc = {r.x + S(80), r.y, S(28), S(28)};
            draw_rrect(dec.x, dec.y, dec.w, dec.h, S(6), C(hovering(dec) ? COL_CONTROL_HOVER : COL_CONTROL));
            draw_minus(dec.x + dec.w * 0.5f, dec.y + dec.h * 0.5f, S(10), MAX(1.f, S(1.5f)), C(COL_TEXT));
            draw_rrect(inc.x, inc.y, inc.w, inc.h, S(6), C(hovering(inc) ? COL_CONTROL_HOVER : COL_CONTROL));
            draw_plus(inc.x + inc.w * 0.5f, inc.y + inc.h * 0.5f, S(10), MAX(1.f, S(1.5f)), C(COL_TEXT));
            char sz[16];
            snprintf(sz, sizeof sz, "%d", A.font_size);
            draw_text_fit(f, dec.x + dec.w, text_y(f, r.y, r.h), inc.x - dec.x - dec.w, sz, (int)strlen(sz), C(COL_TEXT), 2);
            hit_add(dec, H_FONT_DEC, 0);
            hit_add(inc, H_FONT_INC, 0);
        }
        { // clipboard
            const char *l[] = {"10 s", "30 s", "1 min", "2 min", "Never"};
            int on = -1;
            for (int i = 0; i < 5; i++) if (A.clip_secs == g_clip_choices[i]) on = i;
            Rect r = opt_row(&y, x, w, "Clear copied secrets", "from the clipboard, after", S(36));
            chips_row(r, l, 5, on, H_CLIP, g_clip_choices);
        }
        { // idle lock
            const char *l[] = {"Never", "5 min", "15 min", "1 hour"};
            int on = -1;
            for (int i = 0; i < 4; i++) if (A.idle_lock_min == g_idle_choices[i]) on = i;
            Rect r = opt_row(&y, x, w, "Lock keyrings", "when this app isn't used for", S(36));
            chips_row(r, l, 4, on, H_IDLE, g_idle_choices);
        }
        { // columns
            const char *l[COL_COUNT];
            int args[COL_COUNT], n = 0;
            for (int c = COL_ACCOUNT; c < COL_COUNT; c++) { l[n] = g_coldef[c].label; args[n] = c; n++; }
            Rect r = opt_row(&y, x, w, "Columns", NULL, S(28));
            f32 cx = r.x;
            for (int i = 0; i < n; i++) {
                f32 cw = chip_w(l[i]);
                Rect c = {cx, r.y, cw, S(28)};
                draw_chip(c, l[i], A.col_on[args[i]]);
                hit_add(c, H_COLTOG, args[i]);
                cx += cw + S(6);
            }
        }
        { Rect r = opt_row(&y, x, w, "Alternating row colours", NULL, S(28)); draw_switch(r, A.alt_rows); hit_add(r, H_ALTROWS, 0); }
        { Rect r = opt_row(&y, x, w, "Smooth scrolling", NULL, S(28)); draw_switch(r, A.smooth_scroll); hit_add(r, H_SMOOTH, 0); }
        {
            const char *l[] = {"Automatic", "This app's", "The system's"};
            Rect r = opt_row(&y, x, w, "Title bar", NULL, S(28));
            chips_row(r, l, 3, A.title_bar, H_TITLEBAR, NULL);
        }
        { // where the secrets come from
            y += S(4);
            draw_rect(x, y, w, 1, C(COL_POPUP_BORDER));
            y += S(14);
            char m[400];
            if (A.ss_state == SS_READY)
                snprintf(m, sizeof m, "Secrets come from %s through the Secret Service (%s transfers). This app keeps no copy: settings.conf holds only these options.",
                         A.backend, A.encrypted ? "encrypted" : "unencrypted");
            else snprintf(m, sizeof m, "No keyring service is answering on this session.");
            int st[6], ln[6];
            int nl = wrap_text(fs, m, w, st, ln, 6);
            for (int i = 0; i < nl; i++) { draw_text(fs, x, y, m + st[i], ln[i], C(COL_TEXT_DIM)); y += roundf(fs->line_h * 1.3f); }
        }
    } else {
        f32 rh = roundf(S(32));
        const char *hint = "Click a shortcut, then press the new keys (Backspace: none, Esc: keep).";
        draw_text(fs, x, y, hint, (int)strlen(hint), C(COL_TEXT_DIM));
        y += roundf(S(26));
        for (int i = 0; i < NKEYS; i++) {
            Rect row = {x, y, w, rh};
            if (i & 1) draw_rrect(row.x - S(6), row.y, row.w + S(12), row.h, S(6), C(rgb_mix(COL_POPUP, COL_CONTROL, 0.35f)));
            const char *t = cmd_title(g_keys[i].cmd);
            draw_text(f, x + S(4), text_y(f, y, rh), t, (int)strlen(t), C(COL_TEXT));
            char lab[64];
            combo_label(&g_keys[i].cur, lab, sizeof lab);
            const char *show = g_sheet.rebind == i ? "Press keys\xE2\x80\xA6" : lab[0] ? lab : "None";
            f32 cw = MAX(S(90), chip_w(show));
            Rect c = {x + w - S(34) - cw, y + S(3), cw, rh - S(6)};
            draw_chip(c, show, g_sheet.rebind == i);
            hit_add(c, H_KEY, i);
            Combo d;
            combo_parse(g_keys[i].def, &d);
            if (d.key != g_keys[i].cur.key || d.mods != g_keys[i].cur.mods) { // changed: a reset button
                Rect rr = {x + w - S(28), y + S(3), S(28), rh - S(6)};
                bool hv = hovering(rr);
                if (hv) draw_rrect(rr.x, rr.y, rr.w, rr.h, S(6), C(COL_CONTROL_HOVER));
                draw_text_fit(fs, rr.x, text_y(fs, rr.y, rr.h), rr.w, "\xE2\x86\xBA", 3, C(hv ? COL_TEXT : COL_TEXT_DIM), 2);
                hit_add(rr, H_KEY_RESET, i);
            }
            y += rh;
        }
    }
    g_sheet.content_h = y + g_sheet.scroll - y0 + S(16);
    draw_clip_pop(cs);
    if (g_sheet.page == 1) {
        const char *rl = "Reset all";
        Rect rr = {R.x + pad, R.y + R.h - pad - S(32), button_w(rl), S(32)};
        draw_button(rr, rl, false, false, true);
        hit_add(rr, H_KEYS_RESET_ALL, 0);
    }
    sheet_buttons("Close", NULL, true);
}

static void sheet_draw(void) {
    if (g_sheet.kind == SHEET_ITEM) draw_sheet_item();
    else if (g_sheet.kind == SHEET_NAME) draw_sheet_name();
    else if (g_sheet.kind == SHEET_OPTIONS) draw_sheet_options();
}
