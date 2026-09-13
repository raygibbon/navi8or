/* Navi8or presentation over a strictly read-only NavViewSource. */
#include "nav_ui.h"
#include "nav_view.h"
#include "nav_ui_core.h"
#include "nav_theme.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    NavViewer viewer;
    const NavEntry *entry;
    const NavConfig *config;
    bool highlight_current;
} ViewerScreen;

#define VIEW_ITEM(label, command, key) {label, command, NULL, false, false, key}
#define VIEW_SEPARATOR {NULL, 0, NULL, true, true, 0}
static const NavUiMenuItem viewer_file_items[] = {
    VIEW_ITEM("Properties", NAV_CMD_PROPERTIES, 'p'),
    VIEW_ITEM("Close Viewer", NAV_CMD_VIEWER_CLOSE, 'c')};
static const NavUiMenuItem viewer_view_items[] = {
    VIEW_ITEM("Line Numbers", NAV_CMD_LINES, 'l'),
    VIEW_ITEM("Wrap", NAV_CMD_WRAP, 'w'),
    VIEW_SEPARATOR,
    VIEW_ITEM("Go to Top", NAV_CMD_HOME, 't'),
    VIEW_ITEM("Go to Bottom", NAV_CMD_END, 'b')};
static const NavUiMenuItem viewer_search_items[] = {
    VIEW_ITEM("Find", NAV_CMD_FIND, 'f'),
    VIEW_ITEM("Find Next", NAV_CMD_FIND_NEXT, 'n'),
    VIEW_ITEM("Find Previous", NAV_CMD_FIND_PREVIOUS, 'p'),
    VIEW_ITEM("Go To Line", NAV_CMD_GOTO, 'g')};
static const NavUiMenuItem viewer_options_items[] = {
    {"Viewer Settings", 0, NULL, true, false, 'v'},
    {"Theme", 0, NULL, true, false, 't'}};
static const NavUiMenuItem viewer_help_items[] = {
    VIEW_ITEM("Viewer Keys", NAV_CMD_HELP, 'k'),
    {"About Navi8or", 0, NULL, true, false, 'a'}};
static NavUiMenu viewer_menus[] = {
    {"File", viewer_file_items, sizeof viewer_file_items / sizeof *viewer_file_items, 0},
    {"View", viewer_view_items, sizeof viewer_view_items / sizeof *viewer_view_items, 0},
    {"Search", viewer_search_items, sizeof viewer_search_items / sizeof *viewer_search_items, 0},
    {"Options", viewer_options_items, sizeof viewer_options_items / sizeof *viewer_options_items, 0},
    {"Help", viewer_help_items, sizeof viewer_help_items / sizeof *viewer_help_items, 0}};

static size_t viewer_page(void)
{
    int height = nav_term_height();
    NavShellLayout layout = nav_shell_layout(nav_term_width(), height);
    int rows = layout.workspace_bottom - layout.workspace_top;
    return rows > 0 ? (size_t)rows : 1;
}

static size_t line_span(NavViewer *viewer, size_t line, size_t width)
{
    size_t length = viewer->source->line_length(viewer->source, line);
    if (!width)
        return 1;
    return length ? (length + width - 1) / width : 1;
}

static void ensure_wrapped(NavViewer *viewer, size_t width, size_t page)
{
    size_t top = viewer->current_line, used = line_span(viewer, top, width);
    while (top > 0)
    {
        size_t span = line_span(viewer, top - 1, width);
        if (used + span > page)
            break;
        used += span;
        top--;
    }
    viewer->top_line = top;
}

/* Inspect only rows which will be rendered.  Range-backed sources cache these
   reads, and this lets one shared gutter cover a digit boundary in-frame. */
static void prepare_remote_gutter(NavViewer *viewer, size_t rows)
{
    NavViewCursor cursor = viewer->top_cursor;
    for (size_t row = 0; row < rows; row++) {
        long moved = 0;
        size_t length = 0;
        if (!viewer->source->cursor_line(viewer->source, &cursor, &length))
            break;
        nav_viewer_note_visible_ordinal(viewer, &cursor);
        if (viewer->source->cursor_move(viewer->source, &cursor, 1, &moved) ||
            !moved)
            break;
    }
}

static void draw_fragment(NavViewer *viewer, const char *line, size_t length,
                          size_t logical, size_t start, int x, int y, int width,
                          NavStyle style)
{
    size_t available = start < length ? length - start : 0;
    int amount = available < (size_t)width ? (int)available : width;
    nav_ui_text(x, y, width, amount ? line + start : "", style);
    if (logical == viewer->match_line && viewer->match_length)
    {
        size_t match_start = viewer->match_column, match_end = match_start + viewer->match_length;
        size_t visible_end = start + (size_t)width;
        size_t from = match_start > start ? match_start : start;
        size_t to = match_end < visible_end ? match_end : visible_end;
        if (from < to && from < length)
        {
            if (to > length)
                to = length;
            nav_ui_text(x + (int)(from - start), y, (int)(to - from), line + from, NAV_STYLE_VIEWER_SEARCH_MATCH);
        }
    }
}

