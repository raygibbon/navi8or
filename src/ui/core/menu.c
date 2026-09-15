/* Adapted from TDX src/core/pull.c:make_menu(), pull_me(), and draw_lite_head(). */
#include "nav_ui_core.h"
#include "nav_theme.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *rows;
    int *separators;
    size_t separator_count;
    int width;
    int height;
} PreparedMenu;

static NavUiStyle current_style(void)
{
    const NavTheme *theme = nav_term_theme();
    return theme ? theme->style : NAV_UI_STYLE_MODERN;
}

static size_t first_selectable(const NavUiMenu *menu)
{
    for (size_t i = 0; i < menu->minor_count; i++)
        if (!menu->minor[i].separator) return i;
    return menu->minor_count;
}

static void draw_headings(const NavUiMenu *menus, size_t count, size_t selected)
{
    NavUiMajor major[32];
    if (count > sizeof major / sizeof *major) count = sizeof major / sizeof *major;
    nav_ui_get_bar_spacing_for_style(menus, count, nav_term_width(),
                                     current_style(), major);
    nav_ui_draw_lite_head(menus, count, major, selected);
}

void nav_ui_draw_lite_head(const NavUiMenu *menus, size_t count,
                          const NavUiMajor *major, size_t selected)
{
    nav_ui_text(0, 0, nav_term_width(), "", NAV_STYLE_KEYBAR);
    for (size_t index = 0; index < count; index++) {
        NavStyle style = index == selected ? NAV_STYLE_KEYBAR_SELECTED : NAV_STYLE_KEYBAR;
        int start = major[index].column - (index == selected ? 1 : 0);
        int width = major[index].width + (index == selected ? 2 : 0);
        nav_ui_text(start, 0, width, "", style);
        nav_ui_text(major[index].column, 0, major[index].width,
                    menus[index].label ? menus[index].label : "", style);
    }
    int column;
    const char *identity = nav_ui_menu_identity(major, count, nav_term_width(),
                                                nav_ui_show_app_identity(), &column);
    if (identity[0]) nav_ui_text(column, 0, nav_term_width() - column, identity, NAV_STYLE_KEYBAR);
}

void nav_ui_draw_menu_bar(const NavUiMenu *menus, size_t count)
{
    NavUiMajor major[32];
    if (!menus || !count) return;
    if (count > sizeof major / sizeof *major) count = sizeof major / sizeof *major;
    nav_ui_get_bar_spacing_for_style(menus, count, nav_term_width(),
                                     current_style(), major);
    /* A selected index beyond the array draws the permanent, inactive bar. */
    nav_ui_draw_lite_head(menus, count, major, count);
}

/* Navi8or equivalent of TDX make_menu(): build item interiors once, with
 * the same accelerator/label/right-aligned-key field positions. */
static int make_menu(const NavUiMenu *menu, PreparedMenu *prepared)
{
    int longest_name = 0, longest_key = -2;
    memset(prepared, 0, sizeof *prepared);
    for (size_t index = 0; index < menu->minor_count; index++) {
        const NavUiMenuItem *item = &menu->minor[index];
        int length = item->line ? (int)strlen(item->line) : 0;
        char binding[80];
        int key_length = nav_ui_show_menu_keys() ? nav_keymap_label(nav_ui_keymap(), nav_ui_workspace_context(),
                                          (NavCommand)item->command, binding, sizeof binding) : 0;
        if (!key_length) key_length = -2;
        if (length > longest_name) longest_name = length;
        if (key_length > longest_key) longest_key = key_length;
        if (item->separator) prepared->separator_count++;
    }
    prepared->width = 2 + 3 + longest_name + 2 + longest_key + 2;
    if (prepared->width < 7) prepared->width = 7;
    prepared->height = (int)menu->minor_count + 2;
    prepared->separators = calloc(prepared->separator_count ? prepared->separator_count : 1,
                                  sizeof *prepared->separators);
    if (!prepared->separators) return -1;
    size_t separator = 0;
    for (size_t index = 0; index < menu->minor_count; index++) {
        const NavUiMenuItem *item = &menu->minor[index];
        if (item->separator) {
            prepared->separators[separator++] = (int)index + 1;
        }
    }
    prepared->rows = nav_ui_create_frame_rows(prepared->width, prepared->height,
                                              prepared->separators,
                                              prepared->separator_count);
    if (!prepared->rows) return -1;
    for (size_t index = 0; index < menu->minor_count; index++) {
        const NavUiMenuItem *item = &menu->minor[index];
        char *line = prepared->rows + (index + 1) * (prepared->width + 1);
        if (item->separator) continue;
        if (item->accelerator) {
            line[2] = (char)toupper((unsigned char)item->accelerator);
            line[3] = ')';
        }
        if (item->line) {
            size_t amount = strlen(item->line);
            if (amount > (size_t)(prepared->width - 7)) amount = (size_t)(prepared->width - 7);
            memcpy(line + 5, item->line, amount);
        }
        char binding[80];
        int length = nav_ui_show_menu_keys() ? nav_keymap_label(nav_ui_keymap(), nav_ui_workspace_context(),
                                      (NavCommand)item->command, binding, sizeof binding) : 0;
        if (length > 0) {
            int start = prepared->width - 2 - length;
            if (start >= 0) memcpy(line + start, binding, (size_t)length);
        }
    }
    return 0;
}

