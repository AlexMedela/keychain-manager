// app.c - keychain-manager: window, layout, model glue, actions, menus, palette, frame loop, main()

extern char **environ;

#define S(x) ((x) * A.scale)
#define APP_NAME "Keychain Manager" // system-facing name: window title, launcher
#define APP_LABEL "Keychain"        // shown next to the logo inside the app
#define APP_ID "dev.keychainmanager"
#define APP_VERSION "0.1.0" // bumped with each tagged release

typedef struct { f32 x, y, w, h; } Rect;
static inline bool in_rect(Rect r, f32 x, f32 y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

// --- single-line text editing (search field, sheet fields) --------------------

typedef struct {
    char buf[1024];
    int caret, anchor; // selection is [min, max) of the two
    f32 scroll;
    bool secret;       // drawn as dots, never copied out
} TextEdit;

static void te_set(TextEdit *t, const char *s, int anchor, int caret) {
    snprintf(t->buf, sizeof t->buf, "%s", s);
    int n = (int)strlen(t->buf);
    t->anchor = CLAMP(anchor, 0, n);
    t->caret = CLAMP(caret, 0, n);
    t->scroll = 0;
}

static void te_clear(TextEdit *t) {
    explicit_bzero(t->buf, sizeof t->buf);
    t->caret = t->anchor = 0;
    t->scroll = 0;
}

static int te_prev(const TextEdit *t, int i) {
    if (i <= 0) return 0;
    i--;
    while (i > 0 && ((u8)t->buf[i] & 0xC0) == 0x80) i--;
    return i;
}

static int te_next(const TextEdit *t, int i) {
    int n = (int)strlen(t->buf);
    if (i >= n) return n;
    i++;
    while (i < n && ((u8)t->buf[i] & 0xC0) == 0x80) i++;
    return i;
}

static bool te_sep(char c) { return c == ' ' || c == '/' || c == '.' || c == '-' || c == '_' || c == '@' || c == ':'; }

static int te_word_left(const TextEdit *t, int i) {
    if (t->secret) return 0; // (word jumps would tell where the separators are)
    while (i > 0 && te_sep(t->buf[i - 1])) i--;
    while (i > 0 && !te_sep(t->buf[i - 1])) i--;
    return i;
}

static int te_word_right(const TextEdit *t, int i) {
    int n = (int)strlen(t->buf);
    if (t->secret) return n;
    while (i < n && te_sep(t->buf[i])) i++;
    while (i < n && !te_sep(t->buf[i])) i++;
    return i;
}

static void te_delete_sel(TextEdit *t) {
    int lo = MIN(t->anchor, t->caret), hi = MAX(t->anchor, t->caret);
    if (lo == hi) return;
    size_t n = strlen(t->buf);
    memmove(t->buf + lo, t->buf + hi, n - (size_t)hi + 1);
    explicit_bzero(t->buf + n - (hi - lo) + 1, (size_t)(hi - lo)); // (no stale copy past the end)
    t->anchor = t->caret = lo;
}

static void te_insert(TextEdit *t, const char *s) {
    te_delete_sel(t);
    size_t n = strlen(t->buf), k = strlen(s);
    if (n + k + 1 > sizeof t->buf) return;
    memmove(t->buf + t->caret + k, t->buf + t->caret, n - (size_t)t->caret + 1);
    memcpy(t->buf + t->caret, s, k);
    t->caret += (int)k;
    t->anchor = t->caret;
}

static bool te_key(TextEdit *t, SDL_Keycode key, SDL_Keymod mod) {
    bool ctrl = mod & SDL_KMOD_CTRL, shift = mod & SDL_KMOD_SHIFT;
    int n = (int)strlen(t->buf), lo = MIN(t->anchor, t->caret), hi = MAX(t->anchor, t->caret);
    switch (key) {
    case SDLK_LEFT:
        if (lo != hi && !shift) t->caret = lo;
        else t->caret = ctrl ? te_word_left(t, t->caret) : te_prev(t, t->caret);
        if (!shift) t->anchor = t->caret;
        return true;
    case SDLK_RIGHT:
        if (lo != hi && !shift) t->caret = hi;
        else t->caret = ctrl ? te_word_right(t, t->caret) : te_next(t, t->caret);
        if (!shift) t->anchor = t->caret;
        return true;
    case SDLK_HOME: t->caret = 0; if (!shift) t->anchor = 0; return true;
    case SDLK_END: t->caret = n; if (!shift) t->anchor = n; return true;
    case SDLK_BACKSPACE:
        if (lo == hi) t->anchor = ctrl ? te_word_left(t, t->caret) : te_prev(t, t->caret);
        te_delete_sel(t);
        return true;
    case SDLK_DELETE:
        if (lo == hi) t->anchor = ctrl ? te_word_right(t, t->caret) : te_next(t, t->caret);
        te_delete_sel(t);
        return true;
    case SDLK_A:
        if (ctrl) { t->anchor = 0; t->caret = n; return true; }
        break;
    case SDLK_C: case SDLK_X:
        if (!ctrl) break;
        if (lo != hi && !t->secret) { // (a secret field's text goes out through the copy button, which clears it later)
            char tmp[sizeof t->buf];
            snprintf(tmp, sizeof tmp, "%.*s", hi - lo, t->buf + lo);
            SDL_SetClipboardText(tmp);
            if (key == SDLK_X) te_delete_sel(t);
        }
        return true;
    case SDLK_V:
        if (!ctrl) break;
        {
            char *c = SDL_GetClipboardText();
            if (c) {
                for (char *q = c; *q; q++)
                    if (*q == '\n' || *q == '\r' || *q == '\t') *q = ' ';
                te_insert(t, c);
                explicit_bzero(c, strlen(c));
                SDL_free(c);
            }
        }
        return true;
    default: break;
    }
    return false;
}

// --- state -----------------------------------------------------------------

typedef struct Item {
    char *path, *coll_path, *label;
    Attr *attrs;
    int nattrs;
    u64 created, modified;
    bool locked, sel, seen, pending; // pending: its properties are still on their way (streamed load)
    int coll; // index in A.colls (-1: none known)
    // derived from the schema and attributes (model.c)
    char account[128], where[192], kind[64];
    char app_key[96]; // the app that saved it ("brave", "Proton"): its own icon, when installed (appicons.c)
    int icon;
    char *hay; // lower-case search text
} Item;

typedef struct {
    char *path, *label;
    bool locked, is_default;
    u64 created, modified;
    int nitems;
} Coll;

enum { COL_NAME, COL_ACCOUNT, COL_WHERE, COL_KIND, COL_KEYRING, COL_MODIFIED, COL_CREATED, COL_COUNT };

static const struct { const char *key, *label; f32 def_w; bool right; } g_coldef[COL_COUNT] = {
    {"name", "Name", 0, false}, {"account", "Account", 180, false}, {"where", "Where", 200, false},
    {"kind", "Kind", 150, false}, {"keyring", "Keyring", 110, false}, {"modified", "Modified", 140, false},
    {"created", "Created", 140, false},
};

enum { FOCUS_LIST, FOCUS_SEARCH, FOCUS_SIDEBAR };
enum { DRAG_NONE, DRAG_SIDEBAR, DRAG_INSPECTOR, DRAG_COL, DRAG_SCROLL, DRAG_SHEET_SCROLL };
enum { TITLEBAR_AUTO, TITLEBAR_APP, TITLEBAR_SYSTEM };
enum { PURPOSE_REVEAL, PURPOSE_COPY, PURPOSE_EDIT };

// Sidebar rows: "All items", the keyrings, then the footer (service).
enum { SR_ALL, SR_HEADER, SR_COLL };
typedef struct { int kind, coll; Rect r; } SideRow;

#define MAX_DLG_BUTTONS 3
typedef struct {
    bool open, danger;
    char title[128], msg[600];
    const char *buttons[MAX_DLG_BUTTONS];
    int nbuttons, focus, def, cancel;
    Rect r, rb[MAX_DLG_BUTTONS];
    void (*done)(int button); // button index, or the cancel index for Esc
} Dialog;

#define MAX_MENU 32
typedef struct {
    char label[96], shortcut[48];
    int cmd, arg;
    bool sep, enabled, danger;
} MenuItem;

typedef struct {
    bool open;
    MenuItem items[MAX_MENU];
    int n, hover;
    Rect r;
} Menu;

#define PAL_MAX 200
typedef struct {
    int kind; // 0 item, 1 command
    Item *item;
    int cmd, score;
    u16 pos[32];
    int npos;
} PalItem;

static struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Cursor *cur_default, *cur_ew, *cur_text, *cur_hand;
    Font font, font_small, font_brand, font_mono, font_big;
    char *font_path, *mono_path;
    int W, H;
    f32 scale, dpi_scale, zoom;
    bool quit, redraw, csd, mouse_in, selftest, settings_dirty;
    const char *screenshot;
    int screenshot_frames;
    f32 mx, my;
    u64 last_frame, settings_saved, last_input;
    f32 frame_ms;

    // layout
    Rect r_brand, r_side, r_top, r_crumb, r_search, r_btn_new, r_btn_insp, r_btn_lock, r_wctl, r_header, r_list, r_track,
        r_insp, r_status, r_content, r_side_footer;
    SideRow side_rows[64];
    int nside_rows;
    f32 colx0[COL_COUNT], colx1[COL_COUNT];
    int ncols_vis, cols_vis[COL_COUNT];
    f32 row_h;

    // service + model
    int ss_state;
    char backend[64], ss_msg[256];
    bool encrypted, loaded, loading, prompt_open; // loaded: keyrings known; loading: item details still arriving
    f32 load_ms, view_ms;
    Coll *colls;
    int ncolls;
    Item **items;
    int nitems, cap_items;
    Item **map; // open addressing by path
    u32 map_cap;
    Item **view;
    int nview, cap_view, nsel;
    int cursor, anchor, cursor_hint;
    char select_path[512]; // select this item once it shows up (created / found in the palette)
    bool ensure_visible;
    int side_sel; // -1 all items, else a keyring
    char side_path[512];
    f64 scroll, scroll_target;

    // reveal / clipboard
    char *rev_item;  // the item whose secret is shown
    SecBuf rev;
    char rev_ctype[64];
    u32 rev_req;
    bool clip_owned;
    u64 clip_until;
    SecBuf clip;
    char clip_what[160];

    // search + focus
    TextEdit search;
    int focus;
    u64 search_typed;

    // overlays
    char toast[400];
    u64 toast_t;
    Dialog dlg;
    Menu menu;
    bool pal_open;
    TextEdit pal_te;
    PalItem pal[PAL_MAX];
    int pal_n, pal_sel, pal_top;
    Rect pal_r, pal_list;

    // dragging
    int drag, drag_col;
    f32 drag_off, drag_start;

    // settings
    char theme_name[64], ui_font[64];
    int font_size, title_bar, clip_secs, idle_lock_min, win_w, win_h;
    bool maximized, insp_on, insp_squeezed, alt_rows, smooth_scroll, show_overlay;
    f32 insp_scroll, insp_content_h; // details panel: scrolled by, and how tall its content is
    f32 side_w, insp_w, colw[COL_COUNT];
    bool col_on[COL_COUNT];
    int sort_col;
    bool sort_desc;
    bool fe_imported;
    bool idle_locked;
    char renderer[16];
} A;

