#include "nav_input.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const struct { const char *name; int key; } names[] = {
    {"Up", NAV_KEY_UP}, {"Down", NAV_KEY_DOWN}, {"Left", NAV_KEY_LEFT},
    {"Right", NAV_KEY_RIGHT}, {"Home", NAV_KEY_HOME}, {"End", NAV_KEY_END},
    {"PageUp", NAV_KEY_PAGE_UP}, {"PageDown", NAV_KEY_PAGE_DOWN},
    {"PgUp", NAV_KEY_PAGE_UP}, {"PgDn", NAV_KEY_PAGE_DOWN},
    {"Delete", NAV_KEY_DELETE}, {"Backspace", NAV_KEY_BACKSPACE},
    {"Insert", NAV_KEY_INSERT}, {"Enter", NAV_KEY_ENTER}, {"Escape", NAV_KEY_ESCAPE}, {"Tab", NAV_KEY_TAB},
    {"Space", ' '}, {"Plus", '+'}
};
int nav_key_parse(const char *text, NavKeyStroke *key)
{
    *key = (NavKeyStroke){0};
    const char *plus;
    while ((plus = strchr(text, '+'))) {
        unsigned modifier = 0;
        size_t n = (size_t)(plus - text);
        if (n == 4 && !strncasecmp(text, "Ctrl", n)) modifier = NAV_MOD_CTRL;
        if (n == 3 && !strncasecmp(text, "Alt", n)) modifier = NAV_MOD_ALT;
        if (n == 5 && !strncasecmp(text, "Shift", n)) modifier = NAV_MOD_SHIFT;
        if (!modifier || (key->modifiers & modifier)) return -1;
        key->modifiers |= modifier;
        text = plus + 1;
    }
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcasecmp(text, names[i].name)) { key->key = names[i].key; return 0; }
    if (!strcasecmp(text, "Esc")) { key->key = NAV_KEY_ESCAPE; return 0; }
    if ((text[0] == 'F' || text[0] == 'f') && isdigit((unsigned char)text[1])) {
        char *end;
        long number = strtol(text + 1, &end, 10);
        if (*end || number < 1 || number > 12) return -1;
        key->key = NAV_KEY_F1 + (int)number - 1;
        return 0;
    }
    if (text[0] >= 33 && text[0] <= 126 && !text[1]) {
        key->key = tolower((unsigned char)text[0]);
        return 0;
    }
    return -1;
}
int nav_key_format(NavKeyStroke key, char *buffer, size_t size)
{
    char value[16] = "";
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (key.key == names[i].key) snprintf(value, sizeof value, "%s", names[i].name);
    if (key.key >= NAV_KEY_F1 && key.key <= NAV_KEY_F12)
        snprintf(value, sizeof value, "F%d", key.key - NAV_KEY_F1 + 1);
    else if (!value[0] && key.key >= 33 && key.key < 127)
        snprintf(value, sizeof value, "%c", toupper(key.key));
    return snprintf(buffer, size, "%s%s%s%s", key.modifiers & NAV_MOD_CTRL ? "Ctrl+" : "",
        key.modifiers & NAV_MOD_ALT ? "Alt+" : "", key.modifiers & NAV_MOD_SHIFT ? "Shift+" : "", value);
}
