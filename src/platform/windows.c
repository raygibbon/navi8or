/* TDX port.c's native filesystem/config/process boundary, adapted for UTF-8
 * paths and Navi8or's small APIs. No editor or application globals are shared. */
#include "nav.h"
#include "windows_internal.h"
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <bcrypt.h>
#include <shellapi.h>

uint64_t nav_platform_milliseconds(void) { return GetTickCount64(); }

int nav_open_external_url(const char *url, char *error, size_t capacity)
{
    if (!nav_external_url_valid(url)) {
        snprintf(error, capacity, "Invalid browser URL (embedded credentials are not allowed)"); return -1;
    }
    wchar_t *wide = nav_windows_wide(url);
    HINSTANCE result = wide ? ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL) : NULL;
    free(wide);
    if ((INT_PTR)result > 32) return 0;
    snprintf(error, capacity, "Unable to launch default browser (Windows error %lld)", (long long)(INT_PTR)result);
    return -1;
}

wchar_t *nav_windows_wide(const char *text)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *wide = count ? malloc((size_t)count * sizeof *wide) : NULL;
    if (!wide) { errno = count ? ENOMEM : EILSEQ; return NULL; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide); errno = EILSEQ; return NULL;
    }
    return wide;
}
int nav_windows_utf8(const wchar_t *wide, char *text, size_t capacity)
{
    if (capacity > INT_MAX || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide, -1, text, (int)capacity, NULL, NULL)) { errno = EILSEQ; return -1; }
    return 0;
}
int nav_windows_error(void)
{
    DWORD code = GetLastError();
    errno = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ? ENOENT :
            code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS ? EEXIST :
            code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION ? EACCES : EIO;
    return -1;
}
FILE *nav_platform_fopen(const char *path, const char *mode)
{
    wchar_t *wide = nav_windows_wide(path), *wmode = nav_windows_wide(mode);
    FILE *file = wide && wmode ? _wfopen(wide, wmode) : NULL;
    free(wide); free(wmode); return file;
}
int nav_platform_mkdir(const char *path, unsigned mode)
{
    wchar_t *wide = nav_windows_wide(path); int result = -1; (void)mode;
    if (wide) result = _wmkdir(wide);
    free(wide); return result;
}
int nav_platform_unlink(const char *path)
{
    wchar_t *wide = nav_windows_wide(path); int result = -1;
    if (wide) result = _wunlink(wide);
    free(wide); return result;
}
int nav_platform_access(const char *path, int mode)
{
    wchar_t *wide = nav_windows_wide(path); int result = -1;
    if (wide) result = _waccess(wide, mode);
    free(wide); return result;
}
char *nav_platform_getcwd(char *path, size_t capacity)
{
    wchar_t wide[32768];
    DWORD length = GetCurrentDirectoryW(32768, wide);
    if (!length || length >= 32768 || nav_windows_utf8(wide, path, capacity)) return NULL;
    return path;
}
int nav_platform_config_dir(char *path, size_t capacity)
{
    wchar_t wide[32768]; char base[NAV_PATH_MAX];
    DWORD length = GetEnvironmentVariableW(L"APPDATA", wide, 32768);
    if (!length || length >= 32768 || nav_windows_utf8(wide, base, sizeof base)) return -1;
    return snprintf(path, capacity, "%s/Navi8or", base) >= (int)capacity ? -1 : 0;
}
int nav_platform_mkstemp(char *path)
{
    static const char digits[] = "0123456789abcdef";
    size_t length = strlen(path);
    if (length < 6 || strcmp(path + length - 6, "XXXXXX")) { errno = EINVAL; return -1; }
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        unsigned char random[6];
        if (BCryptGenRandom(NULL, random, sizeof random, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) { errno = EIO; return -1; }
        for (size_t i = 0; i < sizeof random; i++) path[length - 6 + i] = digits[random[i] & 15];
        wchar_t *wide = nav_windows_wide(path);
        if (!wide) return -1;
        int fd = _wopen(wide, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        free(wide);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    errno = EEXIST; return -1;
}
int nav_platform_tempfile(char *path, size_t capacity)
{
    wchar_t wide[32768]; char base[NAV_PATH_MAX];
    DWORD length = GetTempPathW(32768, wide);
    if (!length || length >= 32768 || nav_windows_utf8(wide, base, sizeof base) ||
        snprintf(path, capacity, "%snav-view-XXXXXX", base) >= (int)capacity) return -1;
    return nav_platform_mkstemp(path);
}
int nav_platform_sync(int fd)
{ return FlushFileBuffers((HANDLE)_get_osfhandle(fd)) ? 0 : nav_windows_error(); }
int nav_platform_replace(const char *source, const char *destination)
{
    wchar_t *from = nav_windows_wide(source), *to = nav_windows_wide(destination);
    int result = -1;
    if (from && to) result = MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : nav_windows_error();
    free(from); free(to); return result;
}
ssize_t nav_platform_getline(char **line, size_t *capacity, FILE *file)
{
    size_t length = 0; int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (length + 1 >= *capacity) {
            size_t next = *capacity ? *capacity * 2 : 256;
            char *replacement = realloc(*line, next);
            if (!replacement) { errno = ENOMEM; return -1; }
            *line = replacement; *capacity = next;
        }
        (*line)[length++] = (char)ch;
        if (ch == '\n') break;
    }
    if (!length) return -1;
    (*line)[length] = 0; return (ssize_t)length;
}
const NavEditorConfig *nav_editor_default_config(void)
{
    static const char *arguments[] = {"{file}"};
    static const NavEditorConfig config = {"tdx", arguments, 1, true};
    return &config;
}

static char *expand_file(const char *argument, const char *path)
{
    static const char marker[] = "{file}";
    size_t marker_length = sizeof marker - 1;
    size_t path_length = strlen(path), length = 0, count = 0;
    const char *scan = argument, *found;
    while ((found = strstr(scan, marker)))
    {
        count++;
        scan = found + marker_length;
    }
    if (!count)
        return strdup(argument);
    length = strlen(argument) - count * marker_length + count * path_length + 1;
    char *result = malloc(length), *output = result;
    if (!result)
        return NULL;
    scan = argument;
    while ((found = strstr(scan, marker)))
    {
        size_t prefix = (size_t)(found - scan);
        memcpy(output, scan, prefix);
        output += prefix;
        memcpy(output, path, path_length);
        output += path_length;
        scan = found + marker_length;
    }
    strcpy(output, scan);
    return result;
}

int nav_platform_launch_editor(const NavEditorConfig *config, const char *path,
                               char *error, size_t error_size)
{
    wchar_t **arguments = NULL; int result = -1;
    if (!config || !config->executable || !config->executable[0]) goto done;
    arguments = calloc(config->argument_count + 2, sizeof *arguments);
    if (!arguments) goto done;
    arguments[0] = nav_windows_wide(config->executable);
    if (!arguments[0]) goto done;
    for (size_t i = 0; i < config->argument_count; i++) {
        char *expanded = expand_file(config->arguments[i], path);
        if (!expanded) goto done;
        arguments[i + 1] = nav_windows_wide(expanded); free(expanded);
        if (!arguments[i + 1]) goto done;
    }
    intptr_t process = _wspawnvp(config->wait ? _P_WAIT : _P_NOWAIT,
                                 arguments[0], (const wchar_t *const *)arguments);
    if (process == -1 || (config->wait && process != 0)) goto done;
    if (!config->wait) CloseHandle((HANDLE)process);
    result = 0;
done:
    if (arguments) {
        for (size_t i = 0; i <= config->argument_count; i++) free(arguments[i]);
        free(arguments);
    }
    if (result) snprintf(error, error_size, "cannot launch editor: %s", strerror(errno));
    return result;
}
