// input.c - keyboard, text input, mouse (list, header, sidebar, top bar, inspector, overlays), SDL event dispatch

static Rect insp_btn_reveal, insp_btn_copy, insp_btn_unlock, insp_btn_edit, insp_btn_delete, insp_btn_acc, insp_btn_where; // render.c
static Rect empty_btn; // the button of an empty / locked / no-service state (render.c)
static int empty_btn_cmd;
static Rect side_lock_btn[64]; // per sidebar row: its lock / unlock button while hovered (render.c)
static Rect side_add_btn;      // "+" on the Keyrings header

static void list_click(int row, SDL_Keymod mod) {
    if (row < 0) {
        if (!(mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT))) { for (int i = 0; i < A.nview; i++) A.view[i]->sel = false; sel_count_update(); }
        return;
    }
    if (mod & SDL_KMOD_SHIFT) {
        int a = A.anchor >= 0 && A.anchor < A.nview ? A.anchor : A.cursor;
        if (a < 0) a = row;
        if (!(mod & SDL_KMOD_CTRL)) for (int i = 0; i < A.nview; i++) A.view[i]->sel = false;
        for (int i = MIN(a, row); i <= MAX(a, row); i++) A.view[i]->sel = true;
        A.cursor = row;
    } else if (mod & SDL_KMOD_CTRL) {
        A.view[row]->sel = !A.view[row]->sel;
        A.cursor = row;
        A.anchor = row;
    } else select_only(row);
    A.cursor_hint = A.cursor;
    sel_count_update();
}

static void cursor_move(int to, SDL_Keymod mod) {
    if (!A.nview) return;
    to = CLAMP(to, 0, A.nview - 1);
    if (mod & SDL_KMOD_SHIFT) {
        if (A.anchor < 0 || A.anchor >= A.nview) A.anchor = A.cursor >= 0 ? A.cursor : to;
        for (int i = 0; i < A.nview; i++) A.view[i]->sel = i >= MIN(A.anchor, to) && i <= MAX(A.anchor, to);
        A.cursor = to;
        sel_count_update();
    } else select_only(to);
    A.cursor_hint = A.cursor;
    A.ensure_visible = true;
    if (A.rev_item && cursor_item() && strcmp(A.rev_item, cursor_item()->path)) reveal_clear(); // shown secret: only while on it
}

static int page_rows(void) { return MAX(1, (int)(A.r_list.h / A.row_h) - 1); }

static void search_changed(void) {
    A.cursor_hint = 0;
    A.scroll_target = 0;
    view_rebuild();
    if (A.nview) select_only(0);
}

