// render.c - all drawing: brand + title bar, top bar (keyring, search, buttons), sidebar (keyrings, service),
// header + item list, details panel (secret, attributes), status bar, empty states, dialog, menu, toast, palette

// --- small pieces -------------------------------------------------------------

static void draw_brand(void) {
    Rect r = A.r_brand;
    draw_rect(r.x, r.y, r.w, r.h, C(COL_SIDEBAR));
    draw_rect(r.x + r.w - 1, r.y, 1, r.h, C(COL_BORDER));
    f32 lh = roundf(S(17)), lw = roundf(lh * 624.f / 456.f), lx = roundf(r.x + S(18)), ly = roundf(r.y + (r.h - lh) * 0.5f);
    Glyph *g = icon_glyph(lh < 48 ? ICON_LOGO_SMALL : ICON_LOGO, (int)lw); // square raster, logo centred in it
    if (g) draw_glyph(g, lx, roundf(ly - (lw - lh) * 0.5f), (Color){1, 1, 1, 1});
    Font *f = &A.font_brand;
    f32 tx = lx + lw + roundf(S(8));
    draw_text_fit(f, tx, text_y(f, r.y, r.h), r.x + r.w - tx - S(8), APP_LABEL, (int)strlen(APP_LABEL), C(COL_TEXT), 0);
}

// Square outline of side s and line t on whole pixels.
static void draw_box(f32 x, f32 y, f32 s, f32 t, Color c) {
    x = roundf(x);
    y = roundf(y);
    s = roundf(s);
    draw_rect(x, y, s, t, c);
    draw_rect(x, y + s - t, s, t, c);
    draw_rect(x, y + t, t, s - 2 * t, c);
    draw_rect(x + s - t, y + t, t, s - 2 * t, c);
}

// Own title bar: minimise / maximise / close at the top right (Windows-sized, red close on hover), and a hairline
// round the window (borderless windows get no shadow from some compositors).
static void draw_window_controls(void) {
    if (!A.csd) return;
    bool max = window_maximized();
    f32 lt = MAX(1.f, roundf(S(1))), is = roundf(S(11));
    for (int i = 0; i < 3; i++) {
        Rect b = wctl_button(i);
        bool hover = A.mouse_in && A.drag == DRAG_NONE && in_rect(b, A.mx, A.my);
        if (hover) draw_rect(b.x, b.y, b.w, b.h, i == 2 ? (Color){0xC4 / 255.f, 0x2B / 255.f, 0x1C / 255.f, 1} : C(COL_TAB_HOVER));
        Color ic = hover ? (i == 2 ? (Color){1, 1, 1, 1} : C(COL_TEXT)) : C(COL_TEXT2);
        f32 cx = roundf(b.x + b.w * 0.5f), cy = roundf(b.y + b.h * 0.5f), x0 = cx - is * 0.5f, y0 = cy - is * 0.5f;
        if (i == 0) draw_rect(x0, cy, is, lt, ic);
        else if (i == 1 && !max) draw_box(x0, y0, is, lt, ic); // a plain square, whole pixels (no rounded, blurred corners)
        else if (i == 1) { // restore: two overlapping windows
            f32 o = roundf(S(2)), w2 = is - o;
            draw_rect(x0 + o, y0, w2, lt, ic);
            draw_rect(x0 + is - lt, y0, lt, w2, ic);
            draw_box(x0, y0 + o, w2, lt, ic);
        } else draw_cross(cx, cy, is, lt, ic);
    }
    if (!max) {
        Color bc = C(COL_BORDER);
        f32 W = (f32)A.W, H = (f32)A.H;
        draw_rect(0, 0, W, 1, bc);
        draw_rect(0, H - 1, W, 1, bc);
        draw_rect(0, 0, 1, H, bc);
        draw_rect(W - 1, 0, 1, H, bc);
    }
}

static void fmt_date(u64 t0, char *buf, size_t cap, const struct tm *now) {
    if (!t0) { snprintf(buf, cap, "\xE2\x80\x94"); return; }
    time_t t = (time_t)t0;
    struct tm tm;
    localtime_r(&t, &tm);
    if (tm.tm_year == now->tm_year && tm.tm_yday == now->tm_yday) strftime(buf, cap, "Today %H:%M", &tm);
    else strftime(buf, cap, "%Y-%m-%d %H:%M", &tm);
}

static void draw_icon_button(Rect r, int kind, bool on) {
    bool hv = hovering(r) && A.drag == DRAG_NONE;
    if (hv || on) draw_rrect(r.x, r.y, r.w, r.h, S(7), C(on ? COL_CONTROL : COL_CONTROL_HOVER));
    Color ic = C(hv ? COL_TEXT : COL_TEXT2);
    f32 cx = roundf(r.x + r.w * 0.5f), cy = roundf(r.y + r.h * 0.5f);
    if (kind == 0) draw_plus(cx, cy, S(12), MAX(1.f, S(1.6f)), ic);
    else if (kind == 1) draw_lock_icon(cx, cy, S(16), ic, false, hv || on ? (on ? COL_CONTROL : COL_CONTROL_HOVER) : COL_TABBAR);
    else { // details panel: a window with its right part split off
        f32 w = roundf(S(16)), h = roundf(S(13)), x0 = roundf(cx - w * 0.5f), y0 = roundf(cy - h * 0.5f), t = MAX(1.f, roundf(S(1.3f)));
        draw_rrect_outline(x0, y0, w, h, S(2.5f), t, ic);
        draw_rect(roundf(x0 + w * 0.62f), y0 + 1, t, h - 2, ic);
        if (on) draw_rect(roundf(x0 + w * 0.62f), y0 + 1, roundf(w * 0.38f) - 1, h - 2, ic);
    }
}

// An item's icon: the app that saved it when its icon is installed, else its kind's.
static void draw_item_icon(const Item *it, f32 x, f32 y, f32 s, f32 alpha) {
    int id = appicon_id(it->app_key);
    if (id < 0 || !draw_icon_id(x, y, s, id, alpha)) draw_icon_id(x, y, s, it->icon, alpha);
}

// --- top bar: where you are, search, buttons --------------------------------------------------

