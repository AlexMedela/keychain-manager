// clip.c - copying secrets: offered from locked memory, marked as passwords (clipboard managers that know the
// hint keep them out of their history), and cleared after a while if the clipboard still holds them

static const char *const g_clip_mimes[] = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "TEXT", "STRING",
                                           "x-kde-passwordManagerHint"};

static const void *clip_data(void *ud, const char *mime, size_t *size) {
    (void)ud;
    if (mime && !strcmp(mime, "x-kde-passwordManagerHint")) { *size = 6; return "secret"; }
    if (!A.clip.p) { *size = 0; return NULL; }
    *size = A.clip.n;
    return A.clip.p;
}

// Another program took the clipboard (or we cleared it): our copy goes.
static void clip_cleanup(void *ud) {
    (void)ud;
    A.clip_owned = false;
    A.clip_until = 0;
    sec_free(&A.clip);
    A.redraw = true;
}

static void clip_secret(SecBuf *s, const char *what) {
    if (!s->p || !s->n) { toast("This item's secret is empty"); return; }
    if (A.clip_owned) SDL_ClearClipboardData(); // (runs clip_cleanup for the previous one)
    sec_move(&A.clip, s);
    snprintf(A.clip_what, sizeof A.clip_what, "%s", what && what[0] ? what : "secret");
    if (!SDL_SetClipboardData(clip_data, clip_cleanup, NULL, (const char **)g_clip_mimes, ARRAY_COUNT(g_clip_mimes))) {
        sec_free(&A.clip);
        toast("Couldn't copy: %s", SDL_GetError());
        return;
    }
    A.clip_owned = true;
    A.clip_until = A.clip_secs > 0 ? now_ns() + (u64)A.clip_secs * 1000000000ull : 0;
    if (A.clip_secs > 0) toast("Copied the secret of \xE2\x80\x9C%.120s\xE2\x80\x9D: cleared in %d s", A.clip_what, A.clip_secs);
    else toast("Copied the secret of \xE2\x80\x9C%.120s\xE2\x80\x9D", A.clip_what);
}

static int clip_seconds_left(void) {
    if (!A.clip_owned || !A.clip_until) return -1;
    u64 now = now_ns();
    return now >= A.clip_until ? 0 : (int)((A.clip_until - now + 999999999ull) / 1000000000ull);
}

static void clip_clear(void) {
    if (A.clip_owned) SDL_ClearClipboardData();
    clip_cleanup(NULL);
}

static void clip_tick(u64 now) {
    if (A.clip_owned && A.clip_until && now >= A.clip_until) {
        clip_clear();
        toast("Cleared the copied secret from the clipboard");
    }
}

// Quitting: a secret we put on the clipboard doesn't outlive us.
static void clip_quit(void) {
    if (A.clip_owned) clip_clear();
}