static void on_key(SDL_KeyboardEvent *k) {
    SDL_Keycode key = k->key;
    SDL_Keymod mod = k->mod;
    bool ctrl = mod & SDL_KMOD_CTRL;
    A.redraw = true;
    if (A.dlg.open) {
        Dialog *d = &A.dlg;
        if (key == SDLK_ESCAPE) dialog_close(d->cancel);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) dialog_close(d->focus);
        else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_TAB) d->focus = (d->focus + (key == SDLK_LEFT ? d->nbuttons - 1 : 1)) % d->nbuttons;
        return;
    }
    if (sheet_key(key, mod)) return;
    if (A.menu.open) {
        Menu *m = &A.menu;
        if (key == SDLK_ESCAPE) m->open = false;
        else if (key == SDLK_UP || key == SDLK_DOWN) {
            int d = key == SDLK_UP ? -1 : 1, i = m->hover;
            for (int n = 0; n < m->n; n++) {
                i = (i + d + m->n) % m->n;
                if (!m->items[i].sep && m->items[i].enabled) break;
            }
            m->hover = i;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) menu_pick(m->hover);
        return;
    }
    if (A.pal_open) {
        if (key == SDLK_ESCAPE) A.pal_open = false;
        else if (key == SDLK_UP) A.pal_sel = MAX(0, A.pal_sel - 1);
        else if (key == SDLK_DOWN) A.pal_sel = MIN(A.pal_n - 1, A.pal_sel + 1);
        else if (key == SDLK_PAGEUP) A.pal_sel = MAX(0, A.pal_sel - 10);
        else if (key == SDLK_PAGEDOWN) A.pal_sel = MIN(A.pal_n - 1, A.pal_sel + 10);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) palette_accept(A.pal_sel);
        else if (te_key(&A.pal_te, key, mod)) palette_update();
        return;
    }
    if (A.focus == FOCUS_SEARCH) {
        if (key == SDLK_ESCAPE) {
            if (A.search.buf[0]) { te_set(&A.search, "", 0, 0); search_changed(); }
            else A.focus = FOCUS_LIST;
            return;
        }
        if (key == SDLK_DOWN || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_TAB) {
            A.focus = FOCUS_LIST;
            if (A.nview && A.cursor < 0) select_only(0);
            return;
        }
        if (key == SDLK_UP || key == SDLK_PAGEDOWN || key == SDLK_PAGEUP) { A.focus = FOCUS_LIST; }
        else {
            char before[sizeof A.search.buf];
            memcpy(before, A.search.buf, sizeof before);
            if (te_key(&A.search, key, mod)) {
                if (strcmp(before, A.search.buf)) search_changed();
                return;
            }
            if (!(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) && key >= 0x20 && key < 0x7F) return; // (typed: text input)
        }
    }
    int cmd = key_command(key, mod);
    if (cmd == CMD_COPY_SECRET && A.focus == FOCUS_SEARCH) cmd = CMD_NONE; // (handled as text copy above)
    if (cmd != CMD_NONE) { run_command(cmd, -1); return; }
    switch (key) {
    case SDLK_UP: cursor_move(A.cursor - 1, mod); break;
    case SDLK_DOWN: cursor_move(A.cursor + 1, mod); break;
    case SDLK_PAGEUP: cursor_move(A.cursor - page_rows(), mod); break;
    case SDLK_PAGEDOWN: cursor_move(A.cursor + page_rows(), mod); break;
    case SDLK_HOME: cursor_move(0, mod); break;
    case SDLK_END: cursor_move(A.nview - 1, mod); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: run_command(CMD_EDIT, -1); break;
    case SDLK_SPACE: run_command(CMD_INSPECTOR, -1); break;
    case SDLK_ESCAPE:
        if (A.rev_item) reveal_clear();
        else if (A.search.buf[0]) { te_set(&A.search, "", 0, 0); search_changed(); }
        else if (A.nsel > 1) select_only(A.cursor);
        break;
    case SDLK_BACKSPACE:
        if (A.search.buf[0]) { A.focus = FOCUS_SEARCH; te_key(&A.search, key, mod); search_changed(); }
        break;
    default:
        if (ctrl && key >= SDLK_1 && key <= SDLK_9) { // Ctrl+1..9: keyring n
            int c = (int)(key - SDLK_1);
            if (c < A.ncolls) side_select(c);
        }
        break;
    }
}

static void on_text(const char *s) {
    A.redraw = true;
    if (A.dlg.open || A.menu.open) return;
    if (sheet_open()) { sheet_text(s); return; }
    if (A.pal_open) { te_insert(&A.pal_te, s); palette_update(); return; }
    if (s[0] == ' ' && !s[1] && A.focus != FOCUS_SEARCH) return; // (Space toggles the details panel)
    if (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) return;
    if (A.focus != FOCUS_SEARCH) { // typing in the list filters it
        A.focus = FOCUS_SEARCH;
        A.search.anchor = A.search.caret = (int)strlen(A.search.buf);
    }
    te_insert(&A.search, s);
    search_changed();
}

// --- mouse -----------------------------------------------------------------

static int header_edge_at(f32 x) { // a column's right edge (resize), or -1
    for (int i = 0; i < A.ncols_vis - 1; i++) {
        int c = A.cols_vis[i + 1];
        if (fabsf(x - A.colx0[c]) <= S(4)) return c; // dragging the left edge of column c resizes the one before... (c's own width)
    }
    return -1;
}

static int header_col_at(f32 x) {
    for (int i = 0; i < A.ncols_vis; i++) {
        int c = A.cols_vis[i];
        if (x >= A.colx0[c] && x < A.colx1[c]) return c;
    }
    return -1;
}

static bool side_edge_at(f32 x, f32 y) { return y > A.r_side.y - A.r_brand.h && fabsf(x - A.r_side.w) <= S(4); }
static bool insp_edge_at(f32 x, f32 y) { return A.insp_on && y > A.r_insp.y && fabsf(x - A.r_insp.x) <= S(4); }

static f32 scroll_thumb(f32 *ty) {
    f64 total = (f64)A.nview * A.row_h + S(8);
    if (total <= A.r_list.h) { *ty = A.r_track.y; return 0; }
    f32 th = (f32)MAX((f64)S(30), A.r_list.h * A.r_list.h / total);
    *ty = A.r_track.y + (f32)((A.r_track.h - th) * (A.scroll / max_scroll()));
    return th;
}