static void draw_topbar(void) {
    Rect t = A.r_top;
    draw_rect(t.x, t.y, t.w, t.h, C(COL_TABBAR));
    draw_rect(t.x, t.y + t.h - 1, t.w, 1, C(COL_BORDER));
    Font *f = &A.font_brand, *fs = &A.font_small;
    // the keyring being shown
    Rect cr = A.r_crumb;
    if (cr.w > S(40)) {
        const char *name = A.side_sel >= 0 ? coll_label(A.side_sel) : "All items";
        f32 is = roundf(S(18)), iy = roundf(cr.y + (cr.h - is) * 0.5f);
        draw_icon_id(cr.x, iy, is, A.side_sel >= 0 ? ICON_KEYRING : ICON_ALL_ITEMS, 1.f);
        f32 tx = cr.x + is + roundf(S(9));
        f32 nw = MIN(text_width(f, name, (int)strlen(name)), cr.x + cr.w - tx); // (narrow: the count goes first, then the name elides)
        draw_text_fit(f, tx, text_y(f, cr.y, cr.h), nw + 1, name, (int)strlen(name), C(COL_TEXT), 0);
        char n[48];
        fmt_count(n, (u64)A.nview);
        char m[80];
        snprintf(m, sizeof m, "%s item%s", n, A.nview == 1 ? "" : "s");
        f32 mx = tx + nw + roundf(S(10));
        if (mx + text_width(fs, m, (int)strlen(m)) < cr.x + cr.w)
            draw_text(fs, mx, text_y(fs, cr.y, cr.h) + S(1), m, (int)strlen(m), C(COL_TEXT_FAINT));
        if (A.side_sel >= 0 && A.colls[A.side_sel].locked) {
            f32 lx = mx + text_width(fs, m, (int)strlen(m)) + S(12);
            if (lx + S(60) < cr.x + cr.w) {
                draw_lock_icon(lx + S(6), cr.y + cr.h * 0.5f, S(12), C(COL_WARN), false, COL_TABBAR);
                draw_text(fs, lx + S(16), text_y(fs, cr.y, cr.h) + S(1), "locked", 6, C(COL_WARN));
            }
        }
    }
    // search field
    Rect s = A.r_search;
    bool focused = A.focus == FOCUS_SEARCH && !sheet_open() && !A.pal_open;
    draw_rrect(s.x, s.y, s.w, s.h, S(7), C(focused || hovering(s) ? COL_FIELD_HOVER : COL_FIELD));
    if (focused) draw_rrect_outline(s.x, s.y, s.w, s.h, S(7), MAX(1.f, S(1.2f)), C(COL_ACCENT));
    f32 r = roundf(S(4.5f)), mcx = s.x + S(14), mcy = s.y + s.h * 0.5f - S(1);
    Color mc = C(focused ? COL_ACCENT : COL_TEXT_DIM);
    draw_rrect_outline(mcx - r, mcy - r, r * 2, r * 2, r, MAX(1.f, S(1.4f)), mc);
    draw_line(mcx + r * 0.7f, mcy + r * 0.7f, mcx + r * 1.6f, mcy + r * 1.6f, MAX(1.5f, S(1.5f)), mc);
    Rect field = {s.x + S(26), s.y, s.w - S(52), s.h};
    te_draw(&A.search, &A.font, field, roundf(S(4)), C(COL_TEXT), focused);
    if (!A.search.buf[0]) {
        char ph[96], k[32];
        key_label_for(CMD_FIND, k, sizeof k);
        snprintf(ph, sizeof ph, "Search%s%s%s", k[0] ? "  (" : "", k, k[0] ? ")" : "");
        draw_text_fit(&A.font, field.x + S(4), text_y(&A.font, s.y, s.h), field.w, ph, (int)strlen(ph), C(COL_TEXT_FAINT), 0);
    } else {
        Rect clr = {s.x + s.w - S(26), s.y, S(26), s.h};
        draw_cross(clr.x + clr.w * 0.5f, clr.y + clr.h * 0.5f, S(8), MAX(1.f, S(1.4f)), C(hovering(clr) ? COL_TEXT : COL_TEXT_DIM));
    }
    draw_icon_button(A.r_btn_new, 0, false);
    draw_icon_button(A.r_btn_lock, 1, false);
    draw_icon_button(A.r_btn_insp, 2, A.insp_on);
}

// --- sidebar -------------------------------------------------------------------

static void draw_sidebar(void) {
    Rect sb = A.r_side;
    draw_rect(sb.x, sb.y, sb.w, sb.h, C(COL_SIDEBAR));
    draw_rect(sb.x + sb.w - 1, sb.y, 1, sb.h, C(A.drag == DRAG_SIDEBAR || (A.mouse_in && fabsf(A.mx - sb.w) <= S(4) && A.my > sb.y) ? COL_ACCENT : COL_BORDER));
    Font *f = &A.font, *fs = &A.font_small;
    ClipSave cs = draw_clip_push((SDL_Rect){(int)sb.x, (int)sb.y, (int)sb.w - 1, (int)(A.r_side_footer.y - sb.y)});
    side_add_btn = (Rect){0, 0, 0, 0};
    int total = A.nitems;
    for (int i = 0; i < A.nside_rows; i++) {
        SideRow *row = &A.side_rows[i];
        Rect r = row->r;
        side_lock_btn[i] = (Rect){0, 0, 0, 0};
        bool hover = A.mouse_in && in_rect(r, A.mx, A.my) && A.drag == DRAG_NONE;
        if (row->kind == SR_HEADER) {
            draw_text(fs, r.x + S(10), text_y(fs, r.y, r.h), "Keyrings", 8, C(COL_TEXT_FAINT));
            f32 bs = roundf(S(20));
            Rect pr = {roundf(r.x + r.w - bs - S(4)), roundf(r.y + (r.h - bs) * 0.5f), bs, bs};
            side_add_btn = pr;
            bool ph = hovering(pr);
            if (ph) draw_rrect(pr.x, pr.y, pr.w, pr.h, S(5), C(COL_CHIP));
            draw_plus(pr.x + bs * 0.5f, pr.y + bs * 0.5f, S(10), MAX(2.f, S(1.6f)), C(ph ? COL_TEXT : hover ? COL_TEXT_DIM : COL_TEXT_FAINT));
            continue;
        }
        bool active = row->kind == SR_ALL ? A.side_sel < 0 : A.side_sel == row->coll;
        if (active) draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), C(rgb_mix(COL_SEL_IDLE, COL_ACCENT, 0.16f))); // accent-tinted
        else if (hover) draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), C(COL_HOVER));
        if (active) { // accent edge: the left strip of the row's own rounded shape, full height
            ClipSave ac = draw_clip_push((SDL_Rect){(int)r.x, (int)r.y, (int)roundf(S(3)), (int)r.h});
            draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), C(COL_ACCENT));
            draw_clip_pop(ac);
        }
        f32 is = roundf(S(18)), ix = roundf(r.x + S(11)), iy = roundf(r.y + (r.h - is) * 0.5f);
        const char *label;
        int count;
        bool locked = false, def = false;
        if (row->kind == SR_ALL) { label = "All items"; count = total; draw_icon_id(ix, iy, is, ICON_ALL_ITEMS, 1.f); }
        else {
            Coll *c = &A.colls[row->coll];
            label = coll_label(row->coll);
            count = c->nitems;
            locked = c->locked;
            def = c->is_default;
            draw_icon_id(ix, iy, is, ICON_KEYRING, locked ? 0.5f : 1.f);
        }
        f32 lx = ix + is + S(10), right = r.x + r.w - S(8);
        if (row->kind == SR_COLL && hover) { // lock / unlock on hover, in the count's place
            f32 bs = roundf(S(22));
            Rect lb = {right - bs + S(4), roundf(r.y + (r.h - bs) * 0.5f), bs, bs};
            side_lock_btn[i] = lb;
            bool lh = hovering(lb);
            if (lh) draw_rrect(lb.x, lb.y, lb.w, lb.h, S(5), C(COL_CHIP));
            draw_lock_icon(lb.x + bs * 0.5f, lb.y + bs * 0.5f, S(13), C(lh ? COL_TEXT : COL_TEXT_DIM), !locked, lh ? COL_CHIP : COL_HOVER);
            right = lb.x - S(4);
        } else if (locked) {
            draw_lock_icon(right - S(6), r.y + r.h * 0.5f, S(12), C(COL_TEXT_FAINT), false, active ? rgb_mix(COL_SEL_IDLE, COL_ACCENT, 0.16f) : COL_SIDEBAR);
            right -= S(18);
        } else {
            char n[32];
            fmt_count(n, (u64)count);
            f32 nw = text_width(fs, n, (int)strlen(n));
            draw_text(fs, right - nw, text_y(fs, r.y, r.h), n, (int)strlen(n), C(COL_TEXT_FAINT));
            right -= nw + S(8);
        }
        if (def) { // the default keyring: a small dot after its name
            f32 lw = MIN(text_width(f, label, (int)strlen(label)), right - lx - S(14));
            draw_text_fit(f, lx, text_y(f, r.y, r.h), lw + 1, label, (int)strlen(label), C(active ? COL_TEXT : locked ? COL_TEXT_DIM : COL_TEXT2), 0);
            f32 dr = roundf(S(2.5f));
            draw_rrect(roundf(lx + lw + S(7)), roundf(r.y + r.h * 0.5f - dr), dr * 2, dr * 2, dr, C(COL_ACCENT));
        } else
            draw_text_fit(f, lx, text_y(f, r.y, r.h), right - lx, label, (int)strlen(label), C(active ? COL_TEXT : locked ? COL_TEXT_DIM : COL_TEXT2), 0);
    }
    if (A.loaded && A.ss_state == SS_READY && !A.ncolls) {
        Rect r = A.side_rows[A.nside_rows - 1].r;
        draw_text(fs, r.x + S(12), r.y + r.h + S(8), "No keyrings", 11, C(COL_TEXT_FAINT));
    }
    draw_clip_pop(cs);
    // footer: which service answers, and how secrets travel
    Rect ft = A.r_side_footer;
    draw_rect(ft.x + S(12), ft.y, ft.w - S(24), 1, C(COL_BORDER));
    u32 dot = A.ss_state == SS_READY ? COL_OK : A.ss_state == SS_CONNECTING || A.ss_state == SS_KWALLET_OFF ? COL_WARN : COL_DANGER;
    const char *name = A.ss_state == SS_READY || A.ss_state == SS_KWALLET_OFF ? A.backend : A.ss_state == SS_CONNECTING ? "Connecting\xE2\x80\xA6" : "No keyring service";
    char sub[96];
    if (A.ss_state == SS_READY) snprintf(sub, sizeof sub, "%s", A.encrypted ? "Secret Service \xC2\xB7 encrypted" : "Secret Service \xC2\xB7 unencrypted");
    else if (A.ss_state == SS_KWALLET_OFF) snprintf(sub, sizeof sub, "Secret Service off");
    else snprintf(sub, sizeof sub, "Secret Service");
    f32 dr = roundf(S(3.5f)), dx = ft.x + S(18), cy = ft.y + roundf(S(18));
    draw_rrect(dx, roundf(cy - dr), dr * 2, dr * 2, dr, C(dot));
    draw_text_fit(f, dx + S(14), text_y(f, cy - S(9), S(18)), ft.w - S(48), name, (int)strlen(name), C(COL_TEXT2), 0);
    draw_text_fit(fs, dx + S(14), text_y(fs, cy + S(7), S(16)), ft.w - S(48), sub, (int)strlen(sub), C(COL_TEXT_FAINT), 0);
}

