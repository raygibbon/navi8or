#include "nav_clipboard.h"
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
static HWND owner(bool *created)
{
    HWND window = GetConsoleWindow(); *created = !window;
    return window ? window : CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                              HWND_MESSAGE, NULL, NULL, NULL);
}
int nav_clipboard_get_text(char **out, char *error, size_t size)
{
    *out = NULL;
    if (!OpenClipboard(NULL)) goto fail;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *wide = data ? GlobalLock(data) : NULL;
    size_t capacity = data ? GlobalSize(data) / sizeof(wchar_t) : 0, length = 0;
    if (wide && capacity <= NAV_CLIPBOARD_LIMIT) {
        while (length < capacity && wide[length]) length++;
        if (length < capacity) {
            int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
            if (count > 0 && (unsigned)count <= NAV_CLIPBOARD_LIMIT) {
                *out = malloc((size_t)count);
                if (*out && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, *out, count, NULL, NULL)) { free(*out); *out = NULL; }
            }
        }
    }
    if (wide) GlobalUnlock(data);
    CloseClipboard();
    if (*out) return 0;
fail:
    snprintf(error, size, "Clipboard text unavailable, invalid, or too large"); return -1;
}
int nav_clipboard_set_text(const char *text, char *error, size_t size)
{
    if (strlen(text) >= NAV_CLIPBOARD_LIMIT) goto fail;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    HGLOBAL data = count > 0 ? GlobalAlloc(GMEM_MOVEABLE, (size_t)count * sizeof(wchar_t)) : NULL;
    wchar_t *wide = data ? GlobalLock(data) : NULL;
    if (!wide) { if (data) GlobalFree(data); goto fail; }
    int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count);
    GlobalUnlock(data);
    bool created; HWND window = owner(&created);
    int result = -1;
    if (converted && window && OpenClipboard(window)) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data)) { data = NULL; result = 0; }
        CloseClipboard();
    }
    if (created && window) DestroyWindow(window);
    if (data) GlobalFree(data);
    if (!result) return 0;
fail:
    snprintf(error, size, "Unable to copy UTF-8 text to Windows clipboard"); return -1;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
static char *local_text;
/* -2 means helper is absent. Other failures must not paste stale local text. */
static int helper(const char *const *argv, const char *input, char **output)
{
    int channel[2]; if (pipe(channel)) return -1;
    pid_t child = fork();
    if (child < 0) { close(channel[0]); close(channel[1]); return -1; }
    if (!child) {
        int null = open("/dev/null", O_RDWR);
        dup2(channel[input ? 0 : 1], input ? STDIN_FILENO : STDOUT_FILENO);
        if (null >= 0) { dup2(null, STDERR_FILENO); dup2(null, input ? STDOUT_FILENO : STDIN_FILENO); close(null); }
        close(channel[0]); close(channel[1]);
        execvp(argv[0], (char *const *)argv); _exit(errno == ENOENT ? 127 : 126);
    }
    int fd = channel[input ? 1 : 0]; close(channel[input ? 0 : 1]);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    char *buffer = input ? NULL : malloc(NAV_CLIPBOARD_LIMIT);
    size_t used = 0, length = input ? strlen(input) : NAV_CLIPBOARD_LIMIT - 1;
    int result = buffer || input ? 0 : -1;
    /* Blocking SIGPIPE during helper writes avoids terminating the application
       if a clipboard service closes its stdin early. Consume only our signal. */
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE); sigpending(&pending);
    sigprocmask(SIG_BLOCK, &blocked, &previous);
    struct timespec begin; clock_gettime(CLOCK_MONOTONIC, &begin);
    while (!result) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - begin.tv_sec >= 2) { result = -1; break; }
        struct pollfd event = {fd, input ? POLLOUT : POLLIN, 0};
        if (poll(&event, 1, 2000) <= 0) { result = -1; break; }
        ssize_t amount = input ? write(fd, input + used, length - used) : read(fd, buffer + used, length - used);
        if (amount < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (amount < 0) { result = -1; break; }
        if (!amount) break;
        used += (size_t)amount;
        if (used == length) { if (!input) result = -1; break; }
    }
    close(fd);
    if (!sigismember(&pending, SIGPIPE)) { sigset_t now; sigpending(&now); if (sigismember(&now, SIGPIPE)) { int signal; sigwait(&blocked, &signal); } }
    sigprocmask(SIG_SETMASK, &previous, NULL);
    if (result) kill(child, SIGTERM);
    int status = 0;
    /* Helpers normally exit once their pipe is drained. Bound their lifetime. */
    for (int i = 0; ; i++) {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0 && errno != EINTR) { result = -1; break; }
        if (i == 200) { kill(child, SIGKILL); waitpid(child, &status, 0); result = -1; break; }
        struct timespec pause = {0, 10000000}; nanosleep(&pause, NULL);
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) result = -2;
    else if (!WIFEXITED(status) || WEXITSTATUS(status)) result = -1;
    if (!result && !input) { buffer[used] = 0; if (strlen(buffer) != used) result = -1; else *output = buffer; }
    if (result || input) free(buffer);
    return result;
}
static int desktop(const char *input, char **out)
{
#ifdef __APPLE__
    const char *args[] = {input ? "pbcopy" : "pbpaste", NULL}; return helper(args, input, out);
#else
    if (getenv("WAYLAND_DISPLAY")) {
        const char *get[] = {"wl-paste", "--no-newline", "--type", "text", NULL};
        const char *set[] = {"wl-copy", "--type", "text/plain;charset=utf-8", NULL};
        int result = helper(input ? set : get, input, out); if (result != -2) return result;
    }
    if (getenv("DISPLAY")) {
        const char *args[] = {"xclip", "-selection", "clipboard", "-target", "UTF8_STRING", input ? "-in" : "-out", NULL};
        return helper(args, input, out);
    }
    return -2;
#endif
}
int nav_clipboard_get_text(char **out, char *error, size_t size)
{
    *out = NULL; int result = desktop(NULL, out);
    if (!result) return 0;
    if (result == -2 && local_text) { *out = strdup(local_text); if (*out) return 0; }
    snprintf(error, size, "Clipboard unavailable: install wl-clipboard or xclip, or use terminal paste"); return -1;
}
int nav_clipboard_set_text(const char *text, char *error, size_t size)
{
    if (strlen(text) >= NAV_CLIPBOARD_LIMIT) { snprintf(error, size, "Clipboard text too large"); return -1; }
    int result = desktop(text, NULL);
    if (!result) return 0;
    if (result == -2) { char *copy = strdup(text); if (copy) { free(local_text); local_text = copy; return 0; } }
    snprintf(error, size, "Unable to copy clipboard text"); return -1;
}
#endif