static void toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(A.toast, sizeof A.toast, fmt, ap);
    va_end(ap);
    A.toast_t = now_ns();
    A.redraw = true;
}

static void reveal_clear(void);
static void on_secret(SsEvent *ev);
static void layout(void);

#include "model.c"
#include "appicons.c"

// --- layout ----------------------------------------------------------------

static bool titlebar_wanted(void);

static void layout(void) {
    f32 W = (f32)A.W, H = (f32)A.H;
    f32 top = roundf(S(46)), status = roundf(S(26));
    A.side_w = CLAMP(A.side_w, 180.f, 420.f);
    f32 sw = roundf(S(A.side_w));
    // small windows: the list keeps room first; the sidebar, then the details panel give way (the panel is left out
    // when even its narrowest doesn't fit)
    f32 list_min = roundf(S(280)), iw = A.insp_on ? roundf(MIN(S(MAX(A.insp_w, 260.f)), (W - sw) * 0.55f)) : 0;
    if (W - sw - iw < list_min) sw = MAX(roundf(S(170)), W - iw - list_min);
    if (iw > 0 && W - sw - iw < list_min) iw = MAX(roundf(S(250)), W - sw - list_min);
    A.insp_squeezed = iw > 0 && W - sw - iw < list_min;
    if (A.insp_squeezed) iw = 0;
    A.r_brand = (Rect){0, 0, sw, top};
    A.r_side = (Rect){0, top, sw, H - top - status};
    A.r_status = (Rect){0, H - status, W, status};
    A.r_top = (Rect){sw, 0, W - sw, top};
    f32 wctl = A.csd ? roundf(S(46)) * 3 : 0;
    A.r_wctl = (Rect){W - wctl, 0, wctl, top - 1}; // (as tall as the bar they sit on)
    f32 bs = roundf(S(30)), by = roundf((top - bs) * 0.5f), bx = W - wctl - S(10) - bs;
    A.r_btn_insp = (Rect){bx, by, bs, bs};
    bx -= bs + roundf(S(4));
    A.r_btn_lock = (Rect){bx, by, bs, bs};
    bx -= bs + roundf(S(4));
    A.r_btn_new = (Rect){bx, by, bs, bs};
    f32 sfw = roundf(MIN(S(380), MAX(S(160), (bx - sw) * 0.45f)));
    A.r_search = (Rect){roundf(bx - S(12) - sfw), roundf(by + S(1)), sfw, bs - roundf(S(2))};
    A.r_crumb = (Rect){sw + roundf(S(14)), 0, MAX(0.f, A.r_search.x - sw - S(28)), top};
    A.insp_w = CLAMP(A.insp_w, 260.f, 560.f);
    A.r_insp = (Rect){W - iw, top, iw, H - top - status};
    A.r_content = (Rect){sw, top, W - sw - iw, H - top - status};
    f32 hh = roundf(S(30));
    A.r_header = (Rect){A.r_content.x, A.r_content.y, A.r_content.w, hh};
    A.r_list = (Rect){A.r_content.x, A.r_content.y + hh, A.r_content.w, A.r_content.h - hh};
    f32 tw = roundf(S(10));
    A.r_track = (Rect){A.r_list.x + A.r_list.w - tw, A.r_list.y, tw, A.r_list.h};
    A.row_h = roundf(S(32));
    A.r_side_footer = (Rect){0, A.r_side.y + A.r_side.h - roundf(S(52)), sw, roundf(S(52))};

    // columns: name takes what the others leave (at least 160)
    A.ncols_vis = 0;
    for (int c = 0; c < COL_COUNT; c++)
        if (c == COL_NAME || A.col_on[c]) A.cols_vis[A.ncols_vis++] = c;
    f32 x0 = A.r_list.x + roundf(S(8)), x1 = A.r_track.x - roundf(S(4));
    f32 others = 0, avail = x1 - x0, namemin = MAX(roundf(S(160)), roundf(avail * 0.34f));
    for (int i = 1; i < A.ncols_vis; i++) others += roundf(S(A.colw[A.cols_vis[i]]));
    while (A.ncols_vis > 1 && others * 0.7f > avail - namemin) { // too narrow: the last columns go before the rest squeeze
        others -= roundf(S(A.colw[A.cols_vis[A.ncols_vis - 1]]));
        A.ncols_vis--;
    }
    f32 k = others > avail - namemin && others > 0 ? MAX(0.f, avail - namemin) / others : 1.f; // narrow: all shrink
    f32 namew = MAX(namemin, avail - others * k);
    f32 x = x0;
    for (int i = 0; i < A.ncols_vis; i++) {
        int c = A.cols_vis[i];
        f32 w = c == COL_NAME ? namew : roundf(S(A.colw[c]) * k);
        A.colx0[c] = x;
        A.colx1[c] = x + w;
        x += w;
    }

    // sidebar rows
    A.nside_rows = 0;
    f32 y = A.r_side.y + roundf(S(8)), rh = roundf(S(32)), rx = roundf(S(8)), rw = sw - roundf(S(16));
    A.side_rows[A.nside_rows++] = (SideRow){SR_ALL, -1, {rx, y, rw, rh}};
    y += rh + roundf(S(10));
    A.side_rows[A.nside_rows++] = (SideRow){SR_HEADER, -1, {rx, y, rw, roundf(S(26))}};
    y += roundf(S(26));
    for (int i = 0; i < A.ncolls && A.nside_rows < (int)ARRAY_COUNT(A.side_rows); i++) {
        A.side_rows[A.nside_rows++] = (SideRow){SR_COLL, i, {rx, y, rw, rh}};
        y += rh + roundf(S(2));
    }
}

