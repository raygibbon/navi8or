/* Adapted from TDX utils.c:create_frame()/show_window_header(),
 * window.c:show_vertical_separator(), and termbox_backend.c region routines. */
#include "nav_ui_core.h"
#include "nav_theme.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GC_VERTICAL, GC_LEFT_DOWN, GC_BOTTOM_T, GC_RIGHT_DOWN, GC_LEFT_T,
       GC_CROSS, GC_RIGHT_T, GC_LEFT_UP, GC_TOP_T, GC_RIGHT_UP, GC_HORIZONTAL };

/* Exact CP437 graphic_char[][] data from TDX src/core/global.c. */
static const uint8_t graphic_char[6][11] = {
    {'|','+','+','+','+','+','+','+','+','+','-'},
    {0xb3,0xc0,0xc1,0xd9,0xc3,0xc5,0xb4,0xda,0xc2,0xbf,0xc4},
    {0xba,0xc8,0xca,0xbc,0xcc,0xce,0xb9,0xc9,0xcb,0xbb,0xcd},
    {0xba,0xd3,0xd0,0xbd,0xc7,0xd7,0xb6,0xd6,0xd2,0xb7,0xc4},
    {0xb3,0xd4,0xcf,0xbe,0xc6,0xd8,0xb5,0xd5,0xd1,0xb8,0xcd},
    {0xb0,0xb1,0xdc,0xb2,0xde,0xdb,0xdd,0xae,0xdf,0xaf,0xfe}};

NavUiOutputContext nav_ui_output_context(bool framed_output)
{
    const NavTheme *theme = nav_term_theme();
    NavUiOutputContext context = {
        framed_output && theme && theme->frame_space,
        theme && theme->shadow,
        theme && theme->shadow_width > 0 ? theme->shadow_width : 1};
    return context;
}

int nav_ui_save_area(NavUiArea *saved, int col, int row, int width, int height,
                    const NavUiOutputContext *context)
{
    NavUiAreaGeometry area;
    if (!saved) return -1;
    memset(saved, 0, sizeof *saved);
    nav_ui_adjust_area(&area, width, height, row, col, nav_term_width(),
                      nav_term_height(), context);
    if (area.width <= 0 || area.height <= 0) return 0;
    saved->cells = malloc((size_t)area.width * (size_t)area.height * sizeof *saved->cells);
    if (!saved->cells) return -1;
    saved->x = area.col; saved->y = area.row;
    saved->width = area.width; saved->height = area.height;
    for (int y = 0; y < saved->height; y++)
        for (int x = 0; x < saved->width; x++)
            nav_term_get_cell(saved->x + x, saved->y + y,
                              &saved->cells[y * saved->width + x]);
    return 0;
}

void nav_ui_restore_area(NavUiArea *area)
{
    if (!area || !area->cells) return;
    for (int row = 0; row < area->height; row++)
        for (int col = 0; col < area->width; col++)
            nav_term_set_cell(area->x + col, area->y + row,
                              &area->cells[row * area->width + col]);
}

void nav_ui_free_area(NavUiArea *area)
{
    if (!area) return;
    free(area->cells);
    memset(area, 0, sizeof *area);
}

void nav_ui_s_output(const char *text, int row, int col, NavStyle style,
                    const NavUiOutputContext *context)
{
    int max = nav_term_width();
    if (!text) return;
    if (context && context->output_space && col != 0)
        nav_term_glyph(col - 1, row, ' ', style);
    while (*text && col < max)
        nav_term_glyph(col++, row, (unsigned char)*text++, style);
    if (context && context->output_space && col < max)
        nav_term_glyph(col, row, ' ', style);
}

/* Literal geometry and loop structure of TDX termbox_backend.c:shadow_area(). */
void nav_ui_shadow_area(int width, int height, int row, int col,
                       const NavUiOutputContext *context)
{
    NavUiAreaGeometry area;
    int original_height = height;
    if (!context || !context->shadow) return;
    nav_ui_adjust_area(&area, width, height, row, col, nav_term_width(),
                      nav_term_height(), context);
    for (int y = 1; y < area.height; y++)
        for (int x = 0; x < context->shadow_width; x++) {
            NavTermCell cell;
            if (!nav_term_get_cell(area.col + area.width - 1 - x,
                                   area.row + y, &cell)) {
                cell.foreground = 240; cell.background = 16;
                nav_term_set_cell(area.col + area.width - 1 - x,
                                  area.row + y, &cell);
            }
        }
    if (area.height > original_height) {
        for (int x = context->shadow_width; x < area.width; x++) {
            NavTermCell cell;
            if (!nav_term_get_cell(area.col + x, area.row + area.height - 1, &cell)) {
                cell.foreground = 240; cell.background = 16;
                nav_term_set_cell(area.col + x, area.row + area.height - 1, &cell);
            }
        }
    }
}

