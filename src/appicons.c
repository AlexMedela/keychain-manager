// appicons.c - the icons of the apps that saved the secrets (Brave, Chromium, Proton Mail...): the app's desktop
// entry (by app name, id, window class or command) and its icon from the icon theme, found and decoded on a worker
// thread, kept at 64 px and scaled into the atlas when drawn. Items without a match keep their kind's icon.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop

#define APPICON_BASE 0x10000 // icon ids of app icon slot i: APPICON_BASE + i
#define APPICON_PX 64

typedef struct {
    char key[96];
    int state; // 0 looking, 1 found, 2 none
    u8 *rgba;  // APPICON_PX x APPICON_PX
} AppIcon;

static AppIcon *g_appicons;
static int g_nappicons;

// --- desktop entries (worker side, built once) ----------------------------------------------

typedef struct { char id[128], name[128], first[64], wm[128], exec[128], icon[512]; } DeskEnt;
static DeskEnt *g_desk;
static int g_ndesk;
static pthread_once_t g_desk_once = PTHREAD_ONCE_INIT;

// Lower case letters and digits only: "Proton Mail" -> "protonmail", "brave-browser" -> "bravebrowser".
static void norm(char *out, size_t cap, const char *s) {
    size_t k = 0;
    for (; *s && k + 1 < cap; s++) {
        u8 c = ascii_lower((u8)*s);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[k++] = (char)c;
    }
    out[k] = 0;
}

// The folders that hold applications/ and icons/: $XDG_DATA_HOME first, then $XDG_DATA_DIRS, then Flatpak's exports.
static int data_dirs(char (*out)[PATH_MAX], int max) {
    int n = 0;
    char buf[8192];
    data_home(out[n++], PATH_MAX);
    const char *home = getenv("HOME");
    const char *v = getenv("XDG_DATA_DIRS");
    snprintf(buf, sizeof buf, "%s:%s/.local/share/flatpak/exports/share:/var/lib/flatpak/exports/share",
             v && v[0] ? v : "/usr/local/share:/usr/share", home ? home : "/nonexistent");
    char *save = NULL;
    for (char *d = strtok_r(buf, ":", &save); d && n < max; d = strtok_r(NULL, ":", &save)) {
        if (d[0] != '/') continue;
        size_t l = strlen(d);
        while (l > 1 && d[l - 1] == '/') d[--l] = 0;
        bool dup = false;
        for (int i = 0; i < n; i++) dup |= !strcmp(out[i], d);
        if (!dup) snprintf(out[n++], PATH_MAX, "%s", d);
    }
    return n;
}

static void desk_parse(const char *path, const char *id) {
    for (int i = 0; i < g_ndesk; i++)
        if (!strcmp(g_desk[i].id, id)) return; // (an earlier folder overrides)
    FILE *f = fopen(path, "r");
    if (!f) return;
    DeskEnt e;
    memset(&e, 0, sizeof e);
    snprintf(e.id, sizeof e.id, "%s", id);
    char line[1024];
    bool in = false, hidden = false;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { in = !strcmp(line, "[Desktop Entry]"); continue; }
        if (!in) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "Name")) snprintf(e.name, sizeof e.name, "%s", v);
        else if (!strcmp(k, "Icon")) snprintf(e.icon, sizeof e.icon, "%s", v);
        else if (!strcmp(k, "StartupWMClass")) snprintf(e.wm, sizeof e.wm, "%s", v);
        else if (!strcmp(k, "Hidden") && !strcmp(v, "true")) hidden = true;
        else if (!strcmp(k, "Exec")) { // the program's name: "/usr/bin/brave %U" -> "brave" (flatpak run: the app id)
            char tmp[512];
            snprintf(tmp, sizeof tmp, "%s", v);
            char *tok = strtok(tmp, " "), *prog = tok;
            if (tok && strstr(tok, "flatpak")) {
                while ((tok = strtok(NULL, " ")))
                    if (tok[0] != '-' && strcmp(tok, "run")) { prog = tok; break; }
            }
            if (prog) snprintf(e.exec, sizeof e.exec, "%s", path_basename(prog));
        }
    }
    fclose(f);
    if (hidden || !e.icon[0]) return;
    snprintf(e.first, sizeof e.first, "%.*s", (int)strcspn(e.name, " "), e.name);
    g_desk = xrealloc(g_desk, (size_t)(g_ndesk + 1) * sizeof *g_desk);
    g_desk[g_ndesk++] = e;
}