static void free_menu(PreparedMenu *prepared, size_t count)
{
    (void)count;
    free(prepared->rows);
    free(prepared->separators);
    memset(prepared, 0, sizeof *prepared);
}

static void draw_item(const NavUiMenu *menu, const PreparedMenu *prepared,
                      int column, int row, size_t index, size_t selected)
{
    const NavUiMenuItem *item = &menu->minor[index];
    NavStyle style;
    if (item->separator) return;
    if (index == selected) style = item->disabled ? NAV_STYLE_MENU_SELECTED_DISABLED : NAV_STYLE_MENU_SELECTED;
    else style = item->disabled ? NAV_STYLE_MENU_DISABLED : NAV_STYLE_MENU;
    nav_ui_text(column + 1, row + (int)index + 1, prepared->width - 2,
                prepared->rows + (index + 1) * (prepared->width + 1) + 1, style);
    /* TDX pull_me() leaves disabled row frame cells in Menu. */
    if (item->disabled) {
        NavTermCell cell;
        if (!nav_term_get_cell(column, row + (int)index + 1, &cell))
            nav_term_glyph(column, row + (int)index + 1, cell.ch, NAV_STYLE_MENU);
        if (!nav_term_get_cell(column + prepared->width - 1, row + (int)index + 1, &cell))
            nav_term_glyph(column + prepared->width - 1, row + (int)index + 1,
                           cell.ch, NAV_STYLE_MENU);
    }
}

static void place_menu(int *row, int *column, int width, int height)
{
    int screen_width = nav_term_width(), screen_height = nav_term_height();
    if (*column + width + 1 > screen_width - 1) {
        *column = screen_width - 1 - width;
        if (*column < 1) *column = 1;
    }
    if (*row + height > screen_height) {
        *row -= height + 1;
        if (*row < 0) {
            *row = screen_height - height;
            if (*row < 0) *row = 0;
        }
    }
}

