/*
 * Derived from TDX src/terminal/termbox_backend.c.
 * Raw termbox events, modifier translation, CP437 conversion, and resize
 * invalidation stay at this boundary. Persistent UI policy belongs above it.
 */
#define TB_IMPL
#if defined(__GNUC__)
/* Vendored resize-pipe I/O intentionally ignores results; Ubuntu's fortified
 * libc marks these calls warn_unused_result. Keep strict warnings in our code. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#include "termbox2/termbox2.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "cp437.h"
#include "nav_terminal.h"
#include "nav_theme.h"
#include "termbox_input.h"
#include <locale.h>
#include <string.h>

static const uintattr_t dos_palette[16] = {16, 19, 34, 37, 124, 127, 130, 145, 240, 21, 46, 51, 196, 201, 226, 231};
static const NavTheme *active_theme;
static int terminal_active;

void nav_term_set_theme(const NavTheme *theme)
{
    active_theme = theme;
    nav_symbols_set_active(theme ? &theme->symbols : NULL);
}
const NavTheme *nav_term_theme(void) { return active_theme; }

static void put_cell(int x, int y, uint32_t ch, NavStyle style)
{
    if (!terminal_active || !active_theme || x < 0 || y < 0 || x >= tb_width() || y >= tb_height())
        return;
    tb_set_cell(x, y, ch, dos_palette[active_theme->foreground[style]], dos_palette[active_theme->background[style]]);
}

int nav_term_init(void)
{
    int result;
    setlocale(LC_CTYPE, "");
    if (!active_theme)
        return -1;
    result = tb_init();
    if (result < 0)
        return result;
    terminal_active = 1;
    if (tb_set_output_mode(TB_OUTPUT_256) < 0 || tb_set_input_mode(TB_INPUT_ALT) < 0)
    {
        nav_term_shutdown();
        return -1;
    }
    return 0;
}

void nav_term_shutdown(void)
{
    if (terminal_active)
    {
        tb_shutdown();
        terminal_active = 0;
    }
}
int nav_term_width(void) { return terminal_active ? tb_width() : 0; }
int nav_term_height(void) { return terminal_active ? tb_height() : 0; }
void nav_term_clear(NavStyle style)
{
    if (terminal_active && active_theme)
    {
        tb_set_clear_attrs(dos_palette[active_theme->foreground[style]], dos_palette[active_theme->background[style]]);
        tb_clear();
    }
}
void nav_term_present(void)
{
    if (terminal_active)
        tb_present();
}

void nav_term_text(int x, int y, int width, const char *text, NavStyle style)
{
    int ended = text == NULL;
    for (int i = 0; i < width; i++)
    {
        unsigned char ch = ' ';
        if (!ended)
        {
            ch = (unsigned char)text[i];
            if (ch == 0)
            {
                ended = 1;
                ch = ' ';
            }
        }
        put_cell(x + i, y, ch, style);
    }
}

void nav_term_glyph(int x, int y, uint32_t glyph, NavStyle style) { put_cell(x, y, glyph <= 0xff ? tdx_cp437_to_unicode((unsigned char)glyph) : glyph, style); }
void nav_term_hline(int x, int y, uint32_t glyph, int count, NavStyle style)
{
    while (count-- > 0)
        nav_term_glyph(x++, y, glyph, style);
}
void nav_term_vline(int x, int y, uint32_t glyph, int count, NavStyle style)
{
    while (count-- > 0)
        nav_term_glyph(x, y++, glyph, style);
}
void nav_term_cursor(int x, int y)
{
    if (terminal_active)
        tb_set_cursor(x, y);
}
void nav_term_hide_cursor(void)
{
    if (terminal_active)
        tb_hide_cursor();
}

int nav_term_get_cell(int x, int y, NavTermCell *cell)
{
    struct tb_cell *source;
    if (!terminal_active || !cell || tb_get_cell(x, y, 1, &source) < 0)
        return -1;
    cell->ch = source->ch;
    cell->foreground = (uint32_t)source->fg;
    cell->background = (uint32_t)source->bg;
    return 0;
}

int nav_term_set_cell(int x, int y, const NavTermCell *cell)
{
    if (!terminal_active || !cell || x < 0 || y < 0 || x >= tb_width() || y >= tb_height())
        return -1;
    tb_set_cell(x, y, cell->ch, (uintattr_t)cell->foreground, (uintattr_t)cell->background);
    return 0;
}

int nav_term_poll_event(NavTermEvent *event, int timeout_ms)
{
    struct tb_event raw;
    int result;
    if (!event)
        return -1;
    memset(event, 0, sizeof *event);
    result = timeout_ms < 0 ? tb_poll_event(&raw) : tb_peek_event(&raw, timeout_ms);
    /* Termbox reports a populated event as TB_OK (zero). */
    if (result < 0)
        return result;
    result = nav_term_translate_tb_event(&raw, event);
    if (event->type == NAV_TERM_EVENT_RESIZE)
        tb_invalidate();
    return result;
}