static void desk_build(void) {
    char dirs[24][PATH_MAX];
    int n = data_dirs(dirs, 24);
    for (int i = 0; i < n; i++) {
        char d[PATH_MAX + 16];
        snprintf(d, sizeof d, "%s/applications", dirs[i]);
        DIR *dp = opendir(d);
        if (!dp) continue;
        struct dirent *de;
        while ((de = readdir(dp))) {
            size_t l = strlen(de->d_name);
            if (l < 9 || strcmp(de->d_name + l - 8, ".desktop")) continue;
            char path[PATH_MAX * 2], id[128];
            snprintf(path, sizeof path, "%s/%s", d, de->d_name);
            snprintf(id, sizeof id, "%.*s", (int)(l - 8), de->d_name);
            desk_parse(path, id);
        }
        closedir(dp);
    }
}

// How well an app name fits a desktop entry: the same name, id, window class or program (100); the first word of
// its name or id ("Proton" -> "Proton Mail", "brave" -> "brave-browser": 85); a prefix (60) or part of one (40).
static int desk_score(const char *nk, const DeskEnt *e) {
    char f[5][160];
    norm(f[0], sizeof f[0], e->id);
    const char *dot = strrchr(e->id, '.'); // reverse-DNS ids: "com.brave.Browser" -> "browser" is too vague; "org.chromium.Chromium" -> "chromium"
    norm(f[1], sizeof f[1], dot ? dot + 1 : e->id);
    norm(f[2], sizeof f[2], e->wm);
    norm(f[3], sizeof f[3], e->exec);
    norm(f[4], sizeof f[4], e->name);
    size_t kl = strlen(nk);
    if (!kl) return 0;
    for (int i = 0; i < 5; i++)
        if (f[i][0] && !strcmp(f[i], nk)) return 100;
    char w[64];
    norm(w, sizeof w, e->first);
    if (w[0] && !strcmp(w, nk)) return 85;
    char idw[128];
    snprintf(idw, sizeof idw, "%.*s", (int)strcspn(e->id, "-_ "), e->id); // "brave-browser" -> "brave"
    norm(idw, sizeof idw, idw);
    if (idw[0] && !strcmp(idw, nk)) return 85;
    for (int i = 0; i < 5; i++)
        if (kl >= 4 && !strncmp(f[i], nk, kl)) return 60;
    for (int i = 0; i < 5; i++)
        if (kl >= 5 && strstr(f[i], nk)) return 40;
    return 0;
}

static const DeskEnt *desk_find(const char *key) {
    static const struct { const char *from, *to; } alias[] = {{"vscode", "code"}, {"visualstudiocode", "code"}, {"chrome", "googlechrome"}};
    char nk[96];
    norm(nk, sizeof nk, key);
    for (size_t i = 0; i < ARRAY_COUNT(alias); i++)
        if (!strcmp(nk, alias[i].from)) snprintf(nk, sizeof nk, "%s", alias[i].to);
    const DeskEnt *best = NULL;
    int bs = 0;
    for (int i = 0; i < g_ndesk; i++) {
        int s = desk_score(nk, &g_desk[i]);
        if (s > bs) { bs = s; best = &g_desk[i]; }
    }
    return best;
}

// --- icon theme lookup ---------------------------------------------------------------

static void read_key(const char *path, const char *group, const char *key, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    bool in = !group;
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { in = !group || !strcmp(line, group); continue; }
        if (in && !strncmp(line, key, kl) && (line[kl] == '=' || line[kl] == ' ')) {
            const char *v = strchr(line, '=');
            if (v) {
                v++;
                while (*v == ' ') v++;
                snprintf(out, cap, "%s", v);
            }
        }
    }
    fclose(f);
}

