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

enum
{
    VIEWER_MENU_PROPERTIES = 1,
    VIEWER_MENU_CLOSE,
    VIEWER_MENU_LINES,
    VIEWER_MENU_WRAP,
    VIEWER_MENU_TOP,
    VIEWER_MENU_BOTTOM,
    VIEWER_MENU_FIND,
    VIEWER_MENU_NEXT,
    VIEWER_MENU_PREVIOUS,
    VIEWER_MENU_GOTO,
    VIEWER_MENU_KEYS
};

#define VIEW_ITEM(label, command, key, binding) {label, command, NULL, false, false, key, binding}
#define VIEW_SEPARATOR {NULL, 0, NULL, true, true, 0, NULL}
static const NavUiMenuItem viewer_file_items[] = {
    VIEW_ITEM("Properties", VIEWER_MENU_PROPERTIES, 'p', NULL),
    VIEW_ITEM("Close Viewer", VIEWER_MENU_CLOSE, 'c', "Esc")};
static const NavUiMenuItem viewer_view_items[] = {
    VIEW_ITEM("Line Numbers", VIEWER_MENU_LINES, 'l', "L"),
    VIEW_ITEM("Wrap", VIEWER_MENU_WRAP, 'w', "W"),
    VIEW_SEPARATOR,
    VIEW_ITEM("Go to Top", VIEWER_MENU_TOP, 't', "Home"),
    VIEW_ITEM("Go to Bottom", VIEWER_MENU_BOTTOM, 'b', "End")};
static const NavUiMenuItem viewer_search_items[] = {
    VIEW_ITEM("Find", VIEWER_MENU_FIND, 'f', "/"),
    VIEW_ITEM("Find Next", VIEWER_MENU_NEXT, 'n', "F5"),
    VIEW_ITEM("Find Previous", VIEWER_MENU_PREVIOUS, 'p', "F6"),
    VIEW_ITEM("Go To Line", VIEWER_MENU_GOTO, 'g', "G")};
static const NavUiMenuItem viewer_options_items[] = {
    {"Viewer Settings", 0, NULL, true, false, 'v', NULL},
    {"Theme", 0, NULL, true, false, 't', NULL}};
static const NavUiMenuItem viewer_help_items[] = {
    VIEW_ITEM("Viewer Keys", VIEWER_MENU_KEYS, 'k', "F1"),
    {"About Navi8or", 0, NULL, true, false, 'a', NULL}};
static NavUiMenu viewer_menus[] = {
    {"File", viewer_file_items, sizeof viewer_file_items / sizeof *viewer_file_items, 0},
    {"View", viewer_view_items, sizeof viewer_view_items / sizeof *viewer_view_items, 0},
    {"Search", viewer_search_items, sizeof viewer_search_items / sizeof *viewer_search_items, 0},
    {"Options", viewer_options_items, sizeof viewer_options_items / sizeof *viewer_options_items, 0},
    {"Help", viewer_help_items, sizeof viewer_help_items / sizeof *viewer_help_items, 0}};