static f64 max_scroll(void) { return MAX(0.0, (f64)A.nview * A.row_h - A.r_list.h + S(8)); }

static void scroll_to_cursor(void) {
    if (A.cursor < 0 || A.r_list.h <= 0) return;
    f64 y = (f64)A.cursor * A.row_h, top = A.scroll_target, bot = top + A.r_list.h - A.row_h - S(4);
    if (y < top) A.scroll_target = y;
    else if (y > bot) A.scroll_target = y - (A.r_list.h - A.row_h - S(4));
    A.scroll_target = CLAMP(A.scroll_target, 0.0, max_scroll());
    if (!A.smooth_scroll) A.scroll = A.scroll_target;
}

static int row_at(f32 y) {
    if (y < A.r_list.y || y >= A.r_list.y + A.r_list.h) return -1;
    int i = (int)floor((A.scroll + (f64)(y - A.r_list.y)) / A.row_h);
    return i >= 0 && i < A.nview ? i : -1;
}

// --- own title bar ---------------------------------------------------------
// The same look on every desktop: the window is borderless and draws its own title bar - the buttons at the right
// end of the top bar, its empty parts and the logo move the window, the edges resize it (SDL hit test). Tiling
// compositors get it too: there the buttons are the only visible way to close the window.

static bool titlebar_wanted(void) {
    int m = A.title_bar;
    const char *e = getenv("KM_TITLE_BAR"); // (tests, screenshots)
    if (e && !strcmp(e, "app")) m = TITLEBAR_APP;
    else if (e && !strcmp(e, "system")) m = TITLEBAR_SYSTEM;
    if (m != TITLEBAR_AUTO) return m == TITLEBAR_APP;
    const char *drv = SDL_GetCurrentVideoDriver();
    return !drv || strcmp(drv, "offscreen"); // (not in self-tests and screenshots: KM_TITLE_BAR=app shows it there)
}

// 0 minimise, 1 maximise / restore, 2 close
static Rect wctl_button(int i) {
    f32 bw = roundf(A.r_wctl.w / 3);
    return (Rect){A.r_wctl.x + bw * (f32)i, A.r_wctl.y, i == 2 ? A.r_wctl.w - 2 * bw : bw, A.r_wctl.h};
}

static bool window_maximized(void) {
    return A.win && (SDL_GetWindowFlags(A.win) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN));
}

// The parts of the title bar that move the window: the logo, and the top bar outside its controls.
static bool titlebar_drag_at(f32 x, f32 y) {
    if (!A.csd || in_rect(A.r_wctl, x, y)) return false;
    if (in_rect(A.r_brand, x, y)) return true;
    if (!in_rect(A.r_top, x, y)) return false;
    return !in_rect(A.r_search, x, y) && !in_rect(A.r_btn_new, x, y) && !in_rect(A.r_btn_insp, x, y) && !in_rect(A.r_btn_lock, x, y);
}