static bool file_ok(const char *p) { return access(p, R_OK) == 0; }

// The file of an icon name (largest raster or scalable), in the user's theme then hicolor, then pixmaps.
static bool icon_file(const char *name, char *out, size_t cap) {
    if (name[0] == '/') { snprintf(out, cap, "%s", name); return file_ok(out); }
    char themes[3][128] = {"", "", "hicolor"}, cfg[PATH_MAX], p[PATH_MAX];
    config_home(cfg, sizeof cfg);
    snprintf(p, sizeof p, "%s/gtk-3.0/settings.ini", cfg);
    read_key(p, "[Settings]", "gtk-icon-theme-name", themes[0], sizeof themes[0]);
    if (!themes[0][0]) { snprintf(p, sizeof p, "%s/gtk-4.0/settings.ini", cfg); read_key(p, "[Settings]", "gtk-icon-theme-name", themes[0], sizeof themes[0]); }
    snprintf(p, sizeof p, "%s/kdeglobals", cfg);
    read_key(p, "[Icons]", "Theme", themes[1], sizeof themes[1]);
    char dirs[24][PATH_MAX], bases[28][PATH_MAX];
    int nd = data_dirs(dirs, 24), nb = 0;
    const char *home = getenv("HOME");
    if (home) snprintf(bases[nb++], PATH_MAX, "%s/.icons", home);
    for (int i = 0; i < nd && nb < 28; i++) snprintf(bases[nb++], PATH_MAX, "%.*s/icons", PATH_MAX - 8, dirs[i]);
    static const char *const sub[] = {"scalable/apps/%s.svg", "512x512/apps/%s.png", "256x256/apps/%s.png", "128x128/apps/%s.png",
                                      "96x96/apps/%s.png", "64x64/apps/%s.png", "64x64/apps/%s.svg", "apps/64/%s.svg",
                                      "apps/48/%s.svg", "48x48/apps/%s.png", "48x48/apps/%s.svg", "32x32/apps/%s.png"};
    char file[256];
    for (int t = 0; t < 3; t++) {
        if (!themes[t][0] || (t < 2 && !strcmp(themes[t], "hicolor"))) continue;
        for (int b = 0; b < nb; b++)
            for (size_t s = 0; s < ARRAY_COUNT(sub); s++) {
                snprintf(file, sizeof file, sub[s], name);
                snprintf(out, cap, "%s/%s/%s", bases[b], themes[t], file);
                if (file_ok(out)) return true;
            }
    }
    for (int i = 0; i < nd; i++)
        for (int k = 0; k < 2; k++) {
            snprintf(out, cap, "%s/pixmaps/%s.%s", dirs[i], name, k ? "svg" : "png");
            if (file_ok(out)) return true;
        }
    return false;
}

// Scale RGBA w x h into a dw x dh box (centred, aspect kept) by area averaging, colours weighted by alpha.
static void rgba_fit(const u8 *src, int w, int h, u8 *dst, int dw, int dh) {
    memset(dst, 0, (size_t)dw * dh * 4);
    f32 sc = MIN((f32)dw / w, (f32)dh / h);
    int ow = MAX(1, (int)roundf(w * sc)), oh = MAX(1, (int)roundf(h * sc)), ox = (dw - ow) / 2, oy = (dh - oh) / 2;
    for (int y = 0; y < oh; y++) {
        int y0 = y * h / oh, y1 = MAX(y0 + 1, (y + 1) * h / oh);
        for (int x = 0; x < ow; x++) {
            int x0 = x * w / ow, x1 = MAX(x0 + 1, (x + 1) * w / ow);
            f64 r = 0, g = 0, b = 0, a = 0;
            int n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const u8 *p = src + ((size_t)yy * w + xx) * 4;
                    r += p[0] * (f64)p[3]; g += p[1] * (f64)p[3]; b += p[2] * (f64)p[3]; a += p[3];
                    n++;
                }
            u8 *d = dst + ((size_t)(oy + y) * dw + ox + x) * 4;
            if (a > 0) { d[0] = (u8)(r / a); d[1] = (u8)(g / a); d[2] = (u8)(b / a); }
            d[3] = (u8)(a / n);
        }
    }
}

