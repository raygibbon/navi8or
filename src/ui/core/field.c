/* One-line UTF-8 editing; byte offsets always remain on character boundaries. */
#include "nav_ui_core.h"
#include "nav_clipboard.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint32_t nav_ui_field_character(const char *text, size_t *bytes)
{
    const unsigned char *s = (const unsigned char *)text;
    uint32_t c = s[0]; size_t n = 1;
    if (c >= 0xc2 && c <= 0xdf) { c &= 31; n = 2; }
    else if (c >= 0xe0 && c <= 0xef) { c &= 15; n = 3; }
    else if (c >= 0xf0 && c <= 0xf4) { c &= 7; n = 4; }
    else if (c >= 128) { *bytes = 1; return UINT32_MAX; }
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xc0) != 0x80) { *bytes = 1; return UINT32_MAX; }
        c = (c << 6) | (s[i] & 63);
    }
    *bytes = n;
    if ((n == 2 && c < 128) || (n == 3 && c < 2048) ||
        (n == 4 && c < 65536) || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return UINT32_MAX;
    return c;
}
static size_t next(const NavUiField *f, size_t at)
{ size_t n; nav_ui_field_character(f->buffer + at, &n); return at + n > f->length ? f->length : at + n; }
static size_t previous(const NavUiField *f, size_t at)
{ if (at) { at--; while (at && ((unsigned char)f->buffer[at] & 0xc0) == 0x80) at--; } return at; }
static size_t encode(uint32_t c, char out[5])
{
    if (c < 32 || c == 127 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return 0;
    size_t n = c < 128 ? 1 : c < 2048 ? 2 : c < 65536 ? 3 : 4;
    uint32_t value = c;
    for (size_t i = n; i > 1; i--) { out[i - 1] = (char)(0x80 | (value & 63)); value >>= 6; }
    out[0] = (char)(n == 1 ? value : (n == 2 ? 0xc0 : n == 3 ? 0xe0 : 0xf0) | value); out[n] = 0; return n;
}
static void selection(const NavUiField *f, size_t *start, size_t *end)
{
    *start = *end = f->cursor;
    if (f->selected) { *start = f->anchor < f->cursor ? f->anchor : f->cursor; *end = f->anchor > f->cursor ? f->anchor : f->cursor; }
}
static void erase(NavUiField *f, size_t start, size_t end)
{
    memmove(f->buffer + start, f->buffer + end, f->length - end + 1);
    f->length -= end - start; f->cursor = f->anchor = start; f->selected = false;
}
void nav_ui_field_init(NavUiField *f, char *buffer, size_t capacity)
{
    size_t length = capacity ? strnlen(buffer, capacity) : 0;
    if (capacity && length == capacity) { length--; buffer[length] = 0; }
    *f = (NavUiField){.buffer = buffer, .capacity = capacity, .length = length, .cursor = length, .anchor = length};
}
void nav_ui_field_destroy(NavUiField *f) { free(f->paste); f->paste = NULL; f->paste_failed = false; f->paste_length = 0; }
void nav_ui_field_ensure_visible(NavUiField *f, size_t width)
{
    if (!width) { f->offset = f->cursor; return; }
    if (f->offset > f->cursor) f->offset = 0;
    size_t count = 0; for (size_t i = f->offset; i < f->cursor; i = next(f, i)) count++;
    while (count >= width && f->offset < f->cursor) { f->offset = next(f, f->offset); count--; }
}
NavUiFieldResult nav_ui_field_insert(NavUiField *f, const char *text)
{
    size_t start, end, length = strlen(text); selection(f, &start, &end);
    if (!length) return NAV_UI_FIELD_IGNORED;
    if (!f->capacity || length >= f->capacity || f->length - (end - start) >= f->capacity - length) {
        snprintf(f->error, sizeof f->error, "Pasted text exceeds this field's %zu-byte limit", f->capacity ? f->capacity - 1 : 0); return NAV_UI_FIELD_ERROR;
    }
    for (size_t i = 0; i < length;) {
        size_t n; uint32_t c = nav_ui_field_character(text + i, &n);
        if (c == UINT32_MAX || c < 32 || c == 127) { snprintf(f->error, sizeof f->error, "Invalid UTF-8 or control character in pasted text"); return NAV_UI_FIELD_ERROR; }
        i += n;
    }
    memmove(f->buffer + start + length, f->buffer + end, f->length - end + 1);
    memcpy(f->buffer + start, text, length); f->length = f->length - (end - start) + length;
    f->cursor = f->anchor = start + length; f->selected = false; return NAV_UI_FIELD_CHANGED;
}
NavUiFieldResult nav_ui_field_event(NavUiField *f, const NavAction *event)
{
    if (!f || !event) return NAV_UI_FIELD_IGNORED;
    f->error[0] = 0;
    if (event->type == NAV_TERM_EVENT_PASTE_START) {
        nav_ui_field_destroy(f); f->paste = f->capacity ? malloc(f->capacity) : NULL; f->paste_length = 0; f->paste_failed = !f->paste;
        if (f->paste) f->paste[0] = 0;
        return NAV_UI_FIELD_IGNORED;
    }
    if (event->type == NAV_TERM_EVENT_PASTE_END) {
        NavUiFieldResult result;
        if (f->paste_failed) { snprintf(f->error, sizeof f->error, "Paste too large or unable to allocate paste buffer"); result = NAV_UI_FIELD_ERROR; }
        else result = f->paste ? nav_ui_field_insert(f, f->paste) : NAV_UI_FIELD_IGNORED;
        nav_ui_field_destroy(f); return result;
    }
    if (event->type != NAV_TERM_EVENT_KEY) return NAV_UI_FIELD_IGNORED;
    size_t start, end; selection(f, &start, &end);
    if (f->paste || f->paste_failed) {
        char text[5]; size_t n = event->command == NAV_CMD_TEXT ? encode(event->text, text) : 0;
        if (n && !f->paste_failed) {
            if (f->paste_length + n >= f->capacity) f->paste_failed = true;
            else { memcpy(f->paste + f->paste_length, text, n + 1); f->paste_length += n; }
        }
        return NAV_UI_FIELD_IGNORED;
    }
    switch (event->command) {
    case NAV_CMD_CANCEL: return NAV_UI_FIELD_CANCELLED;
    case NAV_CMD_ACCEPT: return NAV_UI_FIELD_ACCEPTED;
    case NAV_CMD_TEXT_SELECT_ALL: f->anchor = 0; f->cursor = f->length; f->selected = true; return NAV_UI_FIELD_MOVED;
    case NAV_CMD_TEXT_COPY:
    case NAV_CMD_TEXT_CUT: {
        if (start == end) return NAV_UI_FIELD_IGNORED;
        char *copy = malloc(end - start + 1);
        if (!copy) { snprintf(f->error, sizeof f->error, "Unable to allocate clipboard text"); return NAV_UI_FIELD_ERROR; }
        memcpy(copy, f->buffer + start, end - start); copy[end - start] = 0;
        int result = nav_clipboard_set_text(copy, f->error, sizeof f->error); free(copy);
        if (result) return NAV_UI_FIELD_ERROR;
        if (event->command == NAV_CMD_TEXT_CUT) { erase(f, start, end); return NAV_UI_FIELD_CHANGED; }
        return NAV_UI_FIELD_IGNORED;
    }
    case NAV_CMD_TEXT_PASTE: {
        char *text = NULL; if (nav_clipboard_get_text(&text, f->error, sizeof f->error)) return NAV_UI_FIELD_ERROR;
        NavUiFieldResult result = nav_ui_field_insert(f, text); free(text); return result;
    }
    case NAV_CMD_HOME: f->cursor = 0; break;
    case NAV_CMD_END: f->cursor = f->length; break;
    case NAV_CMD_LEFT: f->cursor = start != end ? start : previous(f, f->cursor); break;
    case NAV_CMD_RIGHT: f->cursor = start != end ? end : next(f, f->cursor); break;
    case NAV_CMD_BACKSPACE: if (start == end) start = previous(f, start); if (start == end) return NAV_UI_FIELD_IGNORED; erase(f, start, end); return NAV_UI_FIELD_CHANGED;
    case NAV_CMD_TEXT_DELETE: if (start == end) end = next(f, end); if (start == end) return NAV_UI_FIELD_IGNORED; erase(f, start, end); return NAV_UI_FIELD_CHANGED;
    case NAV_CMD_TEXT: { char text[5]; if (!encode(event->text, text)) return NAV_UI_FIELD_IGNORED; return nav_ui_field_insert(f, text); }
    default: return NAV_UI_FIELD_IGNORED;
    }
    f->selected = false; f->anchor = f->cursor; return NAV_UI_FIELD_MOVED;
}