static SDL_HitTestResult window_hit_test(SDL_Window *w, const SDL_Point *pt, void *ud) {
    (void)ud;
    if (!A.csd || A.drag != DRAG_NONE) return SDL_HITTEST_NORMAL;
    f32 d = SDL_GetWindowPixelDensity(w);
    f32 x = (f32)pt->x * d, y = (f32)pt->y * d, W = (f32)A.W, H = (f32)A.H;
    if (!window_maximized()) {
        f32 e = MAX(3.f, roundf(S(5))), c = roundf(S(14)); // edge, and the corners' longer grab along it
        bool l = x < e, r = x >= W - e, t = y < e, b = y >= H - e;
        if ((t && x < c) || (l && y < c)) return SDL_HITTEST_RESIZE_TOPLEFT;
        if ((t && x >= W - c) || (r && y < c)) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if ((b && x < c) || (l && y >= H - c)) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if ((b && x >= W - c) || (r && y >= H - c)) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (t) return SDL_HITTEST_RESIZE_TOP;
        if (b) return SDL_HITTEST_RESIZE_BOTTOM;
        if (l) return SDL_HITTEST_RESIZE_LEFT;
        if (r) return SDL_HITTEST_RESIZE_RIGHT;
    }
    bool overlay = A.dlg.open || A.menu.open || A.pal_open;
    return !overlay && titlebar_drag_at(x, y) ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

// Switch between our title bar and the system's (startup, and when the setting changes).
static void titlebar_apply(void) {
    bool csd = titlebar_wanted();
    if (A.win && !A.selftest) {
        SDL_SetWindowBordered(A.win, !csd);
        SDL_SetWindowHitTest(A.win, csd ? window_hit_test : NULL, NULL);
    }
    A.csd = csd;
    layout();
    A.redraw = true;
}

static void window_toggle_maximized(void) {
    if (A.selftest) return;
    if (window_maximized()) SDL_RestoreWindow(A.win);
    else SDL_MaximizeWindow(A.win);
}

// A press on the title bar: the window buttons; right-click on the empty bar, the window menu. True if used.
static bool titlebar_mouse_down(SDL_MouseButtonEvent *b, f32 x, f32 y) {
    if (!A.csd) return false;
    if (in_rect(A.r_wctl, x, y)) {
        if (b->button != SDL_BUTTON_LEFT) return true;
        for (int i = 0; i < 3; i++) {
            if (!in_rect(wctl_button(i), x, y) || A.selftest) continue;
            if (i == 0) SDL_MinimizeWindow(A.win);
            else if (i == 1) window_toggle_maximized();
            else A.quit = true;
        }
        return true;
    }
    if (!titlebar_drag_at(x, y)) return false;
    // (left presses here normally start a move in the compositor and never reach us)
    if (b->button == SDL_BUTTON_LEFT && b->clicks >= 2) { window_toggle_maximized(); return true; }
    if (b->button == SDL_BUTTON_RIGHT) {
        f32 d = SDL_GetWindowPixelDensity(A.win);
        if (!A.selftest) SDL_ShowWindowSystemMenu(A.win, (int)(x / d), (int)(y / d));
        return true;
    }
    return b->button == SDL_BUTTON_LEFT;
}

// --- secrets: reveal + clipboard -----------------------------------------------------

#include "clip.c"

static void reveal_clear(void) {
    sec_free(&A.rev);
    free(A.rev_item);
    A.rev_item = NULL;
    A.rev_req = 0;
    A.rev_ctype[0] = 0;
    A.redraw = true;
}

static bool revealed(const Item *it) { return it && A.rev_item && !strcmp(A.rev_item, it->path) && A.rev.p; }

static void reveal_toggle(Item *it) {
    if (!it) return;
    if (revealed(it) || (A.rev_item && !strcmp(A.rev_item, it->path))) { reveal_clear(); return; }
    reveal_clear();
    A.rev_item = strdup(it->path);
    A.rev_req = ss_get_secret(it->path, PURPOSE_REVEAL);
}

static void copy_secret(Item *it) {
    if (!it) return;
    ss_get_secret(it->path, PURPOSE_COPY);
}

static void sheet_secret_loaded(SsEvent *ev); // sheets.c: the editor asked for the secret it edits

static void on_secret(SsEvent *ev) {
    Item *it = item_by_path(ev->path);
    if (!ev->ok) {
        if (ev->purpose == PURPOSE_REVEAL && ev->req == A.rev_req) reveal_clear();
        toast("Couldn't read the secret%s%s", ev->msg[0] ? ": " : "", ev->msg);
        return;
    }
    if (ev->purpose == PURPOSE_REVEAL) {
        if (ev->req != A.rev_req) return; // (an older request: something else is shown now)
        sec_move(&A.rev, &ev->secret);
        snprintf(A.rev_ctype, sizeof A.rev_ctype, "%s", ev->ctype ? ev->ctype : "");
    } else if (ev->purpose == PURPOSE_COPY) {
        clip_secret(&ev->secret, it ? it->label : "secret");
    } else if (ev->purpose == PURPOSE_EDIT) {
        sheet_secret_loaded(ev);
    }
}

// --- requests the UI waits for ---------------------------------------------------------

typedef struct { u32 id; int what; char arg[512]; } Pending;
enum { PW_NONE, PW_CREATE_ITEM, PW_CREATE_COLL, PW_SAVE, PW_DELETE, PW_TRANSFER, PW_GENERIC, PW_TEST };
static Pending g_pending[32];

static void pending_add(u32 id, int what, const char *arg) {
    for (int i = 0; i < (int)ARRAY_COUNT(g_pending); i++)
        if (!g_pending[i].id) {
            g_pending[i] = (Pending){id, what, ""};
            snprintf(g_pending[i].arg, sizeof g_pending[i].arg, "%s", arg ? arg : "");
            return;
        }
}

static bool pending_any(void) {
    for (int i = 0; i < (int)ARRAY_COUNT(g_pending); i++)
        if (g_pending[i].id) return true;
    return false;
}

static u32 g_test_done_req; // self-tests: the last finished request (selftest.c waits for it)
static bool g_test_done_ok;
static char g_test_done_path[512];

static void on_request_done(SsEvent *ev) {
    g_test_done_req = ev->req;
    g_test_done_ok = ev->ok;
    snprintf(g_test_done_path, sizeof g_test_done_path, "%s", ev->path ? ev->path : "");
    Pending *p = NULL;
    for (int i = 0; i < (int)ARRAY_COUNT(g_pending); i++)
        if (g_pending[i].id == ev->req) p = &g_pending[i];
    if (!ev->ok && ev->msg[0] && strcmp(ev->msg, "Cancelled")) toast("%s", ev->msg);
    if (!p) return;
    if (ev->ok && p->what == PW_CREATE_ITEM && ev->path) {
        snprintf(A.select_path, sizeof A.select_path, "%s", ev->path);
        Item *it = item_by_path(ev->path);
        if (it && A.side_sel >= 0 && it->coll != A.side_sel) side_select(it->coll);
        view_rebuild();
    } else if (ev->ok && p->what == PW_CREATE_COLL && ev->path) {
        side_select(coll_by_path(ev->path));
    } else if (ev->ok && p->what == PW_TRANSFER) {
        toast("%s", p->arg);
    }
    p->id = 0;
}

// --- actions ------------------------------------------------------------------

static void dialog_open(const char *title, const char *msg, const char **buttons, int n, int def, int cancel, bool danger,
                        void (*done)(int)) {
    Dialog *d = &A.dlg;
    memset(d, 0, sizeof *d);
    d->open = true;
    snprintf(d->title, sizeof d->title, "%s", title);
    snprintf(d->msg, sizeof d->msg, "%s", msg);
    for (int i = 0; i < n && i < MAX_DLG_BUTTONS; i++) d->buttons[i] = buttons[i];
    d->nbuttons = MIN(n, MAX_DLG_BUTTONS);
    d->def = d->focus = def;
    d->cancel = cancel;
    d->danger = danger;
    d->done = done;
    A.redraw = true;
}

static void dialog_close(int button) {
    Dialog *d = &A.dlg;
    if (!d->open) return;
    d->open = false;
    if (d->done) d->done(button);
    A.redraw = true;
}

static char **g_del_paths; // what the confirm dialog is about
static int g_del_n;
static char g_del_coll[512];

static void del_items_done(int b) {
    if (b == 1 && g_del_n) {
        SsReq *r = ss_req(RQ_DELETE);
        r->paths = g_del_paths;
        r->npaths = g_del_n;
        g_del_paths = NULL;
        g_del_n = 0;
        pending_add(ss_submit(r), PW_DELETE, NULL);
    }
    strv_free(g_del_paths, g_del_n);
    g_del_paths = NULL;
    g_del_n = 0;
}

static void action_delete_items(void) {
    Item **v;
    int n = selected_items(&v);
    if (!n) { free(v); return; }
    strv_free(g_del_paths, g_del_n);
    g_del_paths = xmalloc((size_t)n * sizeof *g_del_paths);
    for (int i = 0; i < n; i++) g_del_paths[i] = strdup(v[i]->path);
    g_del_n = n;
    char title[160], msg[400];
    if (n == 1) {
        snprintf(title, sizeof title, "Delete \xE2\x80\x9C%.100s\xE2\x80\x9D?", v[0]->label[0] ? v[0]->label : "this item");
        snprintf(msg, sizeof msg, "It's removed from the \xE2\x80\x9C%.80s\xE2\x80\x9D keyring. Apps that saved it will ask for it again.",
                 coll_label(v[0]->coll));
    } else {
        snprintf(title, sizeof title, "Delete %d items?", n);
        snprintf(msg, sizeof msg, "They're removed from their keyrings. Apps that saved them will ask for them again.");
    }
    free(v);
    static const char *b[] = {"Cancel", "Delete"};
    dialog_open(title, msg, b, 2, 1, 0, true, del_items_done);
}

static void del_coll_done(int b) {
    if (b == 1 && g_del_coll[0]) pending_add(ss_simple(RQ_DELETE_COLL, g_del_coll, NULL), PW_GENERIC, NULL);
}

static void action_delete_coll(int c) {
    if (c < 0 || c >= A.ncolls) return;
    snprintf(g_del_coll, sizeof g_del_coll, "%s", A.colls[c].path);
    char title[160], msg[400];
    snprintf(title, sizeof title, "Delete the keyring \xE2\x80\x9C%.100s\xE2\x80\x9D?", coll_label(c));
    snprintf(msg, sizeof msg, "Its %d item%s %s deleted with it. This can't be undone.", A.colls[c].nitems,
             A.colls[c].nitems == 1 ? "" : "s", A.colls[c].nitems == 1 ? "is" : "are");
    static const char *b[] = {"Cancel", "Delete keyring"};
    dialog_open(title, msg, b, 2, 1, 0, true, del_coll_done);
}

static void action_lock_coll(int c, bool lock) {
    if (c < 0 || c >= A.ncolls) return;
    char *p = A.colls[c].path;
    pending_add(ss_lock(&p, 1, lock), PW_GENERIC, NULL);
}

// Lock every keyring that's open (Ctrl+L, idle timeout).
static int action_lock_all(void) {
    char **v = xmalloc((size_t)MAX(A.ncolls, 1) * sizeof *v);
    int n = 0;
    for (int i = 0; i < A.ncolls; i++)
        if (!A.colls[i].locked && strcmp(path_basename(A.colls[i].path), "session")) v[n++] = A.colls[i].path;
    if (n) pending_add(ss_lock(v, n, true), PW_GENERIC, NULL);
    free(v);
    reveal_clear();
    return n;
}

static void action_unlock_item(Item *it) {
    if (!it) return;
    char *p = it->coll_path;
    pending_add(ss_lock(&p, 1, false), PW_GENERIC, NULL);
}

static void action_transfer(int target, bool move) {
    Item **v;
    int n = selected_items(&v);
    if (!n || target < 0 || target >= A.ncolls) { free(v); return; }
    SsReq *r = ss_req(RQ_TRANSFER);
    r->paths = xmalloc((size_t)n * sizeof *r->paths);
    for (int i = 0; i < n; i++) r->paths[i] = strdup(v[i]->path);
    r->npaths = n;
    r->target = strdup(A.colls[target].path);
    r->move = move;
    char msg[256];
    snprintf(msg, sizeof msg, "%s %d item%s to \xE2\x80\x9C%.100s\xE2\x80\x9D", move ? "Moved" : "Copied", n, n == 1 ? "" : "s", coll_label(target));
    pending_add(ss_submit(r), PW_TRANSFER, msg);
    free(v);
}

static void copy_text(const char *what, const char *text) {
    if (!text || !text[0]) { toast("No %s to copy", what); return; }
    SDL_SetClipboardText(text);
    toast("Copied the %s", what);
}

static void sheet_open_item(Item *it, int coll); // sheets.c
static void sheet_open_name(int coll);
static void sheet_open_options(int page);
static bool sheet_open(void);
static void sheet_close(void);

// --- commands --------------------------------------------------------------

enum {
    CMD_NONE, CMD_COPY_SECRET, CMD_COPY_ACCOUNT, CMD_COPY_WHERE, CMD_REVEAL, CMD_EDIT, CMD_NEW_ITEM, CMD_DELETE,
    CMD_NEW_KEYRING, CMD_RENAME_KEYRING, CMD_DELETE_KEYRING, CMD_LOCK_KEYRING, CMD_UNLOCK_KEYRING, CMD_DEFAULT_KEYRING,
    CMD_LOCK_ALL, CMD_FIND, CMD_PALETTE, CMD_COMMANDS, CMD_INSPECTOR, CMD_OPTIONS, CMD_KEYS, CMD_RELOAD, CMD_SELECT_ALL,
    CMD_PREV_KEYRING, CMD_NEXT_KEYRING, CMD_ALL_ITEMS, CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ZOOM_RESET, CMD_QUIT,
    CMD_COPY_TO, CMD_MOVE_TO, CMD_SORT, CMD_TOGGLE_COL, CMD_OVERLAY, CMD_ENABLE_KWALLET, CMD_COUNT
};

typedef struct { int cmd; const char *title; bool palette; } CmdDef;

static const CmdDef g_cmds[] = {
    {CMD_COPY_SECRET, "Copy secret", true}, {CMD_COPY_ACCOUNT, "Copy account name", true},
    {CMD_COPY_WHERE, "Copy where it's used", true}, {CMD_REVEAL, "Show / hide secret", true},
    {CMD_EDIT, "Edit item", true}, {CMD_NEW_ITEM, "New item", true}, {CMD_DELETE, "Delete item", true},
    {CMD_NEW_KEYRING, "New keyring", true}, {CMD_RENAME_KEYRING, "Rename keyring", true},
    {CMD_DELETE_KEYRING, "Delete keyring", true}, {CMD_LOCK_KEYRING, "Lock keyring", true},
    {CMD_UNLOCK_KEYRING, "Unlock keyring", true}, {CMD_DEFAULT_KEYRING, "Make keyring the default", true},
    {CMD_LOCK_ALL, "Lock all keyrings", true}, {CMD_FIND, "Find", true}, {CMD_PALETTE, "Go to item", true},
    {CMD_COMMANDS, "Commands", false}, {CMD_INSPECTOR, "Show / hide details panel", true}, {CMD_OPTIONS, "Options", true},
    {CMD_KEYS, "Keyboard shortcuts", true}, {CMD_RELOAD, "Reload from the keyring service", true},
    {CMD_SELECT_ALL, "Select all", true}, {CMD_PREV_KEYRING, "Previous keyring", true}, {CMD_NEXT_KEYRING, "Next keyring", true},
    {CMD_ALL_ITEMS, "All items", true}, {CMD_ZOOM_IN, "Zoom in", true}, {CMD_ZOOM_OUT, "Zoom out", true},
    {CMD_ZOOM_RESET, "Reset zoom", true}, {CMD_QUIT, "Quit", true}, {CMD_OVERLAY, "Show frame times", true},
};

static const char *cmd_title(int cmd) {
    for (size_t i = 0; i < ARRAY_COUNT(g_cmds); i++)
        if (g_cmds[i].cmd == cmd) return g_cmds[i].title;
    return "";
}

#include "keys.c"

static void palette_open(bool commands);
static void zoom_set(f32 z);

static int cur_coll(void) {
    Item *it = cursor_item();
    if (A.side_sel >= 0) return A.side_sel;
    return it ? it->coll : -1;
}

// Where new items go: the selected keyring, else the default one, else the first unlocked one.
static int target_coll(void) {
    if (A.side_sel >= 0 && A.side_sel < A.ncolls) return A.side_sel;
    for (int i = 0; i < A.ncolls; i++)
        if (A.colls[i].is_default) return i;
    for (int i = 0; i < A.ncolls; i++)
        if (!A.colls[i].locked) return i;
    return A.ncolls ? 0 : -1;
}

static void kwallet_enable_done(int b) {
    if (b == 1) pending_add(ss_submit(ss_req(RQ_ENABLE_KWALLET)), PW_GENERIC, NULL);
}

static void run_command(int cmd, int arg) {
    Item *it = cursor_item();
    A.redraw = true;
    switch (cmd) {
    case CMD_COPY_SECRET: copy_secret(it); break;
    case CMD_COPY_ACCOUNT: if (it) copy_text("account name", it->account); break;
    case CMD_COPY_WHERE: if (it) copy_text("address", it->where); break;
    case CMD_REVEAL: reveal_toggle(it); if (it && !A.insp_on) { A.insp_on = true; layout(); } break;
    case CMD_EDIT: if (it) sheet_open_item(it, it->coll); break;
    case CMD_NEW_ITEM: sheet_open_item(NULL, target_coll()); break;
    case CMD_DELETE: action_delete_items(); break;
    case CMD_NEW_KEYRING: sheet_open_name(-1); break;
    case CMD_RENAME_KEYRING: if (arg >= 0 || cur_coll() >= 0) sheet_open_name(arg >= 0 ? arg : cur_coll()); break;
    case CMD_DELETE_KEYRING: action_delete_coll(arg >= 0 ? arg : cur_coll()); break;
    case CMD_LOCK_KEYRING: action_lock_coll(arg >= 0 ? arg : cur_coll(), true); break;
    case CMD_UNLOCK_KEYRING: action_lock_coll(arg >= 0 ? arg : cur_coll(), false); break;
    case CMD_DEFAULT_KEYRING: {
        int c = arg >= 0 ? arg : cur_coll();
        if (c >= 0) pending_add(ss_simple(RQ_SET_DEFAULT, A.colls[c].path, NULL), PW_GENERIC, NULL);
        break;
    }
    case CMD_LOCK_ALL: {
        int n = action_lock_all();
        toast(n ? "Locked %d keyring%s" : "All keyrings are locked", n, n == 1 ? "" : "s");
        break;
    }
    case CMD_FIND: A.focus = FOCUS_SEARCH; A.search.anchor = 0; A.search.caret = (int)strlen(A.search.buf); break;
    case CMD_PALETTE: palette_open(false); break;
    case CMD_COMMANDS: palette_open(true); break;
    case CMD_INSPECTOR:
        A.insp_on = !A.insp_on;
        A.settings_dirty = true;
        layout();
        if (A.insp_on && A.insp_squeezed) toast("The window is too narrow for the details panel");
        break;
    case CMD_OPTIONS: sheet_open_options(0); break;
    case CMD_KEYS: sheet_open_options(1); break;
    case CMD_RELOAD: ss_submit(ss_req(RQ_RELOAD)); break;
    case CMD_SELECT_ALL:
        for (int i = 0; i < A.nview; i++) A.view[i]->sel = true;
        sel_count_update();
        break;
    case CMD_PREV_KEYRING: side_select(A.side_sel <= -1 ? A.ncolls - 1 : A.side_sel - 1); break;
    case CMD_NEXT_KEYRING: side_select(A.side_sel >= A.ncolls - 1 ? -1 : A.side_sel + 1); break;
    case CMD_ALL_ITEMS: side_select(-1); break;
    case CMD_ZOOM_IN: zoom_set(A.zoom + 0.1f); break;
    case CMD_ZOOM_OUT: zoom_set(A.zoom - 0.1f); break;
    case CMD_ZOOM_RESET: zoom_set(1.f); break;
    case CMD_QUIT: A.quit = true; break;
    case CMD_COPY_TO: action_transfer(arg, false); break;
    case CMD_MOVE_TO: action_transfer(arg, true); break;
    case CMD_SORT:
        if (A.sort_col == arg) A.sort_desc = !A.sort_desc;
        else { A.sort_col = arg; A.sort_desc = arg == COL_MODIFIED || arg == COL_CREATED; }
        A.settings_dirty = true;
        view_rebuild();
        break;
    case CMD_TOGGLE_COL:
        if (arg > COL_NAME && arg < COL_COUNT) { A.col_on[arg] = !A.col_on[arg]; A.settings_dirty = true; layout(); }
        break;
    case CMD_OVERLAY: A.show_overlay = !A.show_overlay; break;
    case CMD_ENABLE_KWALLET: {
        static const char *b[] = {"Cancel", "Turn it on"};
        dialog_open("Turn on KWallet's Secret Service support?",
                    "KWallet then also answers the standard API that this app (and libsecret apps such as browsers, Git or "
                    "NetworkManager tools) use. This sets apiEnabled in kwalletrc and restarts KWallet; your wallets stay as they are.",
                    b, 2, 1, 0, false, kwallet_enable_done);
        break;
    }
    }
}

// --- menus -------------------------------------------------------------------

static void menu_clear(void) {
    A.menu.n = 0;
    A.menu.hover = -1;
}

static void menu_add(const char *label, int cmd, int arg, bool enabled) {
    if (A.menu.n >= MAX_MENU) return;
    MenuItem *m = &A.menu.items[A.menu.n++];
    memset(m, 0, sizeof *m);
    snprintf(m->label, sizeof m->label, "%s", label);
    m->cmd = cmd;
    m->arg = arg;
    m->enabled = enabled;
    m->danger = cmd == CMD_DELETE || cmd == CMD_DELETE_KEYRING;
    key_label_for(cmd, m->shortcut, sizeof m->shortcut);
}

static void menu_sep(void) {
    if (A.menu.n && A.menu.n < MAX_MENU && !A.menu.items[A.menu.n - 1].sep) {
        memset(&A.menu.items[A.menu.n], 0, sizeof A.menu.items[0]);
        A.menu.items[A.menu.n++].sep = true;
    }
}

static f32 menu_item_h(const MenuItem *m) { return m->sep ? roundf(S(9)) : roundf(S(30)); }

static void menu_show(f32 x, f32 y) {
    Menu *m = &A.menu;
    if (!m->n) return;
    Font *f = &A.font, *fs = &A.font_small;
    f32 w = S(200), h = roundf(S(10));
    for (int i = 0; i < m->n; i++) {
        h += menu_item_h(&m->items[i]);
        f32 lw = text_width(f, m->items[i].label, (int)strlen(m->items[i].label)) + S(40) +
                 text_width(fs, m->items[i].shortcut, (int)strlen(m->items[i].shortcut)) + S(24);
        w = MAX(w, lw);
    }
    w = roundf(MIN(w, S(440)));
    x = MIN(x, A.W - w - S(4));
    y = MIN(y, A.H - h - S(4));
    m->r = (Rect){roundf(MAX(x, S(4))), roundf(MAX(y, S(4))), w, h};
    m->open = true;
    m->hover = -1;
    A.redraw = true;
}

static int menu_at(f32 x, f32 y) {
    Menu *m = &A.menu;
    if (!in_rect(m->r, x, y)) return -1;
    f32 yy = m->r.y + roundf(S(5));
    for (int i = 0; i < m->n; i++) {
        f32 h = menu_item_h(&m->items[i]);
        if (y >= yy && y < yy + h) return m->items[i].sep || !m->items[i].enabled ? -1 : i;
        yy += h;
    }
    return -1;
}

static void menu_pick(int i) {
    Menu *m = &A.menu;
    m->open = false;
    if (i >= 0 && i < m->n && m->items[i].enabled && !m->items[i].sep) run_command(m->items[i].cmd, m->items[i].arg);
    A.redraw = true;
}

static void menu_items(f32 x, f32 y) {
    Item *it = cursor_item();
    menu_clear();
    bool one = it != NULL, unlocked = it && !it->locked;
    menu_add("Copy secret", CMD_COPY_SECRET, -1, one);
    menu_add("Copy account name", CMD_COPY_ACCOUNT, -1, one && it->account[0]);
    menu_add(revealed(it) ? "Hide secret" : "Show secret", CMD_REVEAL, -1, one);
    menu_sep();
    menu_add("Edit\xE2\x80\xA6", CMD_EDIT, -1, unlocked);
    menu_add("New item\xE2\x80\xA6", CMD_NEW_ITEM, -1, true);
    int others = 0;
    for (int c = 0; c < A.ncolls; c++) others += it && c != it->coll && !A.colls[c].locked;
    if (others && others <= 6) {
        menu_sep();
        char lab[128];
        for (int c = 0; c < A.ncolls; c++) {
            if (!it || c == it->coll || A.colls[c].locked) continue;
            snprintf(lab, sizeof lab, "Copy to \xE2\x80\x9C%.60s\xE2\x80\x9D", coll_label(c));
            menu_add(lab, CMD_COPY_TO, c, unlocked);
            snprintf(lab, sizeof lab, "Move to \xE2\x80\x9C%.60s\xE2\x80\x9D", coll_label(c));
            menu_add(lab, CMD_MOVE_TO, c, unlocked);
        }
    }
    menu_sep();
    menu_add(A.nsel > 1 ? "Delete items\xE2\x80\xA6" : "Delete\xE2\x80\xA6", CMD_DELETE, -1, one);
    menu_show(x, y);
}

static void menu_coll(int c, f32 x, f32 y) {
    menu_clear();
    if (c < 0 || c >= A.ncolls) {
        menu_add("New keyring\xE2\x80\xA6", CMD_NEW_KEYRING, -1, true);
        menu_add("Lock all keyrings", CMD_LOCK_ALL, -1, true);
        menu_show(x, y);
        return;
    }
    Coll *k = &A.colls[c];
    menu_add(k->locked ? "Unlock" : "Lock", k->locked ? CMD_UNLOCK_KEYRING : CMD_LOCK_KEYRING, c, true);
    menu_add("Make default", CMD_DEFAULT_KEYRING, c, !k->is_default);
    menu_add("Rename\xE2\x80\xA6", CMD_RENAME_KEYRING, c, true);
    menu_sep();
    menu_add("New keyring\xE2\x80\xA6", CMD_NEW_KEYRING, -1, true);
    menu_sep();
    menu_add("Delete keyring\xE2\x80\xA6", CMD_DELETE_KEYRING, c, true);
    menu_show(x, y);
}

static void menu_header(f32 x, f32 y) {
    menu_clear();
    for (int c = COL_ACCOUNT; c < COL_COUNT; c++) {
        menu_add(g_coldef[c].label, CMD_TOGGLE_COL, c, true);
        A.menu.items[A.menu.n - 1].danger = false;
        snprintf(A.menu.items[A.menu.n - 1].shortcut, sizeof A.menu.items[0].shortcut, "%s", A.col_on[c] ? "\xE2\x9C\x93" : "");
    }
    menu_show(x, y);
}

// --- command palette (Ctrl+P: go to any item, ">": commands) --------------------------------------

static void palette_update(void) {
    const char *q = A.pal_te.buf;
    bool cmds = q[0] == '>';
    if (cmds) q++;
    while (*q == ' ') q++;
    int qn = (int)strlen(q);
    A.pal_n = 0;
    if (cmds) {
        for (size_t i = 0; i < ARRAY_COUNT(g_cmds) && A.pal_n < PAL_MAX; i++) {
            if (!g_cmds[i].palette) continue;
            PalItem *p = &A.pal[A.pal_n];
            memset(p, 0, sizeof *p);
            int s = fuzzy_match(g_cmds[i].title, (int)strlen(g_cmds[i].title), q, qn, p->pos);
            if (s < 0) continue;
            p->kind = 1;
            p->cmd = g_cmds[i].cmd;
            p->score = s;
            p->npos = MIN(qn, 32);
            A.pal_n++;
        }
    } else {
        for (int i = 0; i < A.nitems; i++) {
            Item *it = A.items[i];
            PalItem tmp;
            memset(&tmp, 0, sizeof tmp);
            int s = fuzzy_match(it->label, (int)strlen(it->label), q, qn, tmp.pos);
            tmp.npos = s >= 0 ? MIN(qn, 32) : 0;
            if (s < 0 && qn) { // not in the name: the account or where it's used (no highlight)
                int s2 = MAX(fuzzy_match(it->account, (int)strlen(it->account), q, qn, NULL),
                             fuzzy_match(it->where, (int)strlen(it->where), q, qn, NULL));
                if (s2 < 0) continue;
                s = s2 / 2;
            }
            tmp.kind = 0;
            tmp.item = it;
            tmp.score = qn ? s : (int)(it->modified >> 8); // empty query: recently changed first
            if (A.pal_n < PAL_MAX) A.pal[A.pal_n++] = tmp;
            else { // keep the best PAL_MAX
                int worst = 0;
                for (int k = 1; k < A.pal_n; k++)
                    if (A.pal[k].score < A.pal[worst].score) worst = k;
                if (tmp.score > A.pal[worst].score) A.pal[worst] = tmp;
            }
        }
    }
    for (int i = 1; i < A.pal_n; i++) // insertion sort by score (at most PAL_MAX)
        for (int j = i; j > 0 && A.pal[j - 1].score < A.pal[j].score; j--) {
            PalItem t = A.pal[j];
            A.pal[j] = A.pal[j - 1];
            A.pal[j - 1] = t;
        }
    A.pal_sel = 0;
    A.pal_top = 0;
    A.redraw = true;
}

static void palette_open(bool commands) {
    A.pal_open = true;
    A.menu.open = false;
    te_set(&A.pal_te, commands ? ">" : "", 1, 1);
    palette_update();
}

static void palette_accept(int i) {
    A.pal_open = false;
    if (i < 0 || i >= A.pal_n) return;
    PalItem *p = &A.pal[i];
    if (p->kind == 1) { run_command(p->cmd, -1); return; }
    Item *it = p->item;
    if (A.side_sel >= 0 && it->coll != A.side_sel) side_select(it->coll);
    if (A.search.buf[0] && !item_matches(it, A.search.buf)) te_set(&A.search, "", 0, 0);
    snprintf(A.select_path, sizeof A.select_path, "%s", it->path);
    A.cursor = -1;
    view_rebuild();
    A.focus = FOCUS_LIST;
}

static void zoom_set(f32 z) {
    z = CLAMP(roundf(z * 10.f) / 10.f, 0.6f, 2.5f);
    if (fabsf(z - A.zoom) < 0.01f) return;
    A.zoom = z;
    A.scale = A.dpi_scale * A.zoom;
    font_set_px(&A.font, (int)roundf((f32)A.font_size * A.scale));
    font_set_px(&A.font_small, (int)roundf((f32)(A.font_size - 1) * A.scale));
    font_set_px(&A.font_brand, (int)roundf((f32)(A.font_size + 3) * A.scale));
    font_set_px(&A.font_big, (int)roundf((f32)(A.font_size + 7) * A.scale));
    if (A.mono_path) font_set_px(&A.font_mono, (int)roundf((f32)A.font_size * A.scale));
    atlas_reset();
    A.settings_dirty = true;
    layout();
    toast("Zoom %d%%", (int)roundf(A.zoom * 100));
}

#include "settings.c"
#include "sheets.c"
#include "input.c"
#include "render.c"

// --- idle lock + clipboard timer ----------------------------------------------------------

static void timers_tick(void) {
    u64 now = now_ns();
    clip_tick(now);
    if (A.idle_lock_min > 0 && A.loaded && !A.idle_locked && A.last_input &&
        now - A.last_input > (u64)A.idle_lock_min * 60000000000ull) {
        A.idle_locked = true;
        if (action_lock_all()) toast("Locked the keyrings after %d minute%s without use", A.idle_lock_min, A.idle_lock_min == 1 ? "" : "s");
    }
}

// How long the loop may sleep before a timer needs it (ms; -1 = until an event).
static int timers_wait_ms(void) {
    u64 now = now_ns();
    i64 best = -1;
    if (A.clip_owned && A.clip_until) best = (i64)((A.clip_until > now ? A.clip_until - now : 0) / 1000000) + 1;
    if (A.clip_owned && A.clip_until) best = MIN(best, 1000); // (the status bar counts down)
    if (A.toast[0]) best = best < 0 ? 250 : MIN(best, 250);
    if (A.idle_lock_min > 0 && !A.idle_locked && A.last_input) {
        u64 at = A.last_input + (u64)A.idle_lock_min * 60000000000ull;
        i64 ms = at > now ? (i64)((at - now) / 1000000) + 1 : 0;
        best = best < 0 ? ms : MIN(best, ms);
    }
    if (A.settings_dirty) best = best < 0 ? 3000 : MIN(best, 3000);
    return best < 0 ? -1 : (int)MIN(best, (i64)INT_MAX);
}

// --- frame -------------------------------------------------------------------

static bool animating(void) { return fabs(A.scroll_target - A.scroll) > 0.5 || toast_fading(); }

static void window_icon_update(void) { // the logo in the accent colour (X11 / taskbars without the .desktop icon)
    u8 *logo = icon_raster(ICON_LOGO, 128, 128);
    if (!logo) return;
    SDL_Surface *ic = SDL_CreateSurfaceFrom(128, 128, SDL_PIXELFORMAT_RGBA32, logo, 128 * 4);
    if (ic) { SDL_SetWindowIcon(A.win, ic); SDL_DestroySurface(ic); }
    free(logo);
}

// The system icon (launchers, docks: Icon=dev.keychainmanager) in the accent colour too: a recoloured copy in the
// user's icon folder, which wins over the packaged one. Blue is the artwork as packaged, so then the copy goes away.
static void app_icon_sync(void) {
    static u32 done = 0xFFFFFFFF, seen;
    static u64 since;
    if (A.selftest || A.screenshot || COL_ACCENT == done) return;
    if (COL_ACCENT != seen) { seen = COL_ACCENT; since = now_ns(); return; }
    if (now_ns() - since < 1500000000ull) return; // (once the accent has settled)
    done = COL_ACCENT;
    char dh[PATH_MAX], dir[PATH_MAX + 32], path[PATH_MAX + 64];
    data_home(dh, sizeof dh);
    snprintf(dir, sizeof dir, "%s/icons/hicolor", dh);
    snprintf(path, sizeof path, "%s/scalable/apps/" APP_ID ".svg", dir);
    bool changed;
    if ((done & 0xFFFFFF) == LOGO_BASE_ACCENT) changed = unlink(path) == 0;
    else {
        static const char from[] = "width=\"624\" height=\"456\" viewBox=\"0 0 624 456\"";
        const char *at = strstr(g_logo_svg, from);
        if (!at) return;
        char *svg = xmalloc(sizeof g_logo_svg + 64);
        snprintf(svg, sizeof g_logo_svg + 64, "%.*swidth=\"256\" height=\"256\" viewBox=\"0 -84 624 624\"%s\n",
                 (int)(at - g_logo_svg), g_logo_svg, at + strlen(from)); // square, like the packaged icon
        logo_recolor(svg, done);
        changed = !file_equals(path, svg);
        if (changed) {
            char apps[PATH_MAX + 48];
            snprintf(apps, sizeof apps, "%s/scalable/apps", dir);
            mkdir_p(apps, 0755);
            changed = write_file_atomic(path, svg);
        }
        free(svg);
    }
    if (changed) utimensat(AT_FDCWD, dir, NULL, 0); // icon caches watch the theme folder's time
}

static void frame(void) {
    icons_sync_theme();
    if (g_logo_changed) { g_logo_changed = false; window_icon_update(); }
    app_icon_sync();
    u64 t0 = now_ns();
    f32 dt = A.last_frame ? (f32)((t0 - A.last_frame) / 1e9) : 0.016f;
    dt = MIN(dt, 0.05f);
    A.last_frame = t0;
    layout();
    if (A.screenshot && A.loaded && !A.screenshot_frames && getenv("KM_SHOT_SELECT")) { // (README pictures: this item)
        for (int i = 0; i < A.nview; i++)
            if (!strcmp(A.view[i]->label, getenv("KM_SHOT_SELECT"))) select_only(i);
    }
    if (A.ensure_visible) { A.ensure_visible = false; scroll_to_cursor(); }
    A.scroll_target = CLAMP(A.scroll_target, 0.0, max_scroll());
    if (fabs(A.scroll_target - A.scroll) > 0.5) {
        A.scroll += A.smooth_scroll ? (A.scroll_target - A.scroll) * (1.0 - exp(-dt * 22.0)) : A.scroll_target - A.scroll;
        if (fabs(A.scroll_target - A.scroll) <= 0.5) A.scroll = A.scroll_target;
    } else A.scroll = A.scroll_target;
    // If the glyph atlas fills up mid-frame, that frame would show missing text (a flicker):
    // clear it and draw again (growing the atlas if one frame alone doesn't fit) before presenting.
    for (int attempt = 0; attempt < 3; attempt++) {
        frame_draw();
        if (!g_atlas_full) break;
        if (attempt == 0 || !atlas_grow()) atlas_reset();
    }
    A.frame_ms = (f32)((now_ns() - t0) / 1e6);
    if (A.screenshot && A.loaded && !A.loading && !appicons_busy() && A.ss_state != SS_CONNECTING && !g_atlas_full && ++A.screenshot_frames >= 3) {
        SDL_Surface *s = SDL_RenderReadPixels(A.ren, NULL);
        if (s) {
            SDL_SaveBMP(s, A.screenshot);
            SDL_DestroySurface(s);
        }
        A.quit = true;
    }
    SDL_RenderPresent(A.ren);
    if (g_atlas_full) {
        atlas_reset();
        A.redraw = true;
    }
}

// Run the loop body once without waiting (self-tests and the benchmark drive it).
static void pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event(&e);
    main_drain();
    timers_tick();
    frame();
}