static void on_mouse_down(SDL_MouseButtonEvent *b) {
    f32 d = SDL_GetWindowPixelDensity(A.win), x = b->x * d, y = b->y * d;
    A.mx = x;
    A.my = y;
    A.redraw = true;
    SDL_Keymod mod = SDL_GetModState();
    if (A.dlg.open) {
        Dialog *dl = &A.dlg;
        for (int i = 0; i < dl->nbuttons; i++)
            if (in_rect(dl->rb[i], x, y)) { dialog_close(i); return; }
        if (!in_rect(dl->r, x, y)) dialog_close(dl->cancel);
        return;
    }
    if (sheet_open()) { sheet_mouse_down(b, x, y); return; }
    if (A.menu.open) {
        int i = menu_at(x, y);
        if (in_rect(A.menu.r, x, y)) { if (i >= 0) menu_pick(i); return; }
        A.menu.open = false;
        if (b->button != SDL_BUTTON_RIGHT) return;
    }
    if (A.pal_open) {
        if (in_rect(A.pal_list, x, y)) {
            int i = A.pal_top + (int)((y - A.pal_list.y) / roundf(S(40)));
            if (i < A.pal_n) palette_accept(i);
        } else if (!in_rect(A.pal_r, x, y)) A.pal_open = false;
        return;
    }
    if (titlebar_mouse_down(b, x, y)) return;
    // resize handles
    if (b->button == SDL_BUTTON_LEFT && side_edge_at(x, y)) { A.drag = DRAG_SIDEBAR; return; }
    if (b->button == SDL_BUTTON_LEFT && insp_edge_at(x, y)) { A.drag = DRAG_INSPECTOR; return; }
    // top bar
    if (in_rect(A.r_search, x, y)) {
        A.focus = FOCUS_SEARCH;
        if (b->clicks >= 2) { A.search.anchor = 0; A.search.caret = (int)strlen(A.search.buf); }
        else A.search.caret = A.search.anchor = te_hit(&A.search, &A.font, (Rect){A.r_search.x + S(26), A.r_search.y, A.r_search.w - S(52), A.r_search.h}, roundf(S(4)), x);
        Rect clr = {A.r_search.x + A.r_search.w - S(26), A.r_search.y, S(26), A.r_search.h};
        if (A.search.buf[0] && in_rect(clr, x, y)) { te_set(&A.search, "", 0, 0); search_changed(); }
        return;
    }
    if (A.focus == FOCUS_SEARCH && !in_rect(A.r_search, x, y)) A.focus = FOCUS_LIST;
    if (in_rect(A.r_btn_new, x, y)) { run_command(CMD_NEW_ITEM, -1); return; }
    if (in_rect(A.r_btn_lock, x, y)) { run_command(CMD_LOCK_ALL, -1); return; }
    if (in_rect(A.r_btn_insp, x, y)) { run_command(CMD_INSPECTOR, -1); return; }
    // sidebar
    if (in_rect(A.r_side, x, y)) {
        A.focus = FOCUS_LIST;
        if (in_rect(side_add_btn, x, y)) { run_command(CMD_NEW_KEYRING, -1); return; }
        for (int i = 0; i < A.nside_rows; i++) {
            SideRow *r = &A.side_rows[i];
            if (!in_rect(r->r, x, y)) continue;
            if (r->kind == SR_HEADER) { if (b->button == SDL_BUTTON_RIGHT) menu_coll(-1, x, y); return; }
            if (r->kind == SR_COLL && in_rect(side_lock_btn[i], x, y) && b->button == SDL_BUTTON_LEFT) {
                action_lock_coll(r->coll, !A.colls[r->coll].locked);
                return;
            }
            if (b->button == SDL_BUTTON_RIGHT) {
                if (r->kind == SR_COLL) { side_select(r->coll); menu_coll(r->coll, x, y); }
                else menu_coll(-1, x, y);
                return;
            }
            side_select(r->kind == SR_ALL ? -1 : r->coll);
            if (r->kind == SR_COLL && A.colls[r->coll].locked && b->clicks >= 2) action_lock_coll(r->coll, false);
            return;
        }
        if (b->button == SDL_BUTTON_RIGHT) menu_coll(-1, x, y);
        return;
    }
    // inspector
    if (A.insp_on && in_rect(A.r_insp, x, y)) {
        Item *it = cursor_item();
        if (in_rect(insp_btn_reveal, x, y)) reveal_toggle(it);
        else if (in_rect(insp_btn_copy, x, y)) copy_secret(it);
        else if (in_rect(insp_btn_unlock, x, y)) action_unlock_item(it);
        else if (in_rect(insp_btn_edit, x, y)) run_command(CMD_EDIT, -1);
        else if (in_rect(insp_btn_delete, x, y)) run_command(CMD_DELETE, -1);
        else if (in_rect(insp_btn_acc, x, y) && it) copy_text("account name", it->account);
        else if (in_rect(insp_btn_where, x, y) && it) copy_text("address", it->where);
        return;
    }
    if (in_rect(empty_btn, x, y) && empty_btn_cmd) { run_command(empty_btn_cmd, -1); return; }
    // header
    if (in_rect(A.r_header, x, y)) {
        if (b->button == SDL_BUTTON_RIGHT) { menu_header(x, y); return; }
        int e = header_edge_at(x);
        if (e >= 0) { // resize the column left of this edge
            int left = -1;
            for (int i = 1; i < A.ncols_vis; i++)
                if (A.cols_vis[i] == e) left = A.cols_vis[i - 1];
            if (left > COL_NAME) { A.drag = DRAG_COL; A.drag_col = left; A.drag_start = x; A.drag_off = A.colw[left]; }
            else if (left == COL_NAME) { A.drag = DRAG_COL; A.drag_col = e; A.drag_start = x; A.drag_off = -A.colw[e]; } // name: shrink the next
            return;
        }
        int c = header_col_at(x);
        if (c >= 0) run_command(CMD_SORT, c);
        return;
    }
    // scrollbar
    if (in_rect(A.r_track, x, y) && max_scroll() > 0) {
        f32 ty, th = scroll_thumb(&ty);
        if (y >= ty && y < ty + th) { A.drag = DRAG_SCROLL; A.drag_off = y - ty; }
        else A.scroll_target = CLAMP(A.scroll_target + (y < ty ? -1 : 1) * A.r_list.h, 0.0, max_scroll());
        return;
    }
    // list
    if (in_rect(A.r_list, x, y)) {
        A.focus = FOCUS_LIST;
        int row = row_at(y);
        if (b->button == SDL_BUTTON_RIGHT) {
            if (row >= 0 && !A.view[row]->sel) select_only(row);
            if (row >= 0) menu_items(x, y);
            return;
        }
        if (b->button == SDL_BUTTON_MIDDLE && row >= 0) { select_only(row); copy_secret(A.view[row]); return; }
        if (b->button != SDL_BUTTON_LEFT) return;
        list_click(row, mod);
        if (A.rev_item && cursor_item() && strcmp(A.rev_item, cursor_item()->path)) reveal_clear();
        if (row >= 0 && b->clicks == 2 && !(mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT))) {
            Item *it = A.view[row];
            if (it->locked) action_unlock_item(it);
            else run_command(CMD_EDIT, -1);
        }
    }
}

