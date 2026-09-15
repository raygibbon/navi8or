#ifndef NAV_WINDOWS_INTERNAL_H
#define NAV_WINDOWS_INTERNAL_H
#include <winsock2.h>
#include <windows.h>
#include <stddef.h>
/* Allocated conversions, strict UTF-8; caller frees the result. */
wchar_t *nav_windows_wide(const char *);
int nav_windows_utf8(const wchar_t *, char *, size_t);
int nav_windows_error(void);

#ifdef NAV_SECURE_FILE_TESTING
/* Private test hook; not compiled into production builds. */
void nav_windows_test_format_error(char *error, size_t size, const char *context, DWORD code);
#endif
#endif