static void draw_viewer(void *data)
{
    ViewerScreen *screen = data;
    NavViewer *viewer = &screen->viewer;
    bool cursor_mode = viewer->source->cursor_line != NULL;
    size_t count = cursor_mode ? 0 : viewer->source->line_count(viewer->source);
    int width = nav_term_width(), height = nav_term_height();
    NavShellLayout layout = nav_shell_layout(width, height);
    nav_term_clear(NAV_STYLE_BACKGROUND);
    if (width < 20 || height < 8)
    {
        nav_ui_text(0, 0, width, "Terminal too small", NAV_STYLE_MENU);
        nav_term_hide_cursor();
        return;
    }
    nav_ui_draw_menu_bar(viewer_menus, sizeof viewer_menus / sizeof *viewer_menus);
    size_t gutter = nav_viewer_line_number_width(viewer), page = viewer_page();
    if (cursor_mode && viewer->line_numbers) {
        prepare_remote_gutter(viewer, page);
        gutter = nav_viewer_line_number_width(viewer);
    }
    int text_width = width - (int)gutter;
    if (text_width < 1)
        text_width = 1;
    if (viewer->wrap && !cursor_mode)
        ensure_wrapped(viewer, (size_t)text_width, page);
    else
        nav_viewer_ensure_visible(viewer, page);
    {
        char metadata[96];
        const NavTheme *theme = nav_term_theme();
        const char *title = theme && theme->style == NAV_UI_STYLE_CLASSIC
                                ? screen->entry->resource_id
                                : screen->entry->name;
        if (cursor_mode) {
            uint64_t offset = 0, total = 0; bool known = false;
            viewer->source->position(viewer->source, &viewer->cursor,
                                     &offset, &total, &known);
            if (known) snprintf(metadata, sizeof metadata, "%llu bytes %s",
                                (unsigned long long)total,
                                viewer->wrap ? "wrap" : "no-wrap");
            else snprintf(metadata, sizeof metadata, "remote size unknown %s",
                          viewer->wrap ? "wrap" : "no-wrap");
        } else snprintf(metadata, sizeof metadata, "%zu lines %s", count,
                        viewer->wrap ? "wrap" : "no-wrap");
        nav_ui_window_header(0, layout.workspace_top, width, 1, 'V', title, metadata, true);
    }
    int row = layout.workspace_top + 1;
    char status[512];
    if (cursor_mode) {
        NavViewCursor cursor = viewer->top_cursor;
        while (row <= layout.workspace_bottom) {
            size_t length = 0;
            const char *line = viewer->source->cursor_line(viewer->source,
                                                            &cursor, &length);
            long moved = 0;
            bool current = cursor.offset == viewer->cursor.offset;
            if (!line) {
                if (viewer->source->last_error) {
                    const char *error = viewer->source->last_error(viewer->source);
                    if (error && error[0])
                        snprintf(viewer->status, sizeof viewer->status, "%s", error);
                }
                break;
            }
            size_t segments = viewer->wrap && text_width > 0 ?
                              (length ? (length + (size_t)text_width - 1) /
                                        (size_t)text_width : 1) : 1;
            for (size_t segment = 0; segment < segments && row <= layout.workspace_bottom;
                 segment++, row++) {
                NavStyle style = screen->highlight_current && current ?
                                 NAV_STYLE_ACCENT : NAV_STYLE_TEXT;
                if (gutter) {
                    char number[32] = {0}, digits[24] = "?";
                    size_t digit_count;
                    memset(number, ' ', gutter < sizeof number - 1 ? gutter :
                           sizeof number - 1);
                    if (segment == 0) {
                        number[0] = current ? '>' : ' ';
                        if (cursor.ordinal_known)
                            snprintf(digits, sizeof digits, "%llu",
                                     (unsigned long long)cursor.ordinal + 1);
                        digit_count = strlen(digits);
                        if (digit_count + 1 < gutter)
                            memcpy(number + gutter - 1 - digit_count, digits,
                                   digit_count);
                    }
                    nav_ui_text(0, row, (int)gutter, number,
                                current ? NAV_STYLE_ACCENT :
                                NAV_STYLE_VIEWER_LINE_NUMBER);
                }
                size_t start = viewer->wrap ? segment * (size_t)text_width :
                               viewer->horizontal_offset;
                draw_fragment(viewer, line, length,
                              cursor.ordinal_known ? (size_t)cursor.ordinal : SIZE_MAX,
                              start, (int)gutter, row, text_width, style);
            }
            if (viewer->source->cursor_move(viewer->source, &cursor, 1,
                                            &moved) || !moved) break;
        }
        {
            uint64_t offset = 0, total = 0; bool known = false;
            viewer->source->position(viewer->source, &viewer->cursor,
                                     &offset, &total, &known);
            unsigned percent = known && total ?
                (unsigned)(offset <= total ?
                           (long double)offset * 100.0L / (long double)total :
                           100.0L) : 0;
            snprintf(status, sizeof status,
                     " Byte %llu%s%llu Col %zu%s%u%%%s%s  Esc Back  / Find  F5 Next  F6 Prev",
                     (unsigned long long)offset, known ? "/" : "",
                     (unsigned long long)(known ? total : 0),
                     viewer->horizontal_offset + 1, known ? " " : "", percent,
                     viewer->status[0] ? "  " : "", viewer->status);
        }
    } else {
        size_t logical = viewer->top_line;
        while (logical < count && row <= layout.workspace_bottom) {
            size_t length = 0;
            const char *line = viewer->source->line(viewer->source, logical, &length);
            if (!line) line = "";
            size_t segments = viewer->wrap ? line_span(viewer, logical, (size_t)text_width) : 1;
            for (size_t segment = 0; segment < segments && row <= layout.workspace_bottom; segment++, row++) {
                NavStyle style = screen->highlight_current && viewer->current_line == logical ? NAV_STYLE_ACCENT : NAV_STYLE_TEXT;
                if (gutter) {
                    char number[32] = {0}, digits[24]; size_t digit_count;
                    memset(number, ' ', gutter < sizeof number - 1 ? gutter : sizeof number - 1);
                    if (segment == 0) {
                        number[0] = logical == viewer->current_line ? '>' : ' ';
                        snprintf(digits, sizeof digits, "%zu", logical + 1);
                        digit_count = strlen(digits);
                        if (digit_count + 1 < gutter) memcpy(number + gutter - 1 - digit_count, digits, digit_count);
                    }
                    nav_ui_text(0, row, (int)gutter, number, style == NAV_STYLE_ACCENT ? NAV_STYLE_ACCENT : NAV_STYLE_VIEWER_LINE_NUMBER);
                }
                size_t start = viewer->wrap ? segment * (size_t)text_width : viewer->horizontal_offset;
                draw_fragment(viewer, line, length, logical, start, (int)gutter, row, text_width, style);
            }
            logical++;
        }
        size_t shown = count ? viewer->current_line + 1 : 0;
        unsigned percent = count ? (unsigned)(shown * 100 / count) : 0;
        snprintf(status, sizeof status, " Ln %zu/%zu Col %zu %u%%  Esc Back  / Find  F5 Next  F6 Prev  g GoTo  w Wrap  l Lines%s%s",
                 shown, count, viewer->horizontal_offset + 1, percent,
                 viewer->status[0] ? "  " : "", viewer->status);
    }
    nav_ui_text(0, layout.status_row, width, status, NAV_STYLE_STATUS);
    nav_ui_command_bar(layout.command_row, width, NAV_CONTEXT_VIEWER, NULL, NULL);
    nav_term_hide_cursor();
}