static u8 *icon_decode(const char *path) {
    size_t l = strlen(path);
    u8 *out = NULL;
    if (l > 4 && !strcmp(path + l - 4, ".svg")) {
        NSVGimage *img = nsvgParseFromFile(path, "px", 96.0f);
        if (img && img->width > 0 && img->height > 0) {
            NSVGrasterizer *r = nsvgCreateRasterizer();
            f32 sc = MIN((f32)APPICON_PX / img->width, (f32)APPICON_PX / img->height);
            out = xcalloc(APPICON_PX * APPICON_PX, 4);
            nsvgRasterize(r, img, (APPICON_PX - img->width * sc) * 0.5f, (APPICON_PX - img->height * sc) * 0.5f, sc, out,
                          APPICON_PX, APPICON_PX, APPICON_PX * 4);
            nsvgDeleteRasterizer(r);
        }
        if (img) nsvgDelete(img);
    } else {
        int w, h, n;
        u8 *px = stbi_load(path, &w, &h, &n, 4);
        if (px && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
            out = xmalloc(APPICON_PX * APPICON_PX * 4);
            rgba_fit(px, w, h, out, APPICON_PX, APPICON_PX);
        }
        if (px) stbi_image_free(px);
    }
    return out;
}

// --- jobs: worker finds + decodes, the main thread keeps the result -------------------------------

typedef struct { int slot; char key[96]; u8 *rgba; } AppIconJob;

static void appicon_done(void *arg) {
    AppIconJob *j = arg;
    if (j->slot < g_nappicons && !strcmp(g_appicons[j->slot].key, j->key)) {
        g_appicons[j->slot].rgba = j->rgba;
        g_appicons[j->slot].state = j->rgba ? 1 : 2;
        A.redraw = true;
    } else free(j->rgba);
    free(j);
}

static void appicon_job(void *arg) {
    AppIconJob *j = arg;
    pthread_once(&g_desk_once, desk_build);
    const DeskEnt *e = desk_find(j->key);
    char path[PATH_MAX];
    if (e && icon_file(e->icon, path, sizeof path)) j->rgba = icon_decode(path);
    main_post(appicon_done, j);
}

// Icon id of the app called `key` (an item's app_key), or -1 while it's looked up / when there's none.
static int appicon_id(const char *key) {
    if (!key || !key[0] || A.selftest) return -1; // (self-tests: the machine's apps would make results vary)
    for (int i = 0; i < g_nappicons; i++)
        if (!strcmp(g_appicons[i].key, key)) return g_appicons[i].state == 1 ? APPICON_BASE + i : -1;
    g_appicons = xrealloc(g_appicons, (size_t)(g_nappicons + 1) * sizeof *g_appicons);
    AppIcon *a = &g_appicons[g_nappicons];
    memset(a, 0, sizeof *a);
    snprintf(a->key, sizeof a->key, "%s", key);
    AppIconJob *j = xcalloc(1, sizeof *j);
    j->slot = g_nappicons++;
    snprintf(j->key, sizeof j->key, "%s", key);
    pool_submit(appicon_job, j);
    return -1;
}

// RGBA pixels of app icon id fitted into w x h (caller frees), for icons.c's atlas cache.
static u8 *appicon_raster(int id, int w, int h) {
    int i = id - APPICON_BASE;
    if (i < 0 || i >= g_nappicons || g_appicons[i].state != 1) return NULL;
    u8 *out = xmalloc((size_t)w * h * 4);
    rgba_fit(g_appicons[i].rgba, APPICON_PX, APPICON_PX, out, w, h);
    return out;
}

static bool appicons_busy(void) {
    for (int i = 0; i < g_nappicons; i++)
        if (!g_appicons[i].state) return true;
    return false;
}