// --- header + list ----------------------------------------------------------------

static void draw_header(void) {
    Rect h = A.r_header;
    draw_rect(h.x, h.y, h.w, h.h, C(COL_BG));
    draw_rect(h.x, h.y + h.h - 1, h.w, 1, C(COL_BORDER));
    Font *f = &A.font_small;
    f32 ty = text_y(f, h.y, h.h);
    for (int i = 0; i < A.ncols_vis; i++) {
        int c = A.cols_vis[i];
        f32 x0 = A.colx0[c], x1 = A.colx1[c];
        Rect r = {x0, h.y + S(4), x1 - x0, h.h - S(8)};
        if (A.mouse_in && A.drag == DRAG_NONE && in_rect(r, A.mx, A.my) && header_edge_at(A.mx) < 0 && !A.menu.open)
            draw_rrect(r.x + S(2), r.y, r.w - S(4), r.h, S(5), C(COL_HOVER));
        bool active = A.sort_col == c;
        const char *label = g_coldef[c].label;
        int len = (int)strlen(label);
        f32 tw = text_width(f, label, len), tx = c == COL_NAME ? x0 + S(32) : x0 + S(10);
        draw_text_fit(f, tx, ty, x1 - tx - S(4), label, len, C(active ? COL_TEXT : COL_TEXT_DIM), 0);
        if (active) draw_chevron(MIN(tx + tw + S(9), x1 - S(8)), h.y + h.h * 0.5f, S(7), A.sort_desc ? 3 : 2, MAX(1.f, S(1.3f)), C(COL_ACCENT));
        if (i > 0) {
            bool eh = (A.drag == DRAG_COL && (A.drag_col == c || A.drag_col == A.cols_vis[i - 1])) ||
                      (A.mouse_in && A.drag == DRAG_NONE && in_rect(h, A.mx, A.my) && header_edge_at(A.mx) == c);
            draw_rect(x0, h.y + S(eh ? 5 : 9), 1, h.h - S(eh ? 10 : 18), eh ? C(COL_ACCENT) : C(COL_BORDER));
        }
    }
}