static void on_mouse_up(SDL_MouseButtonEvent *b) {
    (void)b;
    if (A.drag != DRAG_NONE) A.settings_dirty = A.settings_dirty || A.drag == DRAG_SIDEBAR || A.drag == DRAG_INSPECTOR || A.drag == DRAG_COL;
    A.drag = DRAG_NONE;
    g_sheet_drag_sel = false;
    A.redraw = true;
}

static void on_mouse_motion(SDL_MouseMotionEvent *m) {
    f32 d = SDL_GetWindowPixelDensity(A.win), x = m->x * d, y = m->y * d;
    bool moved = fabsf(x - A.mx) >= 0.5f || fabsf(y - A.my) >= 0.5f;
    A.mx = x;
    A.my = y;
    A.mouse_in = true;
    if (g_sheet_drag_sel) sheet_mouse_motion(x);
    switch (A.drag) {
    case DRAG_SIDEBAR: A.side_w = x / A.scale; layout(); A.redraw = true; return;
    case DRAG_INSPECTOR: A.insp_w = ((f32)A.W - x) / A.scale; layout(); A.redraw = true; return;
    case DRAG_COL:
        if (A.drag_off >= 0) A.colw[A.drag_col] = CLAMP(A.drag_off + (x - A.drag_start) / A.scale, 60.f, 600.f);
        else A.colw[A.drag_col] = CLAMP(-A.drag_off - (x - A.drag_start) / A.scale, 60.f, 600.f);
        layout();
        A.redraw = true;
        return;
    case DRAG_SCROLL: {
        f32 ty, th = scroll_thumb(&ty);
        f32 room = A.r_track.h - th;
        if (room > 0) A.scroll = A.scroll_target = CLAMP((f64)(y - A.drag_off - A.r_track.y) / room * max_scroll(), 0.0, max_scroll());
        A.redraw = true;
        return;
    }
    default: break;
    }
    if (A.menu.open) {
        int h = menu_at(x, y);
        if (h != A.menu.hover) { A.menu.hover = h; A.redraw = true; }
    }
    if (A.pal_open && in_rect(A.pal_list, x, y)) {
        int i = A.pal_top + (int)((y - A.pal_list.y) / roundf(S(40)));
        if (i < A.pal_n && i != A.pal_sel && moved) { A.pal_sel = i; A.redraw = true; }
    }
    SDL_Cursor *cur = A.cur_default;
    if (!A.dlg.open && !sheet_open() && !A.menu.open && !A.pal_open) {
        if (side_edge_at(x, y) || insp_edge_at(x, y) || (in_rect(A.r_header, x, y) && header_edge_at(x) >= 0)) cur = A.cur_ew;
        else if (in_rect(A.r_search, x, y)) cur = A.cur_text;
    } else if (sheet_open()) {
        for (int i = g_sheet.nhits - 1; i >= 0; i--)
            if (in_rect(g_sheet.hits[i].r, x, y)) { if (g_sheet.hits[i].id == H_FIELD) cur = A.cur_text; break; }
    }
    SDL_SetCursor(cur);
    if (moved) A.redraw = true; // hover effects
}