#include "selftest.c"

// --- benchmark (headless): how long the service takes to list everything --------------------------

static int run_bench(void) {
    u64 t0 = now_ns(), t_list = 0, t_rows = 0;
    while ((!A.loaded || A.loading) && now_ns() - t0 < 600000000000ull) {
        SDL_WaitEventTimeout(NULL, 2);
        main_drain();
        if (A.loaded && !t_list) t_list = now_ns();
        if (!t_rows && A.nview && !A.view[0]->pending) t_rows = now_ns(); // (pending rows sort last)
    }
    if (t_list) printf("keyrings + item count shown after %.1f ms, first rows readable after %.1f ms\n", (f64)(t_list - t0) / 1e6,
                       (f64)((t_rows ? t_rows : now_ns()) - t0) / 1e6);
    if (A.ss_state != SS_READY) { fprintf(stderr, "no keyring service: %s\n", A.ss_msg); return 1; }
    printf("%s (%s session): %d keyrings, %d items, listed in %.1f ms (ready %.1f ms after the window)\n", A.backend,
           A.encrypted ? "encrypted" : "plain", A.ncolls, A.nitems, A.load_ms, (f64)(now_ns() - t0) / 1e6);
    u64 t1 = now_ns();
    for (int i = 0; i < 20; i++) view_rebuild();
    printf("view (filter + sort): %.2f ms\n", (f64)(now_ns() - t1) / 20e6);
    te_set(&A.search, "a", 1, 1);
    t1 = now_ns();
    for (int i = 0; i < 20; i++) view_rebuild();
    printf("view with a search: %.2f ms (%d shown)\n", (f64)(now_ns() - t1) / 20e6, A.nview);
    return 0;
}

