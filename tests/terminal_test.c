#include "nav_terminal.h"
#include "termbox2/termbox2.h"
#include "terminal/termbox_input.h"
#include <assert.h>

static NavTermEvent translate(uint16_t key, uint8_t modifiers)
{
    struct tb_event raw = {.type = TB_EVENT_KEY, .mod = modifiers, .key = key};
    NavTermEvent event;
    assert(nav_term_translate_tb_event(&raw, &event) == 1);
    assert(event.type == NAV_TERM_EVENT_KEY);
    return event;
}

static void expect(uint16_t raw_key, uint8_t raw_modifiers,
                   int key, unsigned modifiers)
{
    NavTermEvent event = translate(raw_key, raw_modifiers);
    assert(event.key == key);
    assert(event.modifiers == modifiers);
}

static void paste_boundaries(void)
{
    struct tb_event raw = {.type = TB_EVENT_KEY, .key = TB_KEY_PASTE_START}; NavTermEvent event;
    assert(nav_term_translate_tb_event(&raw, &event) == 1 && event.type == NAV_TERM_EVENT_PASTE_START);
    raw.key = TB_KEY_PASTE_END;
    assert(nav_term_translate_tb_event(&raw, &event) == 1 && event.type == NAV_TERM_EVENT_PASTE_END);
}

int main(void)
{
    paste_boundaries();
    expect(TB_KEY_ENTER, TB_MOD_CTRL, NAV_KEY_ENTER, 0);
    expect(TB_KEY_TAB, TB_MOD_CTRL, NAV_KEY_TAB, 0);
    expect(TB_KEY_ESC, 0, NAV_KEY_ESCAPE, 0);
    expect(TB_KEY_BACKSPACE, TB_MOD_CTRL, NAV_KEY_BACKSPACE, 0);
    expect(TB_KEY_BACKSPACE2, TB_MOD_CTRL, NAV_KEY_BACKSPACE, 0);
    expect(TB_KEY_ARROW_RIGHT, TB_MOD_CTRL, NAV_KEY_RIGHT, NAV_MOD_CTRL);
    expect(TB_KEY_ARROW_LEFT, TB_MOD_ALT, NAV_KEY_LEFT, NAV_MOD_ALT);
    expect(TB_KEY_CTRL_BACKSLASH, 0, '\\', NAV_MOD_CTRL);
    expect(TB_KEY_CTRL_R, 0, 'r', NAV_MOD_CTRL);
    expect(TB_KEY_CTRL_U, 0, 'u', NAV_MOD_CTRL);
    expect(TB_KEY_CTRL_Q, 0, 'q', NAV_MOD_CTRL);
    expect(TB_KEY_CTRL_F, 0, 'f', NAV_MOD_CTRL);
    expect(TB_KEY_INSERT, 0, NAV_KEY_INSERT, 0);
    expect(TB_KEY_INSERT, TB_MOD_SHIFT, NAV_KEY_INSERT, NAV_MOD_SHIFT);
    expect(TB_KEY_F3, 0, NAV_KEY_F3, 0);
    expect(TB_KEY_F5, 0, NAV_KEY_F5, 0);
    expect(TB_KEY_F10, 0, NAV_KEY_F10, 0);
    expect(TB_KEY_BACK_TAB, 0, NAV_KEY_TAB, NAV_MOD_SHIFT);
    struct tb_event raw = {.type = TB_EVENT_KEY, .ch = 6, .mod = TB_MOD_CTRL};
    NavTermEvent event;
    assert(nav_term_translate_tb_event(&raw, &event) == 1);
    assert(event.key == 'f' && event.modifiers == NAV_MOD_CTRL);
    raw.ch = 28;
    assert(nav_term_translate_tb_event(&raw, &event) == 1);
    assert(event.key == '\\' && event.modifiers == NAV_MOD_CTRL);
    raw.ch = 0;
    assert(nav_term_translate_tb_event(&raw, &event) == 1);
    assert(event.key == ' ' && event.modifiers == NAV_MOD_CTRL);
    return 0;
}