/* TDX create_frame(..., x = -1): complete NUL-terminated CP437 rows. */
char *nav_ui_create_frame_rows(int width, int height, const int *separators,
                              size_t separator_count)
{
    const NavTheme *theme = nav_term_theme();
    int frame_style = theme ? (int)theme->frame_style : NAV_FRAME_COMBINE;
    const uint8_t *inside, *outside;
    char *rows;
    if (width < 3 || height < 2) return NULL;
    if (frame_style < NAV_FRAME_ASCII || frame_style > NAV_FRAME_BLOCK)
        frame_style = NAV_FRAME_COMBINE;
    inside = graphic_char[frame_style];
    outside = frame_style < NAV_FRAME_COMBINE ? inside : graphic_char[NAV_FRAME_DOUBLE];
    rows = malloc((size_t)(width + 1) * (size_t)height);
    if (!rows) return NULL;
    for (int row = 0; row < height; row++) {
        char *line = rows + row * (width + 1);
        bool separator = false;
        for (size_t index = 0; index < separator_count; index++)
            if (separators[index] == row) separator = true;
        memset(line, ' ', (size_t)width);
        line[width] = 0;
        if (row == 0) {
            line[0] = (char)outside[GC_LEFT_UP];
            memset(line + 1, outside[GC_HORIZONTAL], (size_t)width - 2);
            line[width - 1] = (char)outside[GC_RIGHT_UP];
        } else if (row == height - 1) {
            line[0] = (char)outside[GC_LEFT_DOWN];
            memset(line + 1, outside[GC_HORIZONTAL], (size_t)width - 2);
            line[width - 1] = (char)outside[GC_RIGHT_DOWN];
        } else if (separator) {
            line[0] = (char)inside[GC_LEFT_T];
            memset(line + 1, inside[GC_HORIZONTAL], (size_t)width - 2);
            line[width - 1] = (char)inside[GC_RIGHT_T];
        } else {
            line[0] = line[width - 1] = (char)inside[GC_VERTICAL];
        }
    }
    return rows;
}

void nav_ui_frame(int x, int y, int width, int height, const int *separators,
                 size_t separator_count, const char *title, NavStyle style)
{
    NavUiOutputContext context = nav_ui_output_context(true);
    char *rows = nav_ui_create_frame_rows(width, height, separators, separator_count);
    if (!rows) return;
    for (int row = 0; row < height; row++)
        nav_ui_s_output(rows + row * (width + 1), y + row, x, style, &context);
    if (title && *title && width > 4)
        nav_ui_text(x + 2, y, width - 4, title, NAV_STYLE_DIALOG_TITLE);
    nav_ui_shadow_area(width, height, y, x, &context);
    free(rows);
}

void nav_ui_window_header(int x, int y, int width, int number, char letter,
                         const char *name, const char *metadata, bool active)
{
    char identity[5], right[13];
    int field_width, field_col, name_width;
    const NavTheme *theme = nav_term_theme();
    if (width <= 0) return;
    nav_ui_text(x, y, width, "", NAV_STYLE_HEADER);
    if (!theme || theme->style == NAV_UI_STYLE_MODERN) {
        field_width = width >= 20 ? 12 : 0;
        name_width = width - field_width - 2;
        if (name_width > 0)
            nav_ui_text(x + 1, y, name_width, name ? name : "",
                        NAV_STYLE_HEADER);
        if (field_width > 0) {
            snprintf(right, sizeof right, "%12.12s", metadata ? metadata : "");
            nav_ui_text(x + width - field_width, y, field_width, right,
                        NAV_STYLE_HEADER);
        }
        return;
    }
    snprintf(identity, sizeof identity, "%2d%c", number % 100,
             active ? (char)toupper((unsigned char)letter) : (char)tolower((unsigned char)letter));
    nav_ui_text(x, y, width < 3 ? width : 3, identity, NAV_STYLE_HEADER);
    /* show_window_fname() starts at left+5 and reserves the final 12 columns
     * for show_line_col(). Navi8or maps that fixed field to sort/count/filter. */
    field_width = width >= 16 ? 12 : width - 4;
    if (field_width < 0) field_width = 0;
    field_col = x + width - field_width;
    name_width = width - 18;
    if (name_width > 0) nav_ui_text(x + 5, y, name_width, name ? name : "", NAV_STYLE_HEADER);
    if (field_width > 0) {
        snprintf(right, sizeof right, "%12.12s", metadata ? metadata : "");
        nav_ui_text(field_col, y, field_width,
                    right + (12 - field_width), NAV_STYLE_HEADER);
    }
}

void nav_ui_vertical_separator(int x, int top, int bottom)
{
    if (bottom >= top)
        nav_term_vline(x, top, 0xba, bottom - top + 1, NAV_STYLE_HEADER);
}

void nav_ui_mode_line(const char *text)
{
    int row = nav_term_height() - 1;
    if (row >= 0) nav_ui_text(0, row, nav_term_width(), text ? text : "", NAV_STYLE_STATUS);
}
