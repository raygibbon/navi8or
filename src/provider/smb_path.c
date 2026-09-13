#include "nav_smb.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int hex(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool utf8(const unsigned char *text)
{
    while (*text) {
        unsigned code = *text++;
        unsigned more, minimum;
        if (code < 128) continue;
        if (code >= 0xc2 && code <= 0xdf) { more = 1; minimum = 0x80; code &= 31; }
        else if (code >= 0xe0 && code <= 0xef) { more = 2; minimum = 0x800; code &= 15; }
        else if (code >= 0xf0 && code <= 0xf4) { more = 3; minimum = 0x10000; code &= 7; }
        else return false;
        while (more--) {
            if ((*text & 0xc0) != 0x80) return false;
            code = (code << 6) | (*text++ & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}

static bool component_valid(const char *name)
{
    size_t length = strlen(name);
    if (!length || length >= NAV_NAME_MAX || name[length - 1] == ' ' ||
        name[length - 1] == '.' || !utf8((const unsigned char *)name)) return false;
    for (const unsigned char *ch = (const unsigned char *)name; *ch; ch++)
        if (*ch < 32 || *ch == 127 || strchr("/\\:%*?\"<>|", *ch)) return false;
    return true;
}

static int decode(const char *start, size_t length, char *out, size_t capacity)
{
    size_t used = 0;
    for (size_t index = 0; index < length; index++) {
        unsigned char ch = (unsigned char)start[index];
        if (ch == '%') {
            if (index + 2 >= length || hex(start[index + 1]) < 0 ||
                hex(start[index + 2]) < 0) return -1;
            ch = (unsigned char)((hex(start[index + 1]) << 4) | hex(start[index + 2]));
            index += 2;
        }
        if (!ch || ch == '/' || ch == '\\' || used + 1 >= capacity) return -1;
        out[used++] = (char)ch;
    }
    out[used] = 0;
    return 0;
}

static int encode(const char *input, char *output, size_t capacity)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t used = 0;
    for (const unsigned char *ch = (const unsigned char *)input; *ch; ch++) {
        bool plain = (*ch >= 'a' && *ch <= 'z') || (*ch >= 'A' && *ch <= 'Z') ||
                     (*ch >= '0' && *ch <= '9') || strchr("-._~/", *ch);
        size_t amount = plain ? 1 : 3;
        if (used + amount >= capacity) return -1;
        if (plain) output[used++] = (char)*ch;
        else {
            output[used++] = '%';
            output[used++] = digits[*ch >> 4];
            output[used++] = digits[*ch & 15];
        }
    }
    output[used] = 0;
    return 0;
}

static int format_url(NavSmbUrl *url)
{
    char share[NAV_URL_MAX], path[NAV_URL_MAX];
    if (encode(url->share, share, sizeof share) || encode(url->path, path, sizeof path))
        return -1;
    return snprintf(url->url, sizeof url->url, "smb://%s/%s%s", url->server,
                    share, path) >= (int)sizeof url->url ? -1 : 0;
}

int nav_smb_url_parse(const char *input, NavSmbUrl *url, char *error, size_t error_size)
{
    const char *start, *end;
    char component[NAV_NAME_MAX];
    size_t length, used = 1;
    if (!input || !url || strlen(input) >= NAV_URL_MAX || strlen(input) < 7 ||
        tolower((unsigned char)input[0]) != 's' ||
        tolower((unsigned char)input[1]) != 'm' ||
        tolower((unsigned char)input[2]) != 'b' || strncmp(input + 3, "://", 3) ||
        strchr(input, '?') || strchr(input, '#')) goto invalid;
    memset(url, 0, sizeof *url);
    start = input + 6;
    end = strchr(start, '/');
    if (!end || end == start || (size_t)(end - start) >= sizeof url->server)
        goto invalid;
    /* DNS/IPv4 server names, or bracketed IPv6, with an optional decimal port. */
    const char *host_end = end, *port = NULL;
    if (*start == '[') {
        host_end = memchr(start, ']', (size_t)(end - start));
        if (!host_end || host_end == start + 1) goto invalid;
        for (const char *ch = start + 1; ch < host_end; ch++)
            if (hex(*ch) < 0 && *ch != ':' && *ch != '.') goto invalid;
        host_end++;
        if (host_end < end) {
            if (*host_end != ':') goto invalid;
            port = host_end + 1;
        }
    } else {
        port = memchr(start, ':', (size_t)(end - start));
        if (port) { host_end = port; port++; }
        if (host_end == start) goto invalid;
        for (const char *ch = start; ch < host_end; ch++)
            if (!((*ch >= 'a' && *ch <= 'z') || (*ch >= 'A' && *ch <= 'Z') ||
                  (*ch >= '0' && *ch <= '9') || *ch == '-' || *ch == '.')) goto invalid;
    }
    if (port) {
        unsigned number = 0;
        if (port == end) goto invalid;
        for (const char *ch = port; ch < end; ch++) {
            if (*ch < '0' || *ch > '9' || number > 6553) goto invalid;
            number = number * 10 + (unsigned)(*ch - '0');
        }
        if (!number || number > 65535) goto invalid;
    }
    length = (size_t)(end - start);
    for (size_t index = 0; index < length; index++)
        url->server[index] = (char)tolower((unsigned char)start[index]);
    start = end + 1;
    end = strchr(start, '/');
    if (!end) end = start + strlen(start);
    if (decode(start, (size_t)(end - start), url->share, sizeof url->share) ||
        !component_valid(url->share)) goto invalid;
    url->path[0] = '/';
    start = end;
    while (*start) {
        while (*start == '/') start++;
        if (!*start) break;
        end = strchr(start, '/');
        if (!end) end = start + strlen(start);
        if (decode(start, (size_t)(end - start), component, sizeof component)) goto invalid;
        if (!strcmp(component, "..")) {
            if (used == 1) goto invalid;
            while (used > 1 && url->path[used - 1] != '/') used--;
            if (used > 1) used--;
        } else if (strcmp(component, ".")) {
            if (!component_valid(component)) goto invalid;
            length = strlen(component);
            if (used + length + 1 >= sizeof url->path) goto invalid;
            if (used > 1) url->path[used++] = '/';
            memcpy(url->path + used, component, length);
            used += length;
        }
        url->path[used] = 0;
        start = end;
    }
    if (format_url(url)) goto invalid;
    return 0;
invalid:
    snprintf(error, error_size, "malformed SMB URI or unsupported path component");
    return -1;
}

bool nav_smb_url_within(const NavSmbUrl *root, const NavSmbUrl *target)
{
    size_t length = strlen(root->path);
    return !strcmp(root->server, target->server) && !strcmp(root->share, target->share) &&
           !strncmp(root->path, target->path, length) &&
           (length == 1 || !target->path[length] || target->path[length] == '/');
}

int nav_smb_url_child(const NavSmbUrl *parent, const char *name, NavSmbUrl *child,
                       char *error, size_t error_size)
{
    if (!name || !component_valid(name)) goto invalid;
    *child = *parent;
    if (snprintf(child->path, sizeof child->path, "%s%s%s", parent->path,
                 !strcmp(parent->path, "/") ? "" : "/", name) >= (int)sizeof child->path ||
        format_url(child)) goto invalid;
    return 0;
invalid:
    snprintf(error, error_size, "invalid SMB child name or path too long");
    return -1;
}
