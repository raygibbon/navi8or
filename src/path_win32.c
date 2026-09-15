/* TDX port.c uses native full-path APIs. Keep UTF-8 in Navi8or and convert here. */
#include "nav.h"
#include "platform/windows_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
bool nav_path_is_absolute(const char *p)
{
    return p && (p[0] == '/' || p[0] == '\\' ||
           (isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '/' || p[2] == '\\')));
}
const char *nav_path_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    return base;
}
bool nav_leaf_name_copy(char output[NAV_NAME_MAX], const char *name) {
    size_t length;
    if (!output || !name || !name[0])
        return false;
    length = strlen(name);
    if (length >= NAV_NAME_MAX)
        return false;
    memcpy(output, name, length + 1);
    return true;
}

int nav_path_normalize(const char *input, char *output, size_t capacity)
{
    wchar_t *wide = nav_windows_wide(input), full[32768]; int result = -1;
    if (!wide) return -1;
    DWORD length = GetFullPathNameW(wide, 32768, full, NULL);
    if (length && length < 32768) result = nav_windows_utf8(full, output, capacity);
    free(wide);
    if (!result) for (char *p = output; *p; p++) if (*p == '\\') *p = '/';
    return result;
}
int nav_path_join(const char *a, const char *b, char *output, size_t capacity)
{
    char temporary[NAV_PATH_MAX];
    if (snprintf(temporary, sizeof temporary, "%s/%s", a, b) >= (int)sizeof temporary) return -1;
    return nav_path_normalize(temporary, output, capacity);
}
int nav_path_parent(const char *path, char *output, size_t capacity)
{ return nav_path_join(path, "..", output, capacity); }