// --- command line -----------------------------------------------------------

static void usage(void) {
    printf("Usage: keychain-manager [--version]\n"
           "  A front end for the keyrings your desktop already has (GNOME Keyring, KWallet, KeePassXC...),\n"
           "  through the Secret Service D-Bus API. It stores nothing itself.\n"
           "       keychain-manager --bench                time listing every keyring and item (headless)\n"
           "       keychain-manager --screenshot OUT.bmp   render one frame (headless)\n");
}

int main(int argc, char **argv) {
    harden_process();
    const char *selftest = NULL;
    bool bench = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bench")) bench = true;
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) A.screenshot = argv[++i];
        else if (!strncmp(argv[i], "--selftest", 10)) selftest = argv[i][10] == '-' ? argv[i] + 11 : "all";
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            int v = SDL_GetVersion();
            printf("keychain-manager %s (SDL %d.%d.%d)\n", APP_VERSION, SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
            return 0;
        } else { fprintf(stderr, "unknown argument: %s\n", argv[i]); usage(); return 2; }
    }
    if (selftest && !selftest_bus_ok()) {
        fprintf(stderr, "self-tests run only on their own session bus with a throwaway keyring: use tools/selftest.sh\n");
        return 2;
    }
    A.selftest = selftest != NULL;
    g_ss.test_bus = A.selftest;
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    if ((A.selftest || A.screenshot || bench) && !getenv("KM_TEST_BUS")) // (the harness gives each run its own folders)
        setenv("XDG_CONFIG_HOME", "/tmp/km-selftest-config", 1); // never touch the user's settings
    bool headless = (A.selftest || A.screenshot || bench) && !getenv("SDL_VIDEODRIVER");
    if (headless) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#ifdef __GLIBC__
    mallopt(M_ARENA_MAX, 2);
