/* TDX-compatible normalization of raw termbox input. */
#include "termbox2/termbox2.h"
#include "termbox_input.h"
#include <string.h>

static int translate_key(unsigned key)
{
    switch (key)
    {
    case TB_KEY_F1: return NAV_KEY_F1;
    case TB_KEY_F2: return NAV_KEY_F2;
    case TB_KEY_F3: return NAV_KEY_F3;
    case TB_KEY_F4: return NAV_KEY_F4;
    case TB_KEY_F5: return NAV_KEY_F5;
    case TB_KEY_F6: return NAV_KEY_F6;
    case TB_KEY_F7: return NAV_KEY_F7;
    case TB_KEY_F8: return NAV_KEY_F8;
    case TB_KEY_F9: return NAV_KEY_F9;
    case TB_KEY_F10: return NAV_KEY_F10;
    case TB_KEY_F11: return NAV_KEY_F11;
    case TB_KEY_F12: return NAV_KEY_F12;
    case TB_KEY_ARROW_UP: return NAV_KEY_UP;
    case TB_KEY_ARROW_DOWN: return NAV_KEY_DOWN;
    case TB_KEY_ARROW_LEFT: return NAV_KEY_LEFT;
    case TB_KEY_ARROW_RIGHT: return NAV_KEY_RIGHT;
    case TB_KEY_HOME: return NAV_KEY_HOME;
    case TB_KEY_END: return NAV_KEY_END;
    case TB_KEY_PGUP: return NAV_KEY_PAGE_UP;
    case TB_KEY_PGDN: return NAV_KEY_PAGE_DOWN;
    case TB_KEY_DELETE: return NAV_KEY_DELETE;
    case TB_KEY_ENTER: return NAV_KEY_ENTER;
    case TB_KEY_TAB: return NAV_KEY_TAB;
    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2: return NAV_KEY_BACKSPACE;
    case TB_KEY_ESC: return NAV_KEY_ESCAPE;
    case TB_KEY_CTRL_BACKSLASH: return '\\';
    case TB_KEY_CTRL_R: return 'r';
    case TB_KEY_CTRL_U: return 'u';
    default: return key < 128 ? (int)key : NAV_KEY_NONE;
    }
}

int nav_term_translate_tb_event(const struct tb_event *raw, NavTermEvent *event)
{
    if (!raw || !event)
        return -1;
    memset(event, 0, sizeof *event);
    if (raw->type == TB_EVENT_RESIZE)
    {
        event->type = NAV_TERM_EVENT_RESIZE;
        event->width = raw->w;
        event->height = raw->h;
        return 1;
    }
    if (raw->type != TB_EVENT_KEY)
        return 1;

    event->type = NAV_TERM_EVENT_KEY;
    event->key = raw->ch ? (int)raw->ch : translate_key(raw->key);
    if (raw->mod & TB_MOD_ALT)
        event->modifiers |= NAV_MOD_ALT;
    if (raw->mod & TB_MOD_CTRL)
        event->modifiers |= NAV_MOD_CTRL;
    if (raw->mod & TB_MOD_SHIFT)
        event->modifiers |= NAV_MOD_SHIFT;
    if (raw->key == TB_KEY_CTRL_BACKSLASH)
        event->modifiers |= NAV_MOD_CTRL;
    if (raw->key == TB_KEY_CTRL_R || raw->key == TB_KEY_CTRL_U)
        event->modifiers |= NAV_MOD_CTRL;

    /* Termbox aliases these ordinary controls with Ctrl+letter. TDX removes
     * only that synthetic Ctrl flag, preserving Alt/Shift and modifiers on
     * navigation/function keys. */
    if (raw->key == TB_KEY_ESC || raw->key == TB_KEY_ENTER ||
        raw->key == TB_KEY_TAB || raw->key == TB_KEY_BACKSPACE ||
        raw->key == TB_KEY_BACKSPACE2)
        event->modifiers &= ~NAV_MOD_CTRL;
    return 1;
}