int nav_ui_pull_down(NavUiMenu *menus, size_t count, int *saved_major,
                    NavUiRedrawFn redraw, void *data)
{
    size_t major;
    NavUiArea saved_header;
    NavUiOutputContext plain = {false, false, 0};
    if (!menus || !count || !saved_major) return NAV_UI_MENU_CANCELLED;
    major = *saved_major >= 0 && (size_t)*saved_major < count ? (size_t)*saved_major : 0;
    nav_ui_save_area(&saved_header, 0, 0, nav_term_width(), 1, &plain);
    for (;;) {
        NavUiMenu *menu = &menus[major];
        PreparedMenu prepared;
        NavUiArea saved;
        NavUiOutputContext output = nav_ui_output_context(true);
        NavUiMajor layouts[32];
        size_t selected = menu->current < menu->minor_count &&
                          !menu->minor[menu->current].separator
                              ? menu->current : first_selectable(menu);
        int row = 1, column;
        if (count > sizeof layouts / sizeof *layouts) count = sizeof layouts / sizeof *layouts;
        if (make_menu(menu, &prepared)) {
            nav_ui_restore_area(&saved_header);
            nav_ui_free_area(&saved_header);
            return NAV_UI_MENU_CANCELLED;
        }
        nav_ui_get_bar_spacing_for_style(menus, count, nav_term_width(),
                                         current_style(), layouts);
        column = layouts[major].column;
        place_menu(&row, &column, prepared.width, prepared.height);
        draw_headings(menus, count, major);
        nav_ui_save_area(&saved, column, row, prepared.width, prepared.height, &output);
        for (int line = 0; line < prepared.height; line++) {
            NavStyle style = NAV_STYLE_MENU;
            if (line > 0 && line < prepared.height - 1 &&
                menu->minor[line - 1].disabled && !menu->minor[line - 1].separator)
                style = NAV_STYLE_MENU_DISABLED;
            nav_ui_s_output(prepared.rows + line * (prepared.width + 1),
                           row + line, column, style, &output);
        }
        for (size_t index = 0; index < menu->minor_count; index++) {
            if (menu->minor[index].disabled && !menu->minor[index].separator)
                draw_item(menu, &prepared, column, row, index, menu->minor_count);
        }
        nav_ui_shadow_area(prepared.width, prepared.height, row, column, &output);
        draw_item(menu, &prepared, column, row, selected, selected);
        nav_term_hide_cursor();
        nav_term_present();
        for (;;) {
            NavAction event;
            if (nav_ui_input(NAV_CONTEXT_MENU, &event) <= 0) continue;
            if (event.type == NAV_TERM_EVENT_RESIZE) {
                menu->current = selected; *saved_major = (int)major;
                nav_ui_free_area(&saved); free_menu(&prepared, menu->minor_count);
                nav_ui_free_area(&saved_header);
                if (redraw) redraw(data);
                return NAV_UI_MENU_RESIZED;
            }
            if (event.type != NAV_TERM_EVENT_KEY) continue;
            if (event.command == NAV_CMD_CANCEL) {
                menu->current = selected; *saved_major = (int)major;
                nav_ui_restore_area(&saved); nav_ui_free_area(&saved);
                free_menu(&prepared, menu->minor_count);
                nav_ui_restore_area(&saved_header); nav_ui_free_area(&saved_header);
                nav_term_present();
                return NAV_UI_MENU_CANCELLED;
            }
            int motion = nav_ui_menu_major_motion(&event);
            if (motion || (event.command == NAV_CMD_TEXT && event.text >= '1' && event.text <= '9')) {
                menu->current = selected;
                nav_ui_restore_area(&saved); nav_ui_free_area(&saved);
                free_menu(&prepared, menu->minor_count);
                if (motion) major = nav_ui_menu_move_major(major, count, motion);
                else if ((size_t)(event.text - '1') < count) major = (size_t)(event.text - '1');
                break;
            }
            if ((event.command == NAV_CMD_DOWN || event.command == NAV_CMD_UP)) {
                size_t old = selected;
                selected = nav_ui_menu_move_minor(menu, selected,
                                                  event.command == NAV_CMD_DOWN ? 1 : -1);
                draw_item(menu, &prepared, column, row, old, selected);
                draw_item(menu, &prepared, column, row, selected, selected);
                menu->current = selected; nav_term_present();
                continue;
            }
            /* A configured modal shortcut selects the same menu action. */
            if (event.command != NAV_CMD_NONE && event.command != NAV_CMD_TEXT &&
                event.command != NAV_CMD_ACCEPT) {
                for (size_t item = 0; item < menu->minor_count; item++) {
                    if (menu->minor[item].command == (int)event.command &&
                        nav_ui_menu_activate(menu, item) != NAV_UI_MENU_CANCELLED) {
                        selected = item;
                        event.command = NAV_CMD_ACCEPT;
                        break;
                    }
                }
            }
            if (event.command == NAV_CMD_ACCEPT && selected < menu->minor_count) {
                int command = nav_ui_menu_activate(menu, selected);
                if (command == NAV_UI_MENU_CANCELLED) continue;
                menu->current = selected; *saved_major = (int)major;
                nav_ui_restore_area(&saved); nav_ui_free_area(&saved);
                free_menu(&prepared, menu->minor_count);
                nav_ui_restore_area(&saved_header); nav_ui_free_area(&saved_header);
                nav_term_present();
                return command;
            }
            if (event.command == NAV_CMD_TEXT) {
                size_t accelerated = selected;
                int command = nav_ui_menu_accelerator(menu, (int)event.text, &accelerated);
                if (command != NAV_UI_MENU_CANCELLED) {
                    menu->current = accelerated; *saved_major = (int)major;
                    nav_ui_restore_area(&saved); nav_ui_free_area(&saved);
                    free_menu(&prepared, menu->minor_count);
                    nav_ui_restore_area(&saved_header); nav_ui_free_area(&saved_header);
                    nav_term_present();
                    return command;
                }
            }
        }
    }
}