static void find_prompt(ViewerScreen *screen)
{
    char original[NAV_SEARCH_MAX];
    bool wrapped = false;
    snprintf(original, sizeof original, "%s", screen->viewer.search);
    if (nav_ui_prompt_text(" Find ", "Find: ", screen->viewer.search,
                        sizeof screen->viewer.search, draw_viewer, screen) != 0)
    {
        snprintf(screen->viewer.search, sizeof screen->viewer.search, "%s", original);
        return;
    }
    nav_viewer_find(&screen->viewer, 1, viewer_page(), &wrapped);
}

static void goto_prompt(ViewerScreen *screen)
{
    char answer[32], *end;
    unsigned long long line;
    snprintf(answer, sizeof answer, "%zu", screen->viewer.current_line + 1);
    if (nav_ui_prompt_text(" Go To Line ", "Line: ", answer, sizeof answer, draw_viewer, screen) != 0)
        return;
    line = strtoull(answer, &end, 10);
    if (end == answer || *end)
    {
        snprintf(screen->viewer.status, sizeof screen->viewer.status, "Invalid line number");
        return;
    }
    nav_viewer_goto_line(&screen->viewer, (size_t)line, viewer_page());
    screen->viewer.match_line = SIZE_MAX;
    screen->viewer.match_length = 0;
    snprintf(screen->viewer.status, sizeof screen->viewer.status, "Line %zu", screen->viewer.current_line + 1);
}

static void viewer_help(void)
{ nav_ui_binding_help(NAV_CONTEXT_VIEWER); }

