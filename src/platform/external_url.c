#include "nav_platform.h"
#include <string.h>

/* Never send embedded URL credentials to another application. */
bool nav_external_url_valid(const char *url)
{
    if (!url) return false;
    const char *host;
    if (!strncmp(url, "https://", 8)) host = url + 8;
    else if (!strncmp(url, "http://", 7)) host = url + 7;
    else return false;
    if (!*host || *host == '/' || *host == '?' || *host == '#') return false;
    const char *end = host + strcspn(host, "/?#");
    for (const char *p = host; p < end; p++) if (*p == '@' || *p == '\\') return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p <= 32 || *p == 127 || *p == '\\') return false;
    return true;
}
