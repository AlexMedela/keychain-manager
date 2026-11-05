// main.c - unity build entry point
#include "base.h"
#include <ctype.h>
#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>
#include <dbus/dbus.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#if defined(__SANITIZE_ADDRESS__)
// Debug (ASan) builds: SDL unloads the video/GPU libraries at exit, which makes memory they still own look
// leaked (reported from "<unknown module>"). Keep them loaded so LeakSanitizer only reports real leaks.
int dlclose(void *handle);
int dlclose(void *handle) {
    (void)handle;
    return 0;
}
// Known leaks in system libraries (not ours): libX11's converted titles, Mesa's EGL driver state, libdbus's
// per-process data (kept until exit by design), fontconfig's caches.
const char *__lsan_default_suppressions(void);
const char *__lsan_default_suppressions(void) {
    return "leak:libX11.so\nleak:libEGL_mesa.so\nleak:libgallium-\nleak:libdbus-1.so\nleak:libfontconfig.so\n";
}
#endif

#include "pool.c"
#include "dbus.c"
#include "secret.c"
#include "font.c"
#include "draw.c"
#include "theme.c"
#include "icons.c"
#include "app.c"