#endif
    if (!getenv("MESA_VK_IGNORE_CONFORMANCE_WARNING")) setenv("MESA_VK_IGNORE_CONFORMANCE_WARNING", "1", 1);
    settings_defaults();
    if (!A.selftest && !A.screenshot && !bench) settings_load(); // (tests, screenshots: the defaults)
    themes_scan();
    g_theme_cur = MAX(0, theme_find(A.theme_name));
    theme_apply();

    SDL_SetAppMetadata(APP_NAME, APP_VERSION, APP_ID);
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    g_wake_event = SDL_RegisterEvents(1);
    pool_init();
    ss_start(); // (the keyring service starts answering while the window comes up)

    if (A.screenshot && getenv("KM_SHOT_SIZE")) sscanf(getenv("KM_SHOT_SIZE"), "%dx%d", &A.win_w, &A.win_h); // (checks at other sizes)
    A.csd = titlebar_wanted(); // (borderless from the start: no system title bar flashing up first)
    A.win = SDL_CreateWindow(APP_NAME, A.win_w, A.win_h,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (A.maximized ? SDL_WINDOW_MAXIMIZED : 0) |
                             (A.csd ? SDL_WINDOW_BORDERLESS : 0));
    if (!A.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    window_icon_update();
    titlebar_apply();
    SDL_SetWindowMinimumSize(A.win, 720, 420);
    A.ren = NULL;
    // headless: self-tests on the software renderer (deterministic), screenshots on the GPU (what users see)
    if (headless && A.selftest && !getenv("SDL_RENDER_DRIVER")) A.ren = SDL_CreateRenderer(A.win, "software");
    else if (!getenv("SDL_RENDER_DRIVER")) {
        if (!strcmp(A.renderer, "auto")) A.ren = SDL_CreateRenderer(A.win, "vulkan");
        else A.ren = SDL_CreateRenderer(A.win, A.renderer);
    }
    if (!A.ren) A.ren = SDL_CreateRenderer(A.win, NULL);
    if (!A.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(A.ren, A.selftest || A.screenshot || bench ? 0 : 1);
    g_ren = A.ren;
    A.cur_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    A.cur_ew = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    A.cur_text = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    A.cur_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    SDL_GetWindowSizeInPixels(A.win, &A.W, &A.H);
    A.dpi_scale = SDL_GetWindowDisplayScale(A.win);
    if (A.dpi_scale <= 0) A.dpi_scale = 1;
    A.scale = A.dpi_scale * A.zoom;

    if (!atlas_init(A.ren)) { fprintf(stderr, "font atlas init failed\n"); return 1; }
    A.font_path = A.ui_font[0] ? font_family_file(A.ui_font) : NULL;
    if (!A.font_path) A.font_path = find_font_file(g_ui_families, (int)ARRAY_COUNT(g_ui_families));
    if (!A.font_path || !font_init(&A.font, A.font_path, (int)roundf((f32)A.font_size * A.scale)) ||
        !font_init(&A.font_small, A.font_path, (int)roundf((f32)(A.font_size - 1) * A.scale))) {
        fprintf(stderr, "could not load a UI font\n");
        return 1;
    }
    char *bold = font_weight_file(A.font_path, FC_WEIGHT_SEMIBOLD);
    if (!font_init(&A.font_brand, bold ? bold : A.font_path, (int)roundf((f32)(A.font_size + 3) * A.scale)) ||
        !font_init(&A.font_big, bold ? bold : A.font_path, (int)roundf((f32)(A.font_size + 7) * A.scale))) {
        fprintf(stderr, "could not load a UI font\n");
        return 1;
    }
    free(bold);
    const char *mono[] = {"JetBrains Mono", "Cascadia Code", "Fira Code", "DejaVu Sans Mono", "Noto Sans Mono", "monospace"};
    A.mono_path = find_font_file(mono, (int)ARRAY_COUNT(mono));
    if (A.mono_path && !font_init(&A.font_mono, A.mono_path, (int)roundf((f32)A.font_size * A.scale))) A.mono_path = NULL;
    if (!A.mono_path) A.font_mono = A.font;

    A.side_sel = -1;
    A.cursor = -1;
    A.focus = FOCUS_LIST;
    sheet_init();
    layout();
    SDL_StartTextInput(A.win);
    if (bench) return run_bench();
    if (selftest) return run_selftest(selftest);

    A.redraw = true;
    A.last_input = now_ns();
    while (!A.quit) {
        SDL_Event e;
        bool got;
        int wait = timers_wait_ms();
        if ((A.redraw || animating())) got = SDL_PollEvent(&e);
        else if (!A.loaded || A.loading || A.screenshot) got = SDL_WaitEventTimeout(&e, 30);
        else if (wait >= 0) got = SDL_WaitEventTimeout(&e, MAX(wait, 1));
        else got = SDL_WaitEvent(&e);
        if (got) {
            handle_event(&e);
            while (SDL_PollEvent(&e)) handle_event(&e);
        }
        if (A.quit) break;
        main_drain();
        timers_tick();
        if (A.settings_dirty && !A.screenshot && now_ns() - A.settings_saved > 3000000000ull) settings_save();
        if ((A.toast[0] || (A.clip_owned && A.clip_until)) && now_ns() - A.last_frame > 200000000ull) A.redraw = true; // (toast, countdown)
        if (!A.redraw && !animating() && !A.screenshot) continue;
        frame();
        A.redraw = false;
    }
    clip_quit();
    reveal_clear();
    if (!A.screenshot) settings_save();
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
    return 0;
}
