#ifndef NAV_PLATFORM_H
#define NAV_PLATFORM_H

/* Small OS boundaries; paths passed by the application are UTF-8. */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>
uint64_t nav_platform_milliseconds(void); /* monotonic, for UI throttling */
#ifdef _WIN32
FILE *nav_platform_fopen(const char *, const char *);
int nav_platform_mkdir(const char *, unsigned);
int nav_platform_unlink(const char *);
int nav_platform_access(const char *, int);
char *nav_platform_getcwd(char *, size_t);
int nav_platform_mkstemp(char *);
int nav_platform_tempfile(char *, size_t);
int nav_platform_sync(int);
int nav_platform_replace(const char *, const char *);
ssize_t nav_platform_getline(char **, size_t *, FILE *);
#define nav_platform_seek _fseeki64
#define nav_platform_tell _ftelli64
static inline struct tm *nav_platform_localtime(const time_t *value, struct tm *out)
{ return localtime_s(out, value) ? NULL : out; }
static inline void nav_platform_console_signals(void) { }
#else
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#define nav_platform_fopen fopen
#define nav_platform_mkdir mkdir
#define nav_platform_unlink unlink
#define nav_platform_access access
#define nav_platform_getcwd getcwd
#define nav_platform_mkstemp mkstemp
#define nav_platform_sync fsync
#define nav_platform_replace rename
#define nav_platform_getline getline
#define nav_platform_seek fseeko
#define nav_platform_tell ftello
#define nav_platform_localtime localtime_r
static inline int nav_platform_tempfile(char *path, size_t capacity)
{
    const char *base = getenv("TMPDIR");
    if (!base || !base[0]) base = "/tmp";
    if (snprintf(path, capacity, "%s/nav-view-XXXXXX", base) >= (int)capacity) return -1;
    return mkstemp(path);
}
static inline void nav_platform_console_signals(void) { signal(SIGQUIT, SIG_IGN); }
#endif
#endif
