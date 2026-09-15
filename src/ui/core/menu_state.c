/* Pure state transitions extracted from the TDX pull.c-derived menu control. */
#include "nav_ui_core.h"
#include <ctype.h>

static size_t first_selectable(const NavUiMenu *menu)
{
    for (size_t i = 0; i < menu->minor_count; i++)
        if (!menu->minor[i].separator)
            return i;
    return menu->minor_count;
}

size_t nav_ui_menu_move_minor(const NavUiMenu *menu, size_t current, int direction)
{
    if (!menu || !menu->minor_count)
        return 0;
    if (current >= menu->minor_count)
        current = first_selectable(menu);
    for (size_t tries = 0; tries < menu->minor_count; tries++)
    {
        current = direction > 0 ? (current + 1) % menu->minor_count : (current ? current - 1 : menu->minor_count - 1);
        /* TDX highlights disabled commands but never selects separators. */
        if (!menu->minor[current].separator)
            return current;
    }
    return current;
}

size_t nav_ui_menu_move_major(size_t current, size_t count, int direction)
{
    if (!count)
        return 0;
    return direction > 0 ? (current + 1) % count : (current ? current - 1 : count - 1);
}

int nav_ui_menu_major_motion(const NavAction *event)
{
    if (!event || event->type != NAV_TERM_EVENT_KEY) return 0;
    if (event->command == NAV_CMD_RIGHT) return 1;
    if (event->command == NAV_CMD_LEFT) return -1;
    return 0;
}

int nav_ui_menu_accelerator(const NavUiMenu *menu, int key, size_t *selected)
{
    if (!menu || key < 0 || key >= 128 || !isalnum((unsigned char)key))
        return NAV_UI_MENU_CANCELLED;
    for (size_t i = 0; i < menu->minor_count; i++)
    {
        const NavUiMenuItem *item = &menu->minor[i];
        if (!item->disabled && !item->separator &&
            tolower((unsigned char)item->accelerator) == tolower((unsigned char)key))
        {
            if (selected)
                *selected = i;
            return item->command;
        }
    }
    return NAV_UI_MENU_CANCELLED;
}

int nav_ui_menu_activate(const NavUiMenu *menu, size_t selected)
{
    const NavUiMenuItem *item;
    if (!menu || selected >= menu->minor_count)
        return NAV_UI_MENU_CANCELLED;
    item = &menu->minor[selected];
    if (item->disabled || item->separator)
        return NAV_UI_MENU_CANCELLED;
    return item->command;
}