static bool viewer_dispatch(ViewerScreen *, NavCommand);
static bool viewer_menu(ViewerScreen *screen)
{
    static int saved_major;
    if (screen->config && !screen->config->menu_remember_position)
    {
        saved_major = 0;
        for (size_t index = 0; index < sizeof viewer_menus / sizeof *viewer_menus; index++)
            viewer_menus[index].current = 0;
    }
    NavCommand command = nav_ui_pull_down(viewer_menus, sizeof viewer_menus / sizeof *viewer_menus,
                                  &saved_major, draw_viewer, screen);
    return viewer_dispatch(screen, command);
}

static bool viewer_dispatch(ViewerScreen *screen, NavCommand command)
{
    size_t page = viewer_page();
    bool wrapped = false;
    switch (command)
    {
    case NAV_CMD_QUIT: nav_ui_request_quit(); return true;
    case NAV_CMD_MENU: return viewer_menu(screen);
    case NAV_CMD_UP: nav_viewer_move(&screen->viewer, -1, page); break;
    case NAV_CMD_DOWN: nav_viewer_move(&screen->viewer, 1, page); break;
    case NAV_CMD_PAGE_UP: nav_viewer_page(&screen->viewer, -1, page); break;
    case NAV_CMD_PAGE_DOWN: nav_viewer_page(&screen->viewer, 1, page); break;
    case NAV_CMD_LEFT:
    case NAV_CMD_LEFT_FAST:
        if (!screen->viewer.wrap) nav_viewer_horizontal(&screen->viewer, command == NAV_CMD_LEFT ? -1 : -8);
        break;
    case NAV_CMD_RIGHT:
    case NAV_CMD_RIGHT_FAST:
        if (!screen->viewer.wrap) nav_viewer_horizontal(&screen->viewer, command == NAV_CMD_RIGHT ? 1 : 8);
        break;
    case NAV_CMD_PROPERTIES:
        nav_show_properties(screen->entry, "Type:      Text");
        break;
    case NAV_CMD_VIEWER_CLOSE:
        return true;
    case NAV_CMD_LINES:
        screen->viewer.line_numbers = !screen->viewer.line_numbers;
        break;
    case NAV_CMD_WRAP:
        screen->viewer.wrap = !screen->viewer.wrap;
        screen->viewer.horizontal_offset = 0;
        break;
    case NAV_CMD_HOME:
        nav_viewer_top(&screen->viewer);
        break;
    case NAV_CMD_END:
        nav_viewer_bottom(&screen->viewer, page);
        break;
    case NAV_CMD_FIND:
        find_prompt(screen);
        break;
    case NAV_CMD_FIND_NEXT:
        nav_viewer_find(&screen->viewer, 1, page, &wrapped);
        break;
    case NAV_CMD_FIND_PREVIOUS:
        nav_viewer_find(&screen->viewer, -1, page, &wrapped);
        break;
    case NAV_CMD_GOTO:
        goto_prompt(screen);
        break;
    case NAV_CMD_HELP:
        viewer_help();
        break;
    default:
        break;
    }
    return false;
}

int nav_view_file(NavProvider *provider, const NavEntry *entry, const NavConfig *config)
{
    char error[256] = {0};
    bool binary = false;
    NavViewSource *source = nav_view_source_open_provider(provider, entry->resource_id, &binary, error, sizeof error);
    if (!source)
    {
        if (binary)
            nav_show_properties(entry, "Type:      Binary");
        else
        {
            const char *lines[] = {error[0] ? error : "Unable to open file"};
            nav_ui_info(" Viewer Error ", lines, 1);
        }
        return 0;
    }
    if (source->cursor_line && source->last_error) {
        NavViewCursor first = {0};
        size_t ignored = 0;
        source->cursor_top(source, &first);
        if (!source->cursor_line(source, &first, &ignored)) {
            const char *source_error = source->last_error(source);
            const char *lines[] = {source_error && source_error[0] ? source_error :
                                   "Unable to read remote line"};
            nav_ui_info(" Viewer Error ", lines, 1);
            source->close(source);
            return 0;
        }
    }
    ViewerScreen screen = {.entry = entry, .config = config, .highlight_current = !config || config->viewer_current_line};
    nav_viewer_init(&screen.viewer, source);
    if (config)
    {
        screen.viewer.line_numbers = config->viewer_line_numbers;
        screen.viewer.wrap = config->viewer_wrap;
    }
    NavInputContext previous = nav_ui_workspace(NAV_CONTEXT_VIEWER);
    for (;;)
    {
        NavAction event;
        draw_viewer(&screen);
        nav_term_present();
        if (nav_ui_input(NAV_CONTEXT_VIEWER, &event) <= 0)
            continue;
        if (event.type == NAV_TERM_EVENT_RESIZE)
            continue;
        if (event.type != NAV_TERM_EVENT_KEY)
            continue;
        if (viewer_dispatch(&screen, event.command)) break;
    }
    nav_ui_workspace(previous);
    source->close(source);
    return 0;
}
