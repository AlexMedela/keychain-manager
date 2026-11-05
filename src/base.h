// base.h - core types, memory arenas, string helpers, secret memory
#pragma once
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <pwd.h>
#include <dirent.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#ifdef __GLIBC__
#include <malloc.h> // mallopt
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) MIN(MAX(x, lo), hi)

static inline u64 now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    return p;
}

static void *xcalloc(size_t c, size_t n) {
    void *p = calloc(c ? c : 1, n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    return p;
}

// ---------------------------------------------------------------------------
// Arena: bump allocator made of chained blocks, freed all at once.

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t cap, used, pad; // 32-byte header keeps data 16-byte aligned
} ArenaBlock;

typedef struct {
    ArenaBlock *head;
    size_t block_size;
} Arena;

static void *arena_alloc(Arena *a, size_t n, size_t align) {
    ArenaBlock *b = a->head;
    if (b) {
        size_t off = (b->used + align - 1) & ~(align - 1);
        if (off + n <= b->cap) {
            b->used = off + n;
            return (u8 *)(b + 1) + off;
        }
    }
    size_t bs = a->block_size ? a->block_size : (1u << 20);
    if (n + align > bs) bs = n + align;
    b = xmalloc(sizeof(ArenaBlock) + bs);
    b->next = a->head;
    b->cap = bs;
    b->used = n;
    a->head = b;
    return b + 1;
}

static void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *n = b->next;
        free(b);
        b = n;
    }
    a->head = NULL;
}

// ---------------------------------------------------------------------------
// Strings

static inline bool is_dot_or_dotdot(const char *n) { return n[0] == '.' && (!n[1] || (n[1] == '.' && !n[2])); }
static inline bool is_digit(u8 c) { return c >= '0' && c <= '9'; }
static inline u8 ascii_lower(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c; }

// Natural, case-insensitive compare: "file2" < "file10".
static int natcmp(const char *a, const char *b) {
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    for (;;) {
        u8 ca = *x, cb = *y;
        if (!ca || !cb) return (int)ca - (int)cb;
        if (is_digit(ca) && is_digit(cb)) {
            while (*x == '0') x++;
            while (*y == '0') y++;
            const u8 *dx = x, *dy = y;
            while (is_digit(*x)) x++;
            while (is_digit(*y)) y++;
            size_t lx = (size_t)(x - dx), ly = (size_t)(y - dy);
            if (lx != ly) return lx < ly ? -1 : 1;
            int c = memcmp(dx, dy, lx);
            if (c) return c;
            continue;
        }
        ca = ascii_lower(ca);
        cb = ascii_lower(cb);
        if (ca != cb) return (int)ca - (int)cb;
        x++;
        y++;
    }
}

static bool str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (ascii_lower((u8)*a) != ascii_lower((u8)*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static bool str_iprefix(const char *s, const char *prefix, size_t plen) {
    for (size_t i = 0; i < plen; i++) {
        if (!s[i] || ascii_lower((u8)s[i]) != ascii_lower((u8)prefix[i])) return false;
    }
    return true;
}

static u32 utf8_decode(const char **ps, const char *end) {
    const u8 *s = (const u8 *)*ps;
    u32 c = *s;
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!n || (const char *)s + n > end) { *ps += 1; return 0xFFFD; }
    if (n == 1) { *ps += 1; return c; }
    u32 cp = c & (0x7Fu >> n);
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *ps += 1; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *ps += n;
    return cp;
}

// Index of the character after the one at i (UTF-8), at most n.
static int utf8_next(const char *s, int i, int n) {
    if (i >= n) return n;
    i++;
    while (i < n && ((u8)s[i] & 0xC0) == 0x80) i++;
    return i;
}

// Remove the last UTF-8 character from a NUL-terminated buffer.
static void utf8_pop(char *s) {
    size_t n = strlen(s);
    while (n > 0) {
        n--;
        if (((u8)s[n] & 0xC0) != 0x80) break;
    }
    s[n] = 0;
}

static void fmt_count(char *out, u64 n) {
    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)n);
    int o = 0;
    for (int i = 0; i < len; i++) {
        out[o++] = tmp[i];
        int rem = len - i - 1;
        if (rem > 0 && rem % 3 == 0) out[o++] = ',';
    }
    out[o] = 0;
}

static void fmt_size(char *out, size_t cap, u64 s) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    if (s < 1024) { snprintf(out, cap, "%llu B", (unsigned long long)s); return; }
    f64 v = (f64)s;
    int i = 0;
    while (v >= 1024.0 && i < 5) { v /= 1024.0; i++; }
    snprintf(out, cap, v < 10.0 ? "%.1f %s" : "%.0f %s", v, units[i]);
}

// FNV-1a: hash tables keyed by object paths.
static u64 str_hash(const char *s) {
    u64 h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (u8)*s) * 1099511628211ull;
    return h;
}

// ---------------------------------------------------------------------------
// Paths

static void path_join(char *out, size_t cap, const char *dir, const char *name) {
    if (dir[0] == '/' && dir[1] == 0) snprintf(out, cap, "/%s", name);
    else snprintf(out, cap, "%s/%s", dir, name);
}

