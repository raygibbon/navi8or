#include "nav.h"
#include <string.h>
NavShellLayout nav_shell_layout(int width, int height)
{
    return (NavShellLayout){.width = width, .height = height, .menu_row = 0,
        .workspace_top = 1, .workspace_bottom = height - 3,
        .status_row = height - 2, .command_row = height - 1};
}

bool nav_commander_layout_for_style(int width, int height, NavUiStyle style,
                                    NavCommanderLayout *layout)
{
    if (!layout) return false;
    memset(layout, 0, sizeof *layout);
    layout->width = width;
    layout->height = height;
    NavShellLayout shell = nav_shell_layout(width, height);
    layout->top_row = shell.menu_row;
    layout->menu_row = shell.menu_row;
    layout->pane_title_row = shell.workspace_top;
    layout->pane_location_row = 2;
    layout->pane_column_header_row = 3;
    layout->pane_column_separator_row =
        style == NAV_UI_STYLE_CLASSIC ? 4 : -1;
    layout->body_top = style == NAV_UI_STYLE_CLASSIC ? 5 : 4;
    layout->summary_row = height - 3;
    layout->status_row = shell.status_row;
    layout->key_bar_row = shell.command_row;
    layout->body_height = layout->summary_row - layout->body_top;
    layout->body_bottom = layout->summary_row - 1;
    layout->divider = width / 2;
    layout->pane_x[0] = 0;
    layout->pane_width[0] = layout->divider;
    layout->pane_x[1] = layout->divider + 1;
    layout->pane_width[1] = width - layout->pane_x[1];
    return width >= 20 && height >= 8 && layout->body_height >= 1 &&
           layout->pane_width[0] > 0 && layout->pane_width[1] > 0;
}

bool nav_commander_layout(int width, int height, NavCommanderLayout *layout)
{
    return nav_commander_layout_for_style(width, height, NAV_UI_STYLE_MODERN,
                                          layout);
}

size_t nav_function_key_layout(int width, const NavKeymap *map, NavInputContext context,
                               NavFunctionKeySegment *segments, size_t capacity)
{
    size_t count = 0;
    int minimum = 0;
    if (!segments) return 0;
    for (int f = 1; f <= 12; f++) {
        NavInput input = {0};
        NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = NAV_KEY_F1 + f - 1};
        NavAction action = nav_input_resolve(&input, map, context, &event);
        if (!action.command) continue;
        if (count == capacity) return 0;
        int key_width = f >= 10 ? 3 : 2;
        minimum += key_width;
        segments[count++] = (NavFunctionKeySegment){.key = f, .key_width = key_width,
            .command = action.command, .label = nav_command_label(action.command)};
    }
    if (width < minimum) return 0;
    int x = 0;
    for (size_t i = 0; i < count; i++) {
        segments[i].x = x;
        segments[i].width = (width - x) / (int)(count - i);
        x += segments[i].width;
    }
    return count;
}