// Text with the parts that match the search highlighted (every word of it, case-insensitive).
static void draw_text_marked(Font *f, f32 x, f32 y, f32 maxw, const char *s, Color c, Color hc) {
    int len = (int)strlen(s);
    const char *q = A.search.buf;
    if (!q[0] || maxw <= 0) { draw_text_fit(f, x, y, maxw, s, len, c, 0); return; }
    static u8 mark[1024];
    int n = MIN(len, (int)sizeof mark);
    memset(mark, 0, (size_t)n);
    char buf[256];
    snprintf(buf, sizeof buf, "%s", q);
    char *save = NULL;
    for (char *w = strtok_r(buf, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
        size_t wl = strlen(w);
        for (int i = 0; i + (int)wl <= n; i++)
            if (str_iprefix(s + i, w, wl)) memset(mark + i, 1, wl);
    }
    f32 bl = roundf(y + f->ascent), pen = roundf(x), lim = x + maxw - text_width(f, ELLIPSIS, 3);
    bool over = text_width(f, s, len) > maxw;
    const char *p = s, *end = s + len;
    while (p < end) {
        int bi = (int)(p - s);
        Glyph *g = font_glyph(f, utf8_decode(&p, end));
        if (over && pen + g->adv > lim) { draw_text(f, pen, y, ELLIPSIS, 3, c); return; }
        bool hl = bi < n && mark[bi];
        if (hl) draw_rect(roundf(pen), y + S(1), roundf(g->adv + 0.5f), f->line_h - S(1), CA(COL_SEL_HL, 0.22f));
        if (g->w) draw_glyph(g, roundf(pen) + g->bx, bl - g->by, hl ? hc : c);
        pen += g->adv;
    }
}

static void draw_lock_badge(f32 ix, f32 iy, f32 is, u32 bg) {
    f32 bs = roundf(is * 0.62f), bx = ix + is - bs * 0.7f, by = iy + is - bs * 0.75f;
    draw_rrect(bx - S(1), by - S(1), bs + S(2), bs + S(2), S(3), C(bg));
    draw_lock_icon(bx + bs * 0.5f, by + bs * 0.5f, bs, C(COL_WARN), false, bg);
}

static void draw_list(void) {
    Rect lr = A.r_list;
    draw_rect(lr.x, lr.y, lr.w, lr.h, C(COL_BG));
    ClipSave cs = draw_clip_push((SDL_Rect){(int)lr.x, (int)lr.y, (int)lr.w, (int)lr.h});
    Font *f = &A.font, *fs = &A.font_small;
    f32 rh = A.row_h, is = roundf(S(18));
    f32 rx = lr.x + roundf(S(6)), rw = A.r_track.x - rx - roundf(S(2));
    int first = (int)(A.scroll / rh);
    bool hovering_list = A.mouse_in && A.drag == DRAG_NONE && in_rect(lr, A.mx, A.my) && !in_rect(A.r_track, A.mx, A.my) && !A.menu.open;
    int hover = hovering_list ? row_at(A.my) : -1;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    bool active = A.focus == FOCUS_LIST || A.focus == FOCUS_SEARCH;
    u32 sel_col = active ? COL_SEL : COL_SEL_IDLE, sel_cur = active ? COL_SEL_FOCUS : COL_SEL_IDLE;
    for (int i = first; i < A.nview; i++) {
        f32 y = roundf((f32)((f64)lr.y + (f64)i * rh - A.scroll));
        if (y >= lr.y + lr.h) break;
        Item *it = A.view[i];
        bool sel = it->sel, cur = i == A.cursor;
        if (A.alt_rows && (i & 1) && !sel) draw_rect(lr.x, y, A.r_track.x - lr.x, rh, C(COL_ALT_ROW));
        if (sel) draw_rrect(rx, y + 1, rw, rh - 2, S(5), C(cur ? sel_cur : sel_col));
        else if (i == hover) draw_rrect(rx, y + 1, rw, rh - 2, S(5), C(COL_HOVER));
        if (cur && !sel) draw_rrect_outline(rx, y + 1, rw, rh - 2, S(5), MAX(1.f, S(1.f)), CA(COL_ACCENT, 0.7f));
        bool bright = sel && active;
        u32 rowbg = sel ? (cur ? sel_cur : sel_col) : i == hover ? COL_HOVER : (A.alt_rows && (i & 1)) ? COL_ALT_ROW : COL_BG;
        Color tc = C(bright ? COL_SEL_TEXT : COL_TEXT), dc = C(bright ? COL_SEL_DIM : COL_TEXT_DIM), hc = C(bright ? COL_SEL_HL : COL_TEXT_STRONG);
        for (int k = 0; k < A.ncols_vis; k++) {
            int c = A.cols_vis[k];
            f32 x0 = A.colx0[c], x1 = A.colx1[c];
            if (c == COL_NAME) {
                f32 ix = x0 + S(6), iy = roundf(y + (rh - is) * 0.5f);
                draw_item_icon(it, ix, iy, is, it->locked ? 0.55f : 1.f);
                if (it->locked) draw_lock_badge(ix, iy, is, rowbg);
                f32 tx = x0 + S(32);
                const char *lab = it->pending ? "\xE2\x80\xA6" : it->label[0] ? it->label : "(no name)";
                if (!it->label[0]) draw_text_fit(f, tx, text_y(f, y, rh), x1 - tx - S(8), lab, (int)strlen(lab), C(COL_TEXT_FAINT), 0);
                else draw_text_marked(f, tx, text_y(f, y, rh), x1 - tx - S(8), lab, it->locked ? dc : tc, hc);
                continue;
            }
            char buf[64];
            const char *v = "";
            switch (c) {
            case COL_ACCOUNT: v = it->account; break;
            case COL_WHERE: v = it->where; break;
            case COL_KIND: v = it->kind; break;
            case COL_KEYRING: v = coll_label(it->coll); break;
            case COL_MODIFIED: fmt_date(it->modified, buf, sizeof buf, &tm_now); v = buf; break;
            case COL_CREATED: fmt_date(it->created, buf, sizeof buf, &tm_now); v = buf; break;
            }
            Font *cf = c == COL_ACCOUNT || c == COL_WHERE ? f : fs;
            if (c == COL_ACCOUNT || c == COL_WHERE) draw_text_marked(cf, x0 + S(10), text_y(cf, y, rh), x1 - x0 - S(16), v, c == COL_ACCOUNT ? tc : dc, hc);
            else draw_text_fit(cf, x0 + S(10), text_y(cf, y, rh), x1 - x0 - S(16), v, (int)strlen(v), dc, 0);
        }
    }
    // scrollbar
    if (max_scroll() > 0) {
        f32 ty, th = scroll_thumb(&ty);
        bool hv = A.drag == DRAG_SCROLL || (A.mouse_in && in_rect(A.r_track, A.mx, A.my));
        f32 tw = roundf(S(hv ? 7 : 5));
        draw_rrect(A.r_track.x + A.r_track.w - tw - S(2), ty + S(2), tw, th - S(4), tw * 0.5f, C(hv ? COL_SCROLL_HOVER : COL_SCROLL));
    }
    draw_clip_pop(cs);
}

// Centred message for the content area: an icon, a title, a line of text and maybe a button.
static void draw_empty(int icon, const char *title, const char *msg, const char *button, int cmd) {
    Rect r = A.r_list;
    Font *bf = &A.font_brand, *fs = &A.font;
    f32 is = roundf(S(44)), cy = roundf(r.y + r.h * 0.38f);
    f32 total = is + S(16) + bf->line_h + S(8) + fs->line_h * 2 + (button ? S(52) : 0);
    f32 y = roundf(cy - total * 0.5f);
    if (icon >= 0) draw_icon_id(roundf(r.x + (r.w - is) * 0.5f), y, is, icon, 0.9f);
    y += is + S(16);
    draw_text_fit(bf, r.x + S(20), y, r.w - S(40), title, (int)strlen(title), C(COL_TEXT), 2);
    y += bf->line_h + S(8);
    int st[4], ln[4];
    int nl = wrap_text(fs, msg, MIN(r.w - S(60), S(520)), st, ln, 4);
    for (int i = 0; i < nl; i++) {
        draw_text_fit(fs, r.x + S(30), y, r.w - S(60), msg + st[i], ln[i], C(COL_TEXT_DIM), 2);
        y += roundf(fs->line_h * 1.3f);
    }
    empty_btn = (Rect){0, 0, 0, 0};
    empty_btn_cmd = 0;
    if (button) {
        f32 bw = button_w(button), bh = roundf(S(32));
        empty_btn = (Rect){roundf(r.x + (r.w - bw) * 0.5f), y + S(14), bw, bh};
        empty_btn_cmd = cmd;
        draw_button(empty_btn, button, true, false, true);
    }
}

static void draw_content(void) {
    draw_header();
    draw_list();
    empty_btn = (Rect){0, 0, 0, 0};
    empty_btn_cmd = 0;
    if (A.nview) return;
    if (A.ss_state == SS_CONNECTING || (!A.loaded && A.ss_state != SS_MISSING)) {
        draw_empty(-1, "Opening the keyrings\xE2\x80\xA6", "", NULL, 0);
    } else if (A.ss_state == SS_KWALLET_OFF) {
        draw_empty(ICON_KEYRING, "KWallet's Secret Service support is off",
                   "KWallet is installed, but its Secret Service API (the standard one this app reads keyrings through) is turned "
                   "off. Turn it on and KWallet restarts with it; nothing in your wallets changes.",
                   pending_any() ? NULL : "Turn it on", CMD_ENABLE_KWALLET);
    } else if (A.ss_state == SS_MISSING || A.ss_state == SS_FAILED) {
        char m[400];
        snprintf(m, sizeof m, "Nothing answers for org.freedesktop.secrets on this session. Start GNOME Keyring or KWallet, or turn on "
                              "KeePassXC's Secret Service integration, then try again.%s%s",
                 A.ss_msg[0] ? "  " : "", A.ss_state == SS_FAILED ? A.ss_msg : "");
        draw_empty(ICON_LOCK, "No keyring service", m, "Try again", CMD_RELOAD);
    } else if (A.side_sel >= 0 && A.side_sel < A.ncolls && A.colls[A.side_sel].locked && !A.search.buf[0]) {
        draw_empty(ICON_LOCK, "This keyring is locked", "Unlock it to see and use what's inside. The keyring service asks for its password.",
                   "Unlock", CMD_UNLOCK_KEYRING);
    } else if (A.search.buf[0]) {
        char m[300];
        snprintf(m, sizeof m, "No name, account or attribute contains \xE2\x80\x9C%s\xE2\x80\x9D%s", A.search.buf,
                 A.side_sel >= 0 ? " in this keyring." : ".");
        draw_empty(-1, "No matches", m, NULL, 0);
    } else if (A.ncolls == 0) {
        draw_empty(ICON_KEYRING, "No keyrings yet", "Create one to keep passwords in. Apps also create theirs the first time they save a secret.",
                   "New keyring", CMD_NEW_KEYRING);
    } else {
        draw_empty(ICON_KEY, "Nothing here yet", "Passwords that apps save show up here. You can also add one yourself.", "New item", CMD_NEW_ITEM);
    }
}

// --- details panel ------------------------------------------------------------------

static f32 insp_section(f32 x, f32 y, const char *t) {
    draw_text(&A.font_small, x, y, t, (int)strlen(t), C(COL_TEXT_FAINT));
    return y + roundf(S(22));
}

// One "label  value" row; returns the row rect (value copyable by clicking where the caller says).
static Rect insp_row(f32 x, f32 *y, f32 w, const char *label, const char *value, bool hoverable) {
    Font *f = &A.font, *fs = &A.font_small;
    f32 lw = roundf(S(84)), h = roundf(S(28));
    Rect r = {x - S(6), *y, w + S(12), h};
    if (hoverable && value[0] && hovering(r)) draw_rrect(r.x, r.y, r.w, r.h, S(5), C(COL_HOVER));
    draw_text_fit(fs, x, text_y(fs, *y, h), lw - S(6), label, (int)strlen(label), C(COL_TEXT_FAINT), 0);
    draw_text_fit(f, x + lw, text_y(f, *y, h), w - lw - (hoverable ? S(20) : 0), value[0] ? value : "\xE2\x80\x94", (int)strlen(value[0] ? value : "\xE2\x80\x94"),
                  C(value[0] ? COL_TEXT2 : COL_TEXT_FAINT), 0);
    if (hoverable && value[0] && hovering(r)) draw_copy_icon(x + w - S(12), *y + (h - S(12)) * 0.5f, S(12), C(COL_TEXT_DIM), COL_HOVER);
    *y += h;
    return hoverable && value[0] ? r : (Rect){0, 0, 0, 0};
}

static void draw_inspector(void) {
    insp_btn_reveal = insp_btn_copy = insp_btn_unlock = insp_btn_edit = insp_btn_delete = insp_btn_acc = insp_btn_where = (Rect){0, 0, 0, 0};
    if (!A.insp_on || A.r_insp.w <= 0) return;
    Rect r = A.r_insp;
    draw_rect(r.x, r.y, r.w, r.h, C(COL_PREVIEW_BG));
    draw_rect(r.x, r.y, 1, r.h, C(A.drag == DRAG_INSPECTOR || (A.mouse_in && fabsf(A.mx - r.x) <= S(4) && A.my > r.y) ? COL_ACCENT : COL_BORDER));
    Item *it = cursor_item();
    Font *f = &A.font, *fs = &A.font_small, *bf = &A.font_brand;
    static char shown_path[512];
    if (strcmp(shown_path, it ? it->path : "")) { snprintf(shown_path, sizeof shown_path, "%s", it ? it->path : ""); A.insp_scroll = 0; }
    A.insp_scroll = CLAMP(A.insp_scroll, 0.f, MAX(0.f, A.insp_content_h - r.h));
    f32 pad = roundf(S(20)), x = r.x + pad, w = r.w - pad * 2, y = r.y + pad - roundf(A.insp_scroll), y_top = y;
    if (!it) {
        const char *m = A.nview ? "Pick an item to see its details." : "";
        draw_text_fit(fs, x, r.y + r.h * 0.4f, w, m, (int)strlen(m), C(COL_TEXT_FAINT), 2);
        return;
    }
    ClipSave cs = draw_clip_push((SDL_Rect){(int)r.x + 1, (int)r.y, (int)r.w - 1, (int)r.h});
    if (A.nsel > 1) {
        char m[64];
        snprintf(m, sizeof m, "%d items selected", A.nsel);
        draw_text_fit(bf, x, y, w, m, (int)strlen(m), C(COL_TEXT), 0);
        y += bf->line_h + S(6);
        const char *h = "Showing the one under the cursor.";
        draw_text(fs, x, y, h, (int)strlen(h), C(COL_TEXT_FAINT));
        y += roundf(S(28));
    }
    // icon, name, kind
    f32 is = roundf(S(40));
    draw_item_icon(it, x, y, is, it->locked ? 0.6f : 1.f);
    f32 tx = x + is + S(14);
    const char *lab = it->label[0] ? it->label : "(no name)";
    int st[3], ln[3];
    int nl = wrap_text(bf, lab, w - is - S(14), st, ln, 2);
    f32 ty = y + (nl == 1 ? S(1) : -S(4));
    for (int i = 0; i < nl; i++) {
        draw_text_fit(bf, tx, ty, w - is - S(14), lab + st[i], i == nl - 1 ? (int)strlen(lab + st[i]) : ln[i], C(COL_TEXT_STRONG), 0);
        ty += bf->line_h;
    }
    draw_text_fit(fs, tx, ty + S(1), w - is - S(14), it->kind, (int)strlen(it->kind), C(COL_TEXT_DIM), 0);
    y += MAX(is, ty + fs->line_h - y) + roundf(S(18));
    // secret card
    f32 ch = roundf(S(46));
    u32 card = rgb_mix(COL_PREVIEW_BG, COL_CONTROL, 0.6f);
    bool shown = revealed(it);
    bool pending = A.rev_item && !strcmp(A.rev_item, it->path) && !A.rev.p;
    int lines = 1;
    char hexbuf[200];
    const char *stext = NULL;
    int slen = 0;
    if (shown) {
        if (sec_is_text(&A.rev)) { stext = (const char *)A.rev.p; slen = (int)A.rev.n; }
        else { // binary: the first bytes in hex
            size_t k = 0;
            for (size_t i = 0; i < A.rev.n && k + 4 < sizeof hexbuf - 24; i++) k += (size_t)snprintf(hexbuf + k, sizeof hexbuf - k, "%02x ", A.rev.p[i]);
            snprintf(hexbuf + k, sizeof hexbuf - k, "(%zu bytes)", A.rev.n);
            stext = hexbuf;
            slen = (int)strlen(hexbuf);
        }
    }
    int sst[6], sln[6];
    Font *mf = &A.font_mono;
    f32 bsz = roundf(S(30)), textw = w - S(24) - bsz * 2 - S(8);
    if (stext) {
        static char tmp[4096];
        int n = MIN(slen, (int)sizeof tmp - 1);
        memcpy(tmp, stext, (size_t)n);
        tmp[n] = 0;
        // break anywhere (passwords have no spaces): fixed-width chunks that fit
        lines = 0;
        int i = 0;
        while (i < n && lines < 6) {
            int j = i;
            while (j < n && tmp[j] != '\n' && text_width(mf, tmp + i, j - i + 1) <= textw) j = (int)(utf8_next(tmp, j, n));
            if (j == i) j = utf8_next(tmp, i, n);
            sst[lines] = i;
            sln[lines] = j - i;
            lines++;
            i = j < n && tmp[j] == '\n' ? j + 1 : j;
        }
        if (!lines) lines = 1, sst[0] = 0, sln[0] = 0;
        ch = MAX(ch, roundf(lines * mf->line_h + S(22)));
        draw_rrect(x, y, w, ch, S(8), C(card));
        f32 ly = y + roundf((ch - lines * mf->line_h) * 0.5f);
        for (int k = 0; k < lines; k++) { draw_text(mf, x + S(12), ly, tmp + sst[k], sln[k], C(COL_TEXT_STRONG)); ly += mf->line_h; }
        explicit_bzero(tmp, sizeof tmp);
    } else {
        draw_rrect(x, y, w, ch, S(8), C(card));
        if (it->locked) draw_text(fs, x + S(12), text_y(fs, y, ch), "Locked", 6, C(COL_WARN));
        else if (pending) draw_text(fs, x + S(12), text_y(fs, y, ch), "Reading\xE2\x80\xA6", 10, C(COL_TEXT_FAINT));
        else { // dots
            f32 dr = roundf(S(3)), dx = x + S(14);
            for (int i = 0; i < 10; i++) { draw_rrect(dx, roundf(y + ch * 0.5f - dr), dr * 2, dr * 2, dr, C(COL_TEXT_DIM)); dx += dr * 2 + S(5); }
        }
    }
    { // show + copy (or unlock)
        f32 by = roundf(y + (MIN(ch, S(46)) - bsz) * 0.5f);
        Rect copy = {x + w - S(8) - bsz, by, bsz, bsz}, eye = {copy.x - bsz - S(4), by, bsz, bsz};
        if (it->locked) {
            const char *ul = "Unlock";
            f32 uw = chip_w(ul) + S(6);
            Rect u = {x + w - S(8) - uw, by, uw, bsz};
            draw_chip(u, ul, false);
            insp_btn_unlock = u;
        } else {
            bool eh = hovering(eye), chv = hovering(copy);
            if (eh) draw_rrect(eye.x, eye.y, eye.w, eye.h, S(6), C(COL_CONTROL_HOVER));
            if (chv) draw_rrect(copy.x, copy.y, copy.w, copy.h, S(6), C(COL_CONTROL_HOVER));
            draw_eye_icon(eye.x + bsz * 0.5f, eye.y + bsz * 0.5f, S(7.5f), C(eh ? COL_TEXT : COL_TEXT2), shown);
            draw_copy_icon(copy.x + (bsz - S(14)) * 0.5f, copy.y + (bsz - S(14)) * 0.5f, S(14), C(chv ? COL_TEXT : COL_TEXT2), chv ? COL_CONTROL_HOVER : card);
            insp_btn_reveal = eye;
            insp_btn_copy = copy;
        }
    }
    y += ch + roundf(S(6));
    {
        char k1[32], k2[32], hint[120];
        key_label_for(CMD_COPY_SECRET, k1, sizeof k1);
        key_label_for(CMD_REVEAL, k2, sizeof k2);
        if (!it->locked && (k1[0] || k2[0])) {
            snprintf(hint, sizeof hint, "%s%s%s%s%s", k1, k1[0] ? " copies" : "", k1[0] && k2[0] ? "  \xC2\xB7  " : "", k2, k2[0] ? (shown ? " hides" : " shows") : "");
            draw_text_fit(fs, x + S(2), y, w, hint, (int)strlen(hint), C(COL_TEXT_FAINT), 0);
        }
    }
    y += roundf(S(28));
    // details
    y = insp_section(x, y, "Details");
    insp_btn_acc = insp_row(x, &y, w, "Account", it->account, true);
    insp_btn_where = insp_row(x, &y, w, "Where", it->where, true);
    insp_row(x, &y, w, "Keyring", coll_label(it->coll), false);
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char d1[64], d2[64];
    fmt_date(it->modified, d1, sizeof d1, &tm_now);
    fmt_date(it->created, d2, sizeof d2, &tm_now);
    insp_row(x, &y, w, "Modified", d1, false);
    insp_row(x, &y, w, "Created", d2, false);
    y += roundf(S(14));
    // attributes
    if (it->nattrs) {
        y = insp_section(x, y, "Attributes");
        f32 ah = roundf(S(24));
        draw_rrect(x - S(6), y - S(4), w + S(12), it->nattrs * ah + S(8), S(8), C(rgb_mix(COL_PREVIEW_BG, COL_CONTROL, 0.35f)));
        f32 kw = 0;
        for (int i = 0; i < it->nattrs; i++) kw = MAX(kw, text_width(fs, it->attrs[i].k, (int)strlen(it->attrs[i].k)));
        kw = MIN(kw + S(12), w * 0.45f);
        for (int i = 0; i < it->nattrs; i++) {
            draw_text_fit(fs, x + S(2), text_y(fs, y, ah), kw - S(8), it->attrs[i].k, (int)strlen(it->attrs[i].k), C(COL_TEXT_FAINT), 0);
            draw_text_fit(mf, x + kw, text_y(mf, y, ah), w - kw, it->attrs[i].v, (int)strlen(it->attrs[i].v), C(COL_TEXT2), 0);
            y += ah;
        }
        y += roundf(S(18));
    }
    // edit / delete at the bottom
    f32 bh = roundf(S(32)), by = MAX(y, r.y + r.h - pad - bh);
    f32 ew = button_w("Edit"), dw = button_w("Delete");
    Rect eb = {x, by, ew, bh}, db = {x + ew + S(8), by, dw, bh};
    draw_button(eb, "Edit", false, false, !it->locked);
    if (!it->locked) insp_btn_edit = eb;
    draw_button(db, "Delete", true, true, true); // (a plain red, like the confirmation's button)
    insp_btn_delete = db;
    if (db.y + db.h > r.y + r.h) insp_btn_edit = insp_btn_delete = (Rect){0, 0, 0, 0}; // (scrolled out of the panel)
    (void)f;
    A.insp_content_h = by + bh + pad - y_top + pad;
    if (A.insp_content_h > r.h) { // scroll indicator
        f32 th = MAX(S(30), r.h * r.h / A.insp_content_h), ty = r.y + (r.h - th) * (A.insp_scroll / (A.insp_content_h - r.h));
        draw_rrect(r.x + r.w - S(7), ty + S(2), S(4), th - S(4), S(2), C(COL_SCROLL));
    }
    draw_clip_pop(cs);
}

// --- status bar -------------------------------------------------------------------

static void draw_status(void) {
    Rect s = A.r_status;
    draw_rect(s.x, s.y, s.w, s.h, C(COL_TOOLBAR));
    draw_rect(s.x, s.y, s.w, 1, C(COL_BORDER));
    Font *f = &A.font_small;
    f32 ty = text_y(f, s.y, s.h);
    char left[256], right[256], n1[32], n2[32];
    left[0] = right[0] = 0;
    fmt_count(n1, (u64)A.nview);
    int k = snprintf(left, sizeof left, "%s item%s", n1, A.nview == 1 ? "" : "s");
    if (A.nsel > 1) {
        fmt_count(n2, (u64)A.nsel);
        k += snprintf(left + k, sizeof left - k, "   \xC2\xB7   %s selected", n2);
    }
    if (A.search.buf[0]) snprintf(left + k, sizeof left - k, "   \xC2\xB7   matching \xE2\x80\x9C%.60s\xE2\x80\x9D", A.search.buf);
    draw_text(f, s.x + S(14), ty, left, (int)strlen(left), C(COL_TEXT_DIM));
    int left_secs = clip_seconds_left();
    u32 rc = COL_TEXT_FAINT;
    if (A.prompt_open) { snprintf(right, sizeof right, "Waiting for the keyring's password window\xE2\x80\xA6"); rc = COL_WARN; }
    else if (left_secs > 0) { snprintf(right, sizeof right, "Secret on the clipboard \xC2\xB7 cleared in %d s", left_secs); rc = COL_ACCENT; }
    else if (A.clip_owned) { snprintf(right, sizeof right, "Secret on the clipboard"); rc = COL_ACCENT; }
    else if (A.loading) {
        int got = 0;
        for (int i = 0; i < A.nitems; i++) got += !A.items[i]->pending;
        fmt_count(n1, (u64)got);
        fmt_count(n2, (u64)A.nitems);
        snprintf(right, sizeof right, "Reading items\xE2\x80\xA6 %s of %s", n1, n2);
    } else if (pending_any()) snprintf(right, sizeof right, "Working\xE2\x80\xA6");
    else if (A.loaded && A.ss_state == SS_READY) {
        fmt_count(n1, (u64)A.nitems);
        snprintf(right, sizeof right, "%s item%s in %d keyring%s \xC2\xB7 listed in %.0f ms", n1, A.nitems == 1 ? "" : "s", A.ncolls,
                 A.ncolls == 1 ? "" : "s", A.load_ms);
    }
    f32 rw = text_width(f, right, (int)strlen(right));
    draw_text(f, s.x + s.w - S(14) - rw, ty, right, (int)strlen(right), C(rc));
}

// --- overlays ---------------------------------------------------------------------

static void draw_dialog(void) {
    Dialog *d = &A.dlg;
    if (!d->open) return;
    draw_rect(0, 0, (f32)A.W, (f32)A.H, CA(0x000000, 0.45f));
    Font *f = &A.font, *tf = &A.font_brand;
    f32 w = roundf(MIN(S(480), A.W - S(40))), pad = roundf(S(22)), bs = roundf(S(40)), tx0 = pad + bs + roundf(S(14));
    int st[10], ln[10];
    int nl = wrap_text(f, d->msg, w - tx0 - pad, st, ln, 10);
    f32 lh = roundf(f->line_h * 1.3f), bh = roundf(S(32));
    f32 head = MAX(bs, roundf(tf->line_h + S(6)) + nl * lh);
    f32 h = pad + head + roundf(S(22)) + bh + pad;
    f32 x = roundf((A.W - w) * 0.5f), y = roundf((A.H - h) * 0.42f);
    d->r = (Rect){x, y, w, h};
    draw_rrect(x, y, w, h, S(12), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(12), 1, C(COL_POPUP_BORDER));
    u32 col = d->danger ? COL_DANGER : COL_ACCENT;
    f32 bx = x + pad, by = y + pad;
    draw_rrect(bx, by, bs, bs, bs * 0.5f, C(rgb_mix(COL_POPUP, col, 0.18f)));
    draw_text_fit(tf, bx, text_y(tf, by, bs), bs, d->danger ? "!" : "?", 1, C(col), 2);
    f32 tx = x + tx0, cy = y + pad;
    draw_text_fit(tf, tx, cy - S(1), x + w - pad - tx, d->title, (int)strlen(d->title), C(COL_TEXT_STRONG), 0);
    cy += roundf(tf->line_h + S(6));
    for (int i = 0; i < nl; i++) {
        draw_text(f, tx, cy, d->msg + st[i], ln[i], C(COL_TEXT2));
        cy += lh;
    }
    f32 bxr = x + w - pad;
    for (int i = d->nbuttons - 1; i >= 0; i--) {
        f32 bw = button_w(d->buttons[i]);
        bxr -= bw;
        Rect r = {bxr, y + h - pad - bh, bw, bh};
        d->rb[i] = r;
        draw_button(r, d->buttons[i], i == d->focus, d->danger && i == d->def, true);
        bxr -= roundf(S(8));
    }
}

static void draw_menu(void) {
    Menu *m = &A.menu;
    if (!m->open) return;
    Font *f = &A.font, *fs = &A.font_small;
    Rect r = m->r;
    draw_rrect(r.x + S(1), r.y + S(3), r.w, r.h, S(9), CA(0x000000, 0.25f));
    draw_rrect(r.x, r.y, r.w, r.h, S(9), C(COL_POPUP));
    draw_rrect_outline(r.x, r.y, r.w, r.h, S(9), 1, C(COL_POPUP_BORDER));
    f32 yy = r.y + roundf(S(5));
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        f32 h = menu_item_h(it);
        if (it->sep) {
            draw_rect(r.x + S(10), roundf(yy + h * 0.5f), r.w - S(20), 1, C(COL_POPUP_BORDER));
        } else {
            bool hv = i == m->hover && it->enabled;
            if (hv) draw_rrect(r.x + S(5), yy, r.w - S(10), h, S(6), C(it->danger ? COL_DANGER_FILL : COL_SEL_FOCUS));
            Color tc = !it->enabled ? C(COL_TEXT_FAINT) : it->danger ? C(hv ? 0xFFFFFF : COL_DANGER_RED) : C(COL_TEXT);
            draw_text(f, r.x + S(16), text_y(f, yy, h), it->label, (int)strlen(it->label), tc);
            int sl = (int)strlen(it->shortcut);
            f32 sw = text_width(fs, it->shortcut, sl);
            draw_text(fs, r.x + r.w - S(16) - sw, text_y(fs, yy, h), it->shortcut, sl, hv ? C(COL_SEL_DIM) : C(COL_TEXT_FAINT));
        }
        yy += h;
    }
}

