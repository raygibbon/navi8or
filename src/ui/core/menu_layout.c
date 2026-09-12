/*
 * Adapted substantially from TDX src/core/pull.c:get_bar_spacing() and
 * draw_lite_head(). Editor menu globals and dispatch are removed; the TDX
 * spacing limits and major column/width contract are retained.
 */
#include "nav_ui_core.h"
#include <string.h>

void nav_ui_get_bar_spacing_for_style(const NavUiMenu *menus, size_t count,
                                      int columns, NavUiStyle style,
                                      NavUiMajor *major)
{
    int total = 0, spacing, column;
    if (!menus || !major || !count)
        return;
    for (size_t index = 0; index < count; index++)
    {
        major[index].width = (int)strlen(menus[index].label ? menus[index].label : "");
        total += major[index].width;
    }
    if (style == NAV_UI_STYLE_MODERN) {
        spacing = columns >= total + (int)count * 2 ? 3 : 1;
        column = columns > total + spacing * ((int)count - 1) ? 1 : 0;
        for (size_t index = 0; index < count; index++) {
            major[index].column = column;
            column += spacing + major[index].width;
        }
        return;
    }
    if (count == 1)
        spacing = 0;
    else
    {
        spacing = (columns - 2 - total) / ((int)count - 1);
        if (spacing <= 0)
            spacing = 1;
        else if (spacing > 6)
            spacing = 6;
    }
    column = (columns - 2 - (total + spacing * ((int)count - 1))) / 2;
    if (column < 0)
        column = 0;
    else if (column > 6)
        column = 6;
    for (size_t index = 0; index < count; index++)
    {
        major[index].column = column;
        column += spacing + major[index].width;
    }
}

void nav_ui_get_bar_spacing(const NavUiMenu *menus, size_t count, int columns,
                            NavUiMajor *major)
{
    nav_ui_get_bar_spacing_for_style(menus, count, columns,
                                     NAV_UI_STYLE_MODERN, major);
}
