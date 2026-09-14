#ifndef NAV_CLIPBOARD_H
#define NAV_CLIPBOARD_H
#include <stddef.h>
#define NAV_CLIPBOARD_LIMIT (1024u * 1024u)
/* get returns allocated UTF-8 text, owned by the caller. No shell commands. */
int nav_clipboard_get_text(char **, char *, size_t);
int nav_clipboard_set_text(const char *, char *, size_t);
#endif