static u64 toast_life(void) { return 3000000000ull + (u64)MIN((int)strlen(A.toast), 200) * 30000000ull; }

static bool toast_fading(void) {
    if (!A.toast[0]) return false;
    u64 age = now_ns() - A.toast_t, life = toast_life();
    return age + 400000000ull > life && age < life + 100000000ull;
}

static void draw_toast(void) {
    if (!A.toast[0]) return;
    u64 life = toast_life(), age = now_ns() - A.toast_t;
    if (age > life) { A.toast[0] = 0; return; }
    f32 a = age > life - 400000000ull ? (f32)(life - age) / 400000000.f : 1.f;
    Font *f = &A.font;
    f32 maxw = MIN(S(560), A.r_content.w - S(40)), padx = roundf(S(16)), lh = roundf(f->line_h * 1.25f);
    int st[4], ln[4];
    int nl = wrap_text(f, A.toast, maxw - padx * 2, st, ln, 4);
    f32 tw = 0;
    for (int i = 0; i < nl; i++) tw = MAX(tw, text_width(f, A.toast + st[i], ln[i]));
    f32 w = roundf(MIN(maxw, tw + padx * 2)), h = roundf(MAX(S(34), nl * lh + S(16)));
    f32 x = roundf(A.r_content.x + (A.r_content.w - w) * 0.5f), y = roundf(A.r_status.y - h - S(16));
    g_alpha = a;
    draw_rrect(x, y, w, h, S(9), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(9), 1, C(COL_POPUP_BORDER));
    f32 ty = roundf(y + (h - nl * lh) * 0.5f + (lh - f->line_h) * 0.5f);
    for (int i = 0; i < nl; i++) draw_text_fit(f, x + padx, ty + i * lh, w - padx * 2, A.toast + st[i], ln[i], C(COL_TEXT), 0);
    g_alpha = 1.f;
}