static void on_wheel(SDL_MouseWheelEvent *w) {
    f32 dy = w->y;
    if (w->direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
    A.redraw = true;
    if (A.dlg.open) return;
    if (sheet_open()) { sheet_wheel(dy); return; }
    if (A.pal_open) {
        A.pal_sel = CLAMP(A.pal_sel - (int)roundf(dy), 0, MAX(0, A.pal_n - 1));
        return;
    }
    if (SDL_GetModState() & SDL_KMOD_CTRL) { zoom_set(A.zoom + (dy > 0 ? 0.1f : -0.1f)); return; }
    if (A.insp_on && in_rect(A.r_insp, A.mx, A.my)) { A.insp_scroll -= dy * S(48); return; }
    if (in_rect(A.r_list, A.mx, A.my) || in_rect(A.r_header, A.mx, A.my)) {
        A.scroll_target = CLAMP(A.scroll_target - (f64)dy * A.row_h * 3, 0.0, max_scroll());
        if (!A.smooth_scroll) A.scroll = A.scroll_target;
    }
}

static void handle_event(SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_QUIT: A.quit = true; break;
    case SDL_EVENT_KEY_DOWN: A.last_input = now_ns(); A.idle_locked = false; on_key(&e->key); break;
    case SDL_EVENT_TEXT_INPUT: A.last_input = now_ns(); on_text(e->text.text); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: A.last_input = now_ns(); A.idle_locked = false; on_mouse_down(&e->button); break;
    case SDL_EVENT_MOUSE_BUTTON_UP: on_mouse_up(&e->button); break;
    case SDL_EVENT_MOUSE_MOTION: on_mouse_motion(&e->motion); break;
    case SDL_EVENT_MOUSE_WHEEL: A.last_input = now_ns(); on_wheel(&e->wheel); break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: A.mouse_in = false; A.redraw = true; break;
    case SDL_EVENT_WINDOW_MOUSE_ENTER: A.mouse_in = true; A.redraw = true; break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
        SDL_GetWindowSizeInPixels(A.win, &A.W, &A.H);
        layout();
        A.redraw = true;
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        f32 s = SDL_GetWindowDisplayScale(A.win);
        if (s > 0 && fabsf(s - A.dpi_scale) > 0.01f) {
            A.dpi_scale = s;
            f32 z = A.zoom;
            A.zoom = -1;
            zoom_set(z);
            A.toast[0] = 0;
        }
        break;
    }
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        A.redraw = true;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: A.quit = true; break;
    default:
        if (e->type == g_wake_event) A.redraw = true;
        break;
    }
}