static size_t viewer_page(void)
{
    int height = nav_term_height();
    return height > 2 ? (size_t)height - 2 : 1;
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
    nav_term_clear(NAV_STYLE_BACKGROUND);
    if (width < 20 || height < 8)
    {
        nav_ui_text(0, 0, width, "Terminal too small", NAV_STYLE_MENU);
        nav_term_hide_cursor();
        return;
    }
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
        nav_ui_window_header(0, width, 1, 'V', title, metadata, true);
    }
    int row = 1;
    char status[512];
    if (cursor_mode) {
        NavViewCursor cursor = viewer->top_cursor;
        while (row < height - 1) {
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
            for (size_t segment = 0; segment < segments && row < height - 1;
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
        while (logical < count && row < height - 1) {
            size_t length = 0;
            const char *line = viewer->source->line(viewer->source, logical, &length);
            if (!line) line = "";
            size_t segments = viewer->wrap ? line_span(viewer, logical, (size_t)text_width) : 1;
            for (size_t segment = 0; segment < segments && row < height - 1; segment++, row++) {
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
    nav_ui_mode_line(status);
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
{
    static const char *lines[] = {
        "Viewer Keys", "",
        "Up/Down       Move line", "PgUp/PgDn     Scroll page",
        "Home/End      Top/bottom", "Left/Right    Horizontal scroll",
        "Ctrl+Left/Right  Scroll by eight columns",
        "/             Find", "F5/F6         Find next/previous",
        "g             Go to line", "w             Toggle wrap",
        "l             Toggle line numbers", "Ctrl+\\        Viewer menu",
        "Esc           Close viewer"};
    nav_ui_info(" Viewer Help ", lines, sizeof lines / sizeof *lines);
}

static bool viewer_menu(ViewerScreen *screen)
{
    static int saved_major;
    if (screen->config && !screen->config->menu_remember_position)
    {
        saved_major = 0;
        for (size_t index = 0; index < sizeof viewer_menus / sizeof *viewer_menus; index++)
            viewer_menus[index].current = 0;
    }
    int command = nav_ui_pull_down(viewer_menus, sizeof viewer_menus / sizeof *viewer_menus,
                                  &saved_major, draw_viewer, screen);
    size_t page = viewer_page();
    bool wrapped = false;
    switch (command)
    {
    case VIEWER_MENU_PROPERTIES:
        nav_show_properties(screen->entry, "Type:      Text");
        break;
    case VIEWER_MENU_CLOSE:
        return true;
    case VIEWER_MENU_LINES:
        screen->viewer.line_numbers = !screen->viewer.line_numbers;
        break;
    case VIEWER_MENU_WRAP:
        screen->viewer.wrap = !screen->viewer.wrap;
        screen->viewer.horizontal_offset = 0;
        break;
    case VIEWER_MENU_TOP:
        nav_viewer_top(&screen->viewer);
        break;
    case VIEWER_MENU_BOTTOM:
        nav_viewer_bottom(&screen->viewer, page);
        break;
    case VIEWER_MENU_FIND:
        find_prompt(screen);
        break;
    case VIEWER_MENU_NEXT:
        nav_viewer_find(&screen->viewer, 1, page, &wrapped);
        break;
    case VIEWER_MENU_PREVIOUS:
        nav_viewer_find(&screen->viewer, -1, page, &wrapped);
        break;
    case VIEWER_MENU_GOTO:
        goto_prompt(screen);
        break;
    case VIEWER_MENU_KEYS:
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
    bool binary = false, wrapped = false;
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
    for (;;)
    {
        NavTermEvent event;
        size_t page = viewer_page();
        draw_viewer(&screen);
        nav_term_present();
        if (nav_term_poll_event(&event, -1) <= 0)
            continue;
        if (event.type == NAV_TERM_EVENT_RESIZE)
            continue;
        if (event.type != NAV_TERM_EVENT_KEY)
            continue;
        if (event.key == NAV_KEY_ESCAPE)
            break;
        if (event.key == '\\' && (event.modifiers & NAV_MOD_CTRL))
        {
            if (viewer_menu(&screen))
                break;
            continue;
        }
        if (event.key == NAV_KEY_UP && event.modifiers == 0)
            nav_viewer_move(&screen.viewer, -1, page);
        else if (event.key == NAV_KEY_DOWN && event.modifiers == 0)
            nav_viewer_move(&screen.viewer, 1, page);
        else if (event.key == NAV_KEY_PAGE_UP && event.modifiers == 0)
            nav_viewer_page(&screen.viewer, -1, page);
        else if (event.key == NAV_KEY_PAGE_DOWN && event.modifiers == 0)
            nav_viewer_page(&screen.viewer, 1, page);
        else if ((event.key == NAV_KEY_HOME && event.modifiers == 0) || (event.key == NAV_KEY_HOME && (event.modifiers & NAV_MOD_CTRL)))
            nav_viewer_top(&screen.viewer);
        else if ((event.key == NAV_KEY_END && event.modifiers == 0) || (event.key == NAV_KEY_END && (event.modifiers & NAV_MOD_CTRL)))
            nav_viewer_bottom(&screen.viewer, page);
        else if (!screen.viewer.wrap && event.key == NAV_KEY_LEFT)
            nav_viewer_horizontal(&screen.viewer, event.modifiers & NAV_MOD_CTRL ? -8 : -1);
        else if (!screen.viewer.wrap && event.key == NAV_KEY_RIGHT)
            nav_viewer_horizontal(&screen.viewer, event.modifiers & NAV_MOD_CTRL ? 8 : 1);
        else if (event.key == '/' && event.modifiers == 0)
            find_prompt(&screen);
        else if (event.key == NAV_KEY_F5)
            nav_viewer_find(&screen.viewer, 1, page, &wrapped);
        else if (event.key == NAV_KEY_F6)
            nav_viewer_find(&screen.viewer, -1, page, &wrapped);
        else if ((event.key == 'g' || event.key == 'G') && event.modifiers == 0)
            goto_prompt(&screen);
        else if ((event.key == 'w' || event.key == 'W') && event.modifiers == 0)
        {
            screen.viewer.wrap = !screen.viewer.wrap;
            screen.viewer.horizontal_offset = 0;
            snprintf(screen.viewer.status, sizeof screen.viewer.status, "Wrap %s", screen.viewer.wrap ? "on" : "off");
        }
        else if ((event.key == 'l' || event.key == 'L') && event.modifiers == 0)
        {
            screen.viewer.line_numbers = !screen.viewer.line_numbers;
            snprintf(screen.viewer.status, sizeof screen.viewer.status, "Line numbers %s", screen.viewer.line_numbers ? "on" : "off");
        }
    }
    source->close(source);
    return 0;
}
