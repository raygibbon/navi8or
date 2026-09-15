#include "nav_ui_core.h"
static int span(const NavUiField *f, size_t start, size_t end, bool secret)
{
    int width = 0;
    while (start < end) { size_t n; uint32_t c = nav_ui_field_character(f->buffer + start, &n); width += secret ? 1 : nav_term_unicode_width(c); start += n; }
    return width;
}
void nav_ui_field_draw(NavUiField *f, int x, int y, int width, NavStyle style, bool secret, bool focused)
{
    if (width < 1) return;
    nav_ui_field_ensure_visible(f, (size_t)width);
    while (span(f, f->offset, f->cursor, secret) >= width && f->offset < f->cursor) {
        size_t n; nav_ui_field_character(f->buffer + f->offset, &n); f->offset += n;
    }
    nav_ui_text(x, y, width, "", style);
    int column = 0;
    size_t start = f->anchor < f->cursor ? f->anchor : f->cursor, end = f->anchor > f->cursor ? f->anchor : f->cursor;
    for (size_t at = f->offset; at < f->length;) {
        size_t n; uint32_t c = nav_ui_field_character(f->buffer + at, &n);
        int w = secret ? 1 : nav_term_unicode_width(c);
        if (column + w > width) break;
        NavStyle colour = f->selected && at >= start && at < end ? NAV_STYLE_SELECTION : style;
        nav_ui_text(x + column, y, w, "", colour);
        nav_term_unicode_glyph(x + column, y, secret ? '*' : c, colour);
        column += w; at += n;
    }
    if (focused) nav_term_cursor(x + span(f, f->offset, f->cursor, secret), y);
}