// Text with matched characters (fuzzy positions) highlighted; elides with "..." past maxw.
static f32 draw_text_hl(Font *f, f32 x, f32 y, f32 maxw, const char *s, int len, const u16 *pos, int npos, Color c, Color hc) {
    f32 bl = roundf(y + f->ascent), pen = roundf(x), lim = x + maxw - text_width(f, ELLIPSIS, 3);
    bool over = text_width(f, s, len) > maxw;
    const char *p = s, *end = s + len;
    while (p < end) {
        int bi = (int)(p - s);
        Glyph *g = font_glyph(f, utf8_decode(&p, end));
        if (over && pen + g->adv > lim) { draw_text(f, pen, y, ELLIPSIS, 3, c); return pen; }
        bool hl = false;
        for (int k = 0; k < npos && !hl; k++) hl = pos[k] == bi;
        if (g->w) draw_glyph(g, roundf(pen) + g->bx, bl - g->by, hl ? hc : c);
        pen += g->adv;
    }
    return pen;
}

static void draw_palette(void) {
    if (!A.pal_open) return;
    draw_rect(0, 0, (f32)A.W, (f32)A.H, CA(0x000000, 0.35f));
    Font *f = &A.font, *fs = &A.font_small;
    f32 w = roundf(MIN(S(680), A.W - S(60))), ih = roundf(S(44)), rh = roundf(S(40));
    int vis = MIN(A.pal_n, 10);
    f32 h = ih + (A.pal_n ? vis * rh + S(12) : roundf(S(44)));
    f32 x = roundf((A.W - w) * 0.5f), y = roundf(A.r_content.y + S(24));
    A.pal_r = (Rect){x, y, w, h};
    A.pal_list = (Rect){x, y + ih + S(6), w, vis * rh};
    draw_rrect(x, y, w, h, S(12), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(12), 1, C(COL_CHIP));
    Rect in = {x + S(10), y + S(8), w - S(20), ih - S(10)};
    f32 r = roundf(S(5)), cx = in.x + S(12), cy = in.y + in.h * 0.5f - S(1);
    draw_rrect_outline(cx - r, cy - r, r * 2, r * 2, r, MAX(1.f, S(1.5f)), C(COL_ACCENT));
    draw_line(cx + r * 0.7f, cy + r * 0.7f, cx + r * 1.6f, cy + r * 1.6f, MAX(1.5f, S(1.6f)), C(COL_ACCENT));
    Rect field = {in.x + S(26), in.y, in.w - S(26), in.h};
    te_draw(&A.pal_te, f, field, roundf(S(4)), C(COL_TEXT), true);
    bool cmds = A.pal_te.buf[0] == '>';
    if (!A.pal_te.buf[0]) {
        const char *ph = "Go to item\xE2\x80\xA6   (> commands)";
        draw_text(f, field.x + S(6), text_y(f, field.y, field.h), ph, (int)strlen(ph), C(COL_TEXT_FAINT));
    }
    draw_rect(x + 1, y + ih + S(1), w - 2, 1, C(COL_CONTROL));
    if (!A.pal_n) {
        const char *m = cmds ? "No matching commands" : "No matching items";
        draw_text(fs, x + S(20), text_y(fs, y + ih, S(44)), m, (int)strlen(m), C(COL_TEXT_DIM));
        return;
    }
    if (A.pal_sel < A.pal_top) A.pal_top = A.pal_sel;
    if (A.pal_sel >= A.pal_top + vis) A.pal_top = A.pal_sel - vis + 1;
    for (int vi = 0; vi < vis; vi++) {
        int i = A.pal_top + vi;
        if (i >= A.pal_n) break;
        PalItem *p = &A.pal[i];
        f32 ry = A.pal_list.y + vi * rh;
        bool sel = i == A.pal_sel;
        if (sel) draw_rrect(x + S(6), ry, w - S(12), rh, S(7), C(COL_SEL_FOCUS));
        f32 is = roundf(S(18)), ix = x + S(18), iy = roundf(ry + (rh - is) * 0.5f);
        Color tc = C(sel ? COL_SEL_TEXT : COL_TEXT), hc = C(sel ? COL_SEL_HL : COL_ACCENT), dc = C(sel ? COL_SEL_DIM : COL_TEXT_FAINT);
        f32 tx = ix + is + S(12), avail = w - (tx - x) - S(20);
        const char *hint;
        char hbuf[200];
        if (p->kind == 1) {
            draw_rrect(ix, iy, is, is, S(4), C(sel ? COL_SEL_FOCUS : COL_CHIP));
            draw_chevron(ix + is * 0.45f, iy + is * 0.5f, S(7), 1, MAX(1.f, S(1.4f)), C(COL_TEXT));
            key_label_for(p->cmd, hbuf, sizeof hbuf);
            hint = hbuf;
        } else {
            draw_item_icon(p->item, ix, iy, is, p->item->locked ? 0.55f : 1.f);
            snprintf(hbuf, sizeof hbuf, "%.80s", coll_label(p->item->coll));
            hint = hbuf;
        }
        f32 hw = text_width(fs, hint, (int)strlen(hint));
        draw_text(fs, x + w - S(20) - hw, text_y(fs, ry, rh), hint, (int)strlen(hint), dc);
        avail -= hw + S(16);
        f32 ty = text_y(f, ry, rh);
        if (p->kind == 1) {
            const char *t = cmd_title(p->cmd);
            draw_text_hl(f, tx, ty, avail, t, (int)strlen(t), p->pos, p->npos, tc, hc);
            continue;
        }
        const char *lab = p->item->label[0] ? p->item->label : "(no name)";
        f32 ex = draw_text_hl(f, tx, ty, avail, lab, (int)strlen(lab), p->pos, p->npos, tc, hc);
        const char *sub = p->item->account[0] ? p->item->account : p->item->where;
        f32 rem = avail - (ex - tx) - S(12);
        if (rem > S(40) && sub[0]) draw_text_fit(fs, ex + S(12), text_y(fs, ry, rh), rem, sub, (int)strlen(sub), dc, 0);
    }
}

