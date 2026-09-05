#include "nav.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool nav_path_is_absolute(const char *p) { return p != NULL && p[0] == '/'; }
const char *nav_path_basename(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
int nav_path_normalize(const char *in, char *out, size_t n) {
    char work[NAV_PATH_MAX], *parts[512], *tok, *save = NULL; size_t count = 0, used = 0;
    if (in == NULL || out == NULL || n == 0) return -1;
    if (!nav_path_is_absolute(in)) { if (getcwd(work, sizeof work) == NULL || snprintf(work + strlen(work), sizeof work - strlen(work), "/%s", in) >= (int)(sizeof work - strlen(work))) return -1; }
    else if (snprintf(work, sizeof work, "%s", in) >= (int)sizeof work) return -1;
    for (tok = strtok_r(work, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (!strcmp(tok, "."))
            continue;
        if (!strcmp(tok, "..")) {
            if (count)
                --count;
            continue;
        }
        parts[count++] = tok;
    }
    if (n < 2)
        return -1;
    out[used++] = '/';
    out[used] = 0;
    for (size_t i = 0; i < count; ++i) { int r = snprintf(out + used, n - used, "%s%s", used > 1 ? "/" : "", parts[i]); if (r < 0 || (size_t)r >= n - used) return -1; used += (size_t)r; }
    return 0;
}
int nav_path_join(const char *a, const char *b, char *o, size_t n) { char temp[NAV_PATH_MAX]; if (snprintf(temp, sizeof temp, "%s/%s", a, b) >= (int)sizeof temp) return -1; return nav_path_normalize(temp, o, n); }
int nav_path_parent(const char *p, char *o, size_t n) { return nav_path_join(p, "..", o, n); }