static const char *path_basename(const char *p) {
    const char *s = strrchr(p, '/');
    return (s && s[1]) ? s + 1 : p;
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(tmp, mode) && errno != EEXIST) return -1;
        *p = '/';
    }
    return mkdir(tmp, mode) && errno != EEXIST ? -1 : 0;
}

// $XDG_CONFIG_HOME (or ~/.config).
static void config_home(char *out, size_t cap) {
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s", x);
    else snprintf(out, cap, "%s/.config", h && h[0] ? h : "/tmp");
}

// $XDG_DATA_HOME (or ~/.local/share).
static void data_home(char *out, size_t cap) {
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s", x);
    else snprintf(out, cap, "%s/.local/share", h && h[0] ? h : "/tmp");
}

// Write a whole file through a temporary next to it, so readers never see half of it.
static bool write_file_atomic(const char *path, const char *text) {
    char tmp[PATH_MAX + 16];
    snprintf(tmp, sizeof tmp, "%s.tmp%d", path, (int)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    size_t n = strlen(text), off = 0;
    while (off < n) {
        ssize_t w = write(fd, text + off, n - off);
        if (w <= 0) { close(fd); unlink(tmp); return false; }
        off += (size_t)w;
    }
    if (close(fd) || rename(tmp, path)) { unlink(tmp); return false; }
    return true;
}

static bool file_equals(const char *path, const char *text) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = strlen(text);
    char *buf = xmalloc(n + 2);
    size_t got = fread(buf, 1, n + 1, f);
    fclose(f);
    bool eq = got == n && !memcmp(buf, text, n);
    free(buf);
    return eq;
}

// Running from an AppImage? Its path ($APPIMAGE, set by the AppImage runtime), else NULL.
static const char *appimage_path(void) {
    const char *a = getenv("APPIMAGE");
    return a && a[0] == '/' && access(a, X_OK) == 0 ? a : NULL;
}

// ---------------------------------------------------------------------------
// Secret memory: buffers holding passwords are locked into RAM (never swapped) and wiped before they're freed.

typedef struct {
    u8 *p;
    size_t n, cap;
} SecBuf;

static void sec_free(SecBuf *b) {
    if (!b->p) return;
    explicit_bzero(b->p, b->cap);
    munlock(b->p, b->cap);
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

// n bytes (+1 for a NUL, so text secrets can be used as C strings), zeroed.
static void sec_alloc(SecBuf *b, size_t n) {
    sec_free(b);
    b->cap = n + 1;
    b->p = xcalloc(1, b->cap);
    mlock(b->p, b->cap); // (best effort: RLIMIT_MEMLOCK may be small)
    b->n = n;
}

static void sec_set(SecBuf *b, const void *data, size_t n) {
    sec_alloc(b, n);
    if (n) memcpy(b->p, data, n);
}

static void sec_move(SecBuf *dst, SecBuf *src) {
    sec_free(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

// Valid UTF-8 without control characters other than tab/newline: shown as text, else as bytes.
static bool sec_is_text(const SecBuf *b) {
    const char *s = (const char *)b->p, *e = s + b->n;
    while (s < e) {
        u8 c = (u8)*s;
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return false;
        if (c == 0x7F) return false;
        if (utf8_decode(&s, e) == 0xFFFD) return false;
    }
    return true;
}

// No core dumps or ptrace from other processes of the same user (passwords would end up in them).
static void harden_process(void) {
#if defined(__SANITIZE_ADDRESS__)
    return; // (debug builds: LeakSanitizer needs to ptrace the process at exit)
#endif
    struct rlimit rl = {0, 0};
    setrlimit(RLIMIT_CORE, &rl);
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Starting programs
// ---------------------------------------------------------------------------

// SIGCHLD is ignored app-wide (children reap themselves), and an ignored signal survives exec: every program we
// start gets it back at its default, or ones that wait for their own children (Python's subprocess, shells) break.
// flags: extra POSIX_SPAWN_* flags (setsid, process group...).
static void spawn_attr_init(posix_spawnattr_t *at, short flags) {
    posix_spawnattr_init(at);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGCHLD);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigdefault(at, &def);
    posix_spawnattr_setflags(at, (short)(flags | POSIX_SPAWN_SETSIGDEF));
}

static posix_spawnattr_t g_spawn_at;
static pthread_once_t g_spawn_once = PTHREAD_ONCE_INIT;
static void spawn_at_make(void) { spawn_attr_init(&g_spawn_at, 0); }

// The attributes for a plain posix_spawn (shared, read-only once made).
static const posix_spawnattr_t *spawn_attr(void) {
    pthread_once(&g_spawn_once, spawn_at_make);
    return &g_spawn_at;
}

// Is the program there (a path, or a name looked up in $PATH)?
static bool in_path(const char *name) {
    if (strchr(name, '/')) return access(name, X_OK) == 0;
    const char *path = getenv("PATH");
    char dirs[4096], p[PATH_MAX];
    snprintf(dirs, sizeof dirs, "%s", path && path[0] ? path : "/usr/local/bin:/usr/bin:/bin");
    char *save = NULL;
    for (char *d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        snprintf(p, sizeof p, "%s/%s", d[0] ? d : ".", name);
        if (access(p, X_OK) == 0) return true;
    }
    return false;
}