static void draw_overlay(void) {
    char buf[160];
    snprintf(buf, sizeof buf, "frame %.2f ms  view %.2f ms  scale %.2f  %dx%d  items %d", A.frame_ms, A.view_ms, A.scale, A.W, A.H, A.nitems);
    Font *f = &A.font_small;
    int len = (int)strlen(buf);
    f32 w = text_width(f, buf, len) + S(16), h = S(24);
    f32 x = A.r_content.x + A.r_content.w - w - S(10), y = A.r_content.y + S(40);
    draw_rrect(x, y, w, h, S(6), CA(0x000000, 0.75f));
    draw_text(f, x + S(8), text_y(f, y, h), buf, len, C(0x8FD45A));
}

static void frame_draw(void) {
    draw_clip(NULL);
    g_alpha = 1.f;
    SDL_SetRenderDrawColor(A.ren, (COL_BG >> 16) & 255, (COL_BG >> 8) & 255, COL_BG & 255, 255);
    SDL_RenderClear(A.ren);
    // While a menu, the palette, a sheet or a dialog is open, nothing underneath reacts to the mouse.
    bool mouse_in = A.mouse_in;
    if (A.menu.open || A.pal_open || sheet_open() || A.dlg.open) A.mouse_in = false;
    draw_content();
    draw_inspector();
    draw_sidebar();
    draw_brand();
    draw_topbar();
    draw_window_controls();
    draw_status();
    draw_toast();
    A.mouse_in = mouse_in;
    draw_menu();
    draw_palette();
    A.mouse_in = mouse_in && !A.dlg.open;
    sheet_draw();
    A.mouse_in = mouse_in;
    draw_dialog();
    if (A.show_overlay) draw_overlay();
    draw_flush();
}
