/* Navi8or presentation over a strictly read-only NavViewSource. */
#include "nav_ui.h"
#include "nav_view.h"
#include "nav_ui_core.h"
#include "nav_theme.h"
#include "nav_clipboard.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct NavPaneViewer
{
    NavViewer viewer;
    const NavEntry *entry;
    const NavConfig *config;
    bool highlight_current;
    NavProvider *provider;
    const NavLocation *origin;
    NavEntry resource;
    NavApp *app;
    NavUiRedrawFn redraw;
    void *redraw_data;
    int pane;
    bool fullscreen, owned;
    struct { NavViewer viewer; NavEntry entry; NavProvider *provider; bool owned; } history[16];
    size_t history_count;
    NavLocation launch_location;
    NavUiMenu menus[5];
    int saved_major;
};
typedef struct NavPaneViewer ViewerScreen;

#define VIEW_ITEM(label, command, key) {label, command, NULL, false, false, key}
#define VIEW_SEPARATOR {NULL, 0, NULL, true, true, 0}
static const NavUiMenuItem viewer_file_items[] = {
    VIEW_ITEM("Back", NAV_CMD_VIEWER_BACK, 'b'),
    VIEW_ITEM("Open Link...", NAV_CMD_VIEWER_OPEN_LINK, 'o'),
    VIEW_ITEM("Download / Save Copy...", NAV_CMD_DOWNLOAD, 'd'),
    VIEW_ITEM("Properties", NAV_CMD_PROPERTIES, 'p'),
    VIEW_ITEM("Close Viewer", NAV_CMD_VIEWER_CLOSE, 'c')};
static const NavUiMenuItem viewer_view_items[] = {
    VIEW_ITEM("Toggle Fullscreen", NAV_CMD_VIEWER_FULLSCREEN, 'f'),
    VIEW_ITEM("Next Link", NAV_CMD_VIEWER_NEXT_LINK, 'n'),
    VIEW_ITEM("Previous Link", NAV_CMD_VIEWER_PREVIOUS_LINK, 'p'),
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
static const NavUiMenu viewer_menus[] = {
    {"File", viewer_file_items, sizeof viewer_file_items / sizeof *viewer_file_items, 0},
    {"View", viewer_view_items, sizeof viewer_view_items / sizeof *viewer_view_items, 0},
    {"Search", viewer_search_items, sizeof viewer_search_items / sizeof *viewer_search_items, 0},
    {"Options", viewer_options_items, sizeof viewer_options_items / sizeof *viewer_options_items, 0},
    {"Help", viewer_help_items, sizeof viewer_help_items / sizeof *viewer_help_items, 0}};

typedef struct { int x, width, top, bottom, status; } ViewerArea;
static ViewerArea viewer_area(const ViewerScreen *screen)
{
    int width = nav_term_width(), height = nav_term_height();
    NavShellLayout layout = nav_shell_layout_for_config(width, height, screen->config);
    ViewerArea area = {0, width, layout.workspace_top, layout.workspace_bottom, layout.status_row};
    if (screen->app && !screen->fullscreen) {
        NavCommanderLayout panes;
        const NavTheme *theme = nav_term_theme();
        nav_commander_layout_for_config(width, height, theme ? theme->style : NAV_UI_STYLE_MODERN, screen->config, &panes);
        area.x = panes.pane_x[screen->pane]; area.width = panes.pane_width[screen->pane];
        area.status = area.bottom--;
    }
    return area;
}

static size_t viewer_page(const ViewerScreen *screen)
{
    ViewerArea area = viewer_area(screen);
    int rows = area.bottom - area.top;
    return rows > 0 ? (size_t)rows : 1;
}

static size_t line_span(NavViewer *viewer, size_t line, size_t width)
{
    size_t length = viewer->source->line_length(viewer->source, line);
    if (!width)
        return 1;
    return length ? (length + width - 1) / width : 1;
}

/* Re-anchor wrapped navigation only after movement, never as a side effect
 * of redraw, fullscreen toggling or returning to a saved history state. */
static void ensure_wrapped(ViewerScreen *screen)
{
    NavViewer *viewer = &screen->viewer;
    ViewerArea area = viewer_area(screen);
    size_t gutter = nav_viewer_line_number_width(viewer);
    size_t width = area.width > (int)gutter ? (size_t)area.width - gutter : 1;
    size_t page = viewer_page(screen), top = viewer->current_line;
    size_t used = line_span(viewer, top, width);
    while (top) {
        size_t span = line_span(viewer, top - 1, width);
        if (span > page || used > page - span) break;
        used += span; top--;
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

static void viewer_text(int x, int y, int width, const char *text, size_t length, NavStyle style)
{
    nav_ui_text(x, y, width, "", style);
    for (int i = 0; i < width && (size_t)i < length; i++) {
        unsigned char ch = (unsigned char)text[i];
        /* Tabs/control bytes are data, never terminal control sequences. */
        nav_term_unicode_glyph(x + i, y, ch < 32 || ch == 127 ? ' ' : ch, style);
    }
}

static void draw_fragment(NavViewer *viewer, const char *line, size_t length,
                          size_t logical, size_t start, int x, int y, int width,
                          NavStyle style, bool current)
{
    size_t available = start < length ? length - start : 0;
    int amount = available < (size_t)width ? (int)available : width;
    viewer_text(x, y, width, amount ? line + start : "", (size_t)amount, style);
    if (current && viewer->link_length) {
        size_t from = viewer->link_column > start ? viewer->link_column : start;
        size_t to = viewer->link_column + viewer->link_length;
        if (to > start + (size_t)width) to = start + (size_t)width;
        if (to > length) to = length;
        if (from < to) viewer_text(x + (int)(from - start), y, (int)(to - from), line + from, to - from, NAV_STYLE_VIEWER_SEARCH_MATCH);
    }
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
            viewer_text(x + (int)(from - start), y, (int)(to - from), line + from, to - from, NAV_STYLE_VIEWER_SEARCH_MATCH);
        }
    }
}

static void viewer_hints(const ViewerScreen *screen, bool compact, char *output, size_t size)
{
    static const NavCommand commands[] = {NAV_CMD_VIEWER_CLOSE, NAV_CMD_FIND, NAV_CMD_FIND_NEXT, NAV_CMD_FIND_PREVIOUS, NAV_CMD_GOTO, NAV_CMD_WRAP, NAV_CMD_LINES, NAV_CMD_DOWNLOAD, NAV_CMD_VIEWER_FULLSCREEN, NAV_CMD_VIEWER_OPEN_LINK, NAV_CMD_VIEWER_NEXT_LINK, NAV_CMD_VIEWER_PREVIOUS_LINK, NAV_CMD_VIEWER_BACK};
    static const char *const labels[] = {"Close", "Find", "Next", "Prev", "GoTo", "Wrap", "Lines", "Download", "Fullscreen", "Open Link", "Next Link", "Prev Link", "Back"};
    output[0] = 0;
    if (!nav_ui_show_dialog_keys()) return;
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++) {
        if ((compact && i >= 4 && i < 7) || (i == 7 && !screen->origin)) continue;
        char key[80]; if (!nav_ui_hint_key(NAV_CONTEXT_VIEWER, commands[i], i == 2 || i == 3, key, sizeof key)) continue;
        size_t used = strlen(output);
        if (used < size) snprintf(output + used, size - used, "  %s %s", key, labels[i]);
    }
}

static void draw_viewer(void *data)
{
    ViewerScreen *screen = data;
    screen->redraw(screen->redraw_data);
}

static void render_viewer(ViewerScreen *screen)
{
    NavViewer *viewer = &screen->viewer;
    bool active = screen->app->active == screen->pane;
    bool cursor_mode = viewer->source->cursor_line != NULL;
    size_t count = cursor_mode ? 0 : viewer->source->line_count(viewer->source);
    int width = nav_term_width(), height = nav_term_height();
    ViewerArea area = viewer_area(screen);
    char hints[256]; viewer_hints(screen, cursor_mode, hints, sizeof hints);
    if (width < 20 || height < 8)
    {
        nav_ui_text(0, 0, width, "Terminal too small", NAV_STYLE_MENU);
        nav_term_hide_cursor();
        return;
    }
    size_t gutter = nav_viewer_line_number_width(viewer), page = viewer_page(screen);
    if (cursor_mode && viewer->line_numbers) {
        prepare_remote_gutter(viewer, page);
        gutter = nav_viewer_line_number_width(viewer);
    }
    if (gutter >= (size_t)area.width) gutter = area.width > 1 ? (size_t)area.width - 1 : 0;
    int text_width = area.width - (int)gutter;
    if (text_width < 1)
        text_width = 1;
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
        if (area.width < 60) snprintf(metadata, sizeof metadata, "%s %s", screen->provider->scheme,
                                      screen->fullscreen ? "Fullscreen" : "Pane View");
        nav_ui_window_header(area.x, area.top, area.width, 1, 'V', title, metadata, active);
    }
    int row = area.top + 1;
    char status[512];
    if (cursor_mode) {
        NavViewCursor cursor = viewer->top_cursor;
        while (row <= area.bottom) {
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
            for (size_t segment = 0; segment < segments && row <= area.bottom;
                 segment++, row++) {
                NavStyle style = active && screen->highlight_current && current ?
                                 NAV_STYLE_ACCENT : NAV_STYLE_TEXT;
                if (gutter) {
                    char number[32] = {0}, digits[24] = "?";
                    size_t digit_count;
                    memset(number, ' ', gutter < sizeof number - 1 ? gutter :
                           sizeof number - 1);
                    if (segment == 0) {
                        number[0] = active && current ? '>' : ' ';
                        if (cursor.ordinal_known)
                            snprintf(digits, sizeof digits, "%llu",
                                     (unsigned long long)cursor.ordinal + 1);
                        digit_count = strlen(digits);
                        if (digit_count + 1 < gutter)
                            memcpy(number + gutter - 1 - digit_count, digits,
                                   digit_count);
                    }
                    nav_ui_text(area.x, row, (int)gutter, number,
                                active && current ? NAV_STYLE_ACCENT :
                                NAV_STYLE_VIEWER_LINE_NUMBER);
                }
                size_t start = viewer->wrap ? segment * (size_t)text_width :
                               viewer->horizontal_offset;
                draw_fragment(viewer, line, length,
                              cursor.ordinal_known ? (size_t)cursor.ordinal : SIZE_MAX,
                              start, area.x + (int)gutter, row, text_width, style, active && current);
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
                     " Byte %llu%s%llu Col %zu%s%u%%%s%s%s",
                     (unsigned long long)offset, known ? "/" : "",
                     (unsigned long long)(known ? total : 0),
                     viewer->horizontal_offset + 1, known ? " " : "", percent,
                     viewer->status[0] ? "  " : "", viewer->status, hints);
        }
    } else {
        size_t logical = viewer->top_line;
        while (logical < count && row <= area.bottom) {
            size_t length = 0;
            const char *line = viewer->source->line(viewer->source, logical, &length);
            if (!line) line = "";
            size_t segments = viewer->wrap ? line_span(viewer, logical, (size_t)text_width) : 1;
            for (size_t segment = 0; segment < segments && row <= area.bottom; segment++, row++) {
                NavStyle style = active && screen->highlight_current && viewer->current_line == logical ? NAV_STYLE_ACCENT : NAV_STYLE_TEXT;
                if (gutter) {
                    char number[32] = {0}, digits[24]; size_t digit_count;
                    memset(number, ' ', gutter < sizeof number - 1 ? gutter : sizeof number - 1);
                    if (segment == 0) {
                        number[0] = active && logical == viewer->current_line ? '>' : ' ';
                        snprintf(digits, sizeof digits, "%zu", logical + 1);
                        digit_count = strlen(digits);
                        if (digit_count + 1 < gutter) memcpy(number + gutter - 1 - digit_count, digits, digit_count);
                    }
                    nav_ui_text(area.x, row, (int)gutter, number, style == NAV_STYLE_ACCENT ? NAV_STYLE_ACCENT : NAV_STYLE_VIEWER_LINE_NUMBER);
                }
                size_t start = viewer->wrap ? segment * (size_t)text_width : viewer->horizontal_offset;
                draw_fragment(viewer, line, length, logical, start, area.x + (int)gutter, row, text_width, style, active && logical == viewer->current_line);
            }
            logical++;
        }
        size_t shown = count ? viewer->current_line + 1 : 0;
        unsigned percent = count ? (unsigned)(shown * 100 / count) : 0;
        snprintf(status, sizeof status, " Ln %zu/%zu Col %zu %u%%%s%s%s",
                 shown, count, viewer->horizontal_offset + 1, percent,
                 viewer->status[0] ? "  " : "", viewer->status, hints);
    }
    if (area.status >= 0) nav_ui_text(area.x, area.status, area.width, status, NAV_STYLE_STATUS);
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
    nav_viewer_find(&screen->viewer, 1, viewer_page(screen), &wrapped);
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
    nav_viewer_goto_line(&screen->viewer, (size_t)line, viewer_page(screen));
    screen->viewer.match_line = SIZE_MAX;
    screen->viewer.match_length = 0;
    snprintf(screen->viewer.status, sizeof screen->viewer.status, "Line %zu", screen->viewer.current_line + 1);
}

static void viewer_help(void)
{ nav_ui_binding_help(NAV_CONTEXT_VIEWER); }

static NavViewSource *viewer_source(NavApp *app, NavProvider **provider, bool *owned, const NavEntry *entry,
                                    const NavLocation *origin, const NavConfig *config,
                                    NavUiRedrawFn redraw, void *data)
{
    char error[256] = {0}; bool binary = false;
    NavHttpAuthAttempts attempts = {0};
    NavViewSource *source;
retry:
    error[0] = 0; binary = false;
    source = nav_view_source_open_provider(*provider, entry->resource_id, &binary, error, sizeof error);
    if (!source) {
        if (!binary && nav_http_authentication_needed(*provider)) {
            if (nav_ui_http_auth_retry(app, provider, owned, entry->resource_id,
                                      &attempts, error, redraw, data)) goto retry;
            return NULL;  /* Cancel returns directly to the unchanged Viewer. */
        }
        if (binary) {
            if (origin && nav_ui_confirm("This resource does not appear to be text. Download it instead?", redraw, data))
                nav_ui_download(app, *provider, entry, origin, config, redraw, data);
            else if (!origin) { const char *lines[] = {"This resource does not appear to be text."}; nav_ui_info(" Viewer ", lines, 1); }
        } else { const char *lines[] = {error[0] ? error : "Unable to open resource"}; nav_ui_info(" Viewer Error ", lines, 1); }
        return NULL;
    }
    if (source->cursor_line && source->last_error) {
        NavViewCursor first = {0}; size_t ignored = 0;
        source->cursor_top(source, &first);
        if (!source->cursor_line(source, &first, &ignored)) {
            const char *message = source->last_error(source);
            snprintf(error, sizeof error, "%s", message && message[0] ? message : "Unable to read remote line");
            if (nav_http_authentication_needed(*provider)) {
                source->close(source);
                if (nav_ui_http_auth_retry(app, provider, owned, entry->resource_id,
                                           &attempts, error, redraw, data)) goto retry;
                return NULL;
            }
            const char *lines[] = {message && message[0] ? message : "Unable to read remote line"};
            nav_ui_info(" Viewer Error ", lines, 1); source->close(source); return NULL;
        }
    }
    return source;
}

static void viewer_release(NavViewer *viewer, NavProvider *provider, bool owned)
{
    viewer->source->close(viewer->source);
    if (owned) nav_provider_destroy(provider);
}

static void viewer_follow(ViewerScreen *screen, const char *url, bool download)
{
    if (!screen->app) { snprintf(screen->viewer.status, sizeof screen->viewer.status, "Link actions need a Commander origin"); return; }
    NavEntry entry; bool directory, owned;
    char error[256] = {0};
    NavProvider *provider = nav_location_resolve(screen->app, url, &entry, &directory, &owned, error, sizeof error);
    if (!provider) { const char *lines[] = {error}; nav_ui_info(" Open Link ", lines, 1); return; }
    /* Explicit View is resource intent, including URLs ending in '/'. */
    entry.flags &= ~NAV_ENTRY_DIR;
    if (download) nav_ui_download(screen->app, provider, &entry, screen->origin, screen->config, draw_viewer, screen);
    else {
        NavViewSource *source = viewer_source(screen->app, &provider, &owned, &entry, screen->origin, screen->config, draw_viewer, screen);
        if (source) {
            if (screen->history_count == sizeof screen->history / sizeof *screen->history) {
                viewer_release(&screen->history[0].viewer, screen->history[0].provider, screen->history[0].owned);
                memmove(screen->history, screen->history + 1, (--screen->history_count) * sizeof *screen->history);
            }
            size_t slot = screen->history_count++;
            screen->history[slot].viewer = screen->viewer;
            screen->history[slot].entry = screen->resource;
            screen->history[slot].provider = screen->provider;
            screen->history[slot].owned = screen->owned;
            bool wrap = screen->viewer.wrap, numbers = screen->viewer.line_numbers;
            screen->resource = entry; screen->provider = provider; screen->owned = owned;
            nav_viewer_init(&screen->viewer, source);
            screen->viewer.wrap = wrap; screen->viewer.line_numbers = numbers;
            return;
        }
    }
    if (owned) nav_provider_destroy(provider);
}

static void viewer_open_link(ViewerScreen *screen)
{
    char url[NAV_URL_MAX], error[256] = {0};
    if (!nav_viewer_link(&screen->viewer, 0, url, sizeof url)) return;
    static const char *const actions[] = {"View", "Download", "Open in Browser", "Copy URL", "Cancel"};
    int selected = 0;
    if (!nav_ui_select(" Open Link ", actions, 5, &selected, NAV_CONTEXT_PICKER, draw_viewer, screen)) return;
    switch (selected) {
    case 0: viewer_follow(screen, url, false); break;
    case 1: viewer_follow(screen, url, true); break;
    case 2:
        if (nav_open_external_url(url, error, sizeof error)) snprintf(screen->viewer.status, sizeof screen->viewer.status, "%.127s", error);
        else snprintf(screen->viewer.status, sizeof screen->viewer.status, "URL sent to default browser");
        break;
    case 3:
        if (nav_clipboard_set_text(url, error, sizeof error)) snprintf(screen->viewer.status, sizeof screen->viewer.status, "%.127s", error);
        else snprintf(screen->viewer.status, sizeof screen->viewer.status, "URL copied");
        break;
    default: break;
    }
}

static bool viewer_dispatch(ViewerScreen *, NavCommand);
static bool viewer_menu(ViewerScreen *screen)
{
    if (screen->config && !screen->config->menu_remember_position)
    {
        screen->saved_major = 0;
        for (size_t index = 0; index < sizeof viewer_menus / sizeof *viewer_menus; index++)
            screen->menus[index].current = 0;
    }
    NavCommand command = nav_ui_pull_down(screen->menus, sizeof screen->menus / sizeof *screen->menus,
                                  &screen->saved_major, draw_viewer, screen);
    return viewer_dispatch(screen, command);
}

static bool viewer_dispatch(ViewerScreen *screen, NavCommand command)
{
    size_t page = viewer_page(screen);
    bool wrapped = false;
    if ((command >= NAV_CMD_UP && command <= NAV_CMD_RIGHT_FAST) || command == NAV_CMD_GOTO ||
        command == NAV_CMD_FIND || command == NAV_CMD_FIND_NEXT || command == NAV_CMD_FIND_PREVIOUS)
        screen->viewer.link_length = 0;
    switch (command)
    {
    case NAV_CMD_QUIT: nav_ui_request_quit(); return true;
    case NAV_CMD_MENU: return viewer_menu(screen);
    case NAV_CMD_VIEWER_FULLSCREEN: screen->fullscreen = !screen->fullscreen; break;
    case NAV_CMD_VIEWER_OPEN_LINK: viewer_open_link(screen); break;
    case NAV_CMD_VIEWER_NEXT_LINK:
    case NAV_CMD_VIEWER_PREVIOUS_LINK: {
        char url[NAV_URL_MAX];
        if (nav_viewer_link(&screen->viewer, command == NAV_CMD_VIEWER_NEXT_LINK ? 1 : -1, url, sizeof url)) {
            nav_viewer_ensure_visible(&screen->viewer, page);
            ViewerArea area = viewer_area(screen);
            size_t gutter = nav_viewer_line_number_width(&screen->viewer);
            size_t width = area.width > (int)gutter ? (size_t)area.width - gutter : 1;
            if (!screen->viewer.wrap && (screen->viewer.link_column < screen->viewer.horizontal_offset ||
                screen->viewer.link_column >= screen->viewer.horizontal_offset + width))
                screen->viewer.horizontal_offset = screen->viewer.link_column;
            snprintf(screen->viewer.status, sizeof screen->viewer.status, "Link selected");
        }
        break;
    }
    case NAV_CMD_VIEWER_BACK:
        if (screen->history_count) {
            viewer_release(&screen->viewer, screen->provider, screen->owned);
            size_t slot = --screen->history_count;
            screen->viewer = screen->history[slot].viewer; screen->resource = screen->history[slot].entry;
            screen->provider = screen->history[slot].provider; screen->owned = screen->history[slot].owned;
        } else snprintf(screen->viewer.status, sizeof screen->viewer.status, "No Viewer history");
        break;
    case NAV_CMD_DOWNLOAD:
        if (screen->origin) nav_ui_download(screen->app, screen->provider, screen->entry, screen->origin, screen->config, draw_viewer, screen);
        break;
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
    if (screen->viewer.wrap && !screen->viewer.source->cursor_line &&
        ((command >= NAV_CMD_UP && command <= NAV_CMD_RIGHT_FAST) || command == NAV_CMD_GOTO ||
         command == NAV_CMD_FIND || command == NAV_CMD_FIND_NEXT || command == NAV_CMD_FIND_PREVIOUS ||
         command == NAV_CMD_VIEWER_NEXT_LINK || command == NAV_CMD_VIEWER_PREVIOUS_LINK))
        ensure_wrapped(screen);
    return false;
}

int nav_ui_viewer_open(NavApp *app, NavProvider *provider, const NavEntry *entry,
                       bool owned, NavUiRedrawFn redraw, void *data)
{
    NavPane *pane = &app->panes[app->active];
    const NavConfig *config = &app->config;
    NavProvider *request = provider; bool request_owned = false;
    NavViewSource *source = viewer_source(app, &request, &request_owned, entry, &pane->location, config, redraw, data);
    if (!source) { if (request_owned) nav_provider_destroy(request); return 0; }
    ViewerScreen *screen = calloc(1, sizeof *screen);
    if (!screen) {
        source->close(source); if (request_owned) nav_provider_destroy(request);
        const char *lines[] = {"Out of memory creating pane Viewer"};
        nav_ui_info(" Viewer Error ", lines, 1); return 0;
    }
    if (request != provider && owned) nav_provider_destroy(provider);
    screen->provider = request; screen->owned = owned || request_owned;
    screen->resource = *entry; screen->entry = &screen->resource;
    screen->launch_location = pane->location; screen->origin = &screen->launch_location;
    screen->config = config; screen->highlight_current = config->viewer_current_line;
    screen->app = app; screen->redraw = redraw; screen->redraw_data = data; screen->pane = app->active;
    memcpy(screen->menus, viewer_menus, sizeof screen->menus);
    nav_viewer_init(&screen->viewer, source);
    screen->viewer.line_numbers = config->viewer_line_numbers;
    screen->viewer.wrap = config->viewer_wrap;
    nav_ui_viewer_close(pane);
    pane->viewer = screen; pane->content_mode = NAV_PANE_VIEWER;
    return 1;
}

void nav_ui_viewer_close(NavPane *pane)
{
    ViewerScreen *screen = pane->viewer;
    if (screen) {
        viewer_release(&screen->viewer, screen->provider, screen->owned);
        for (size_t i = 0; i < screen->history_count; i++)
            viewer_release(&screen->history[i].viewer, screen->history[i].provider, screen->history[i].owned);
        free(screen);
    }
    pane->viewer = NULL; pane->content_mode = NAV_PANE_FILES;
}

bool nav_ui_viewer_fullscreen(const NavPane *pane)
{ return pane->viewer && pane->viewer->fullscreen; }

NavInputContext nav_ui_active_context(const NavApp *app)
{ return app->panes[app->active].content_mode == NAV_PANE_VIEWER ? NAV_CONTEXT_VIEWER : NAV_CONTEXT_PANEL; }

void nav_ui_viewer_draw(NavApp *app, int index)
{
    ViewerScreen *screen = app->panes[index].viewer;
    if (screen) { screen->pane = index; render_viewer(screen); }
}

void nav_ui_viewer_menu_bar(const NavPane *pane)
{ if (pane->viewer) nav_ui_draw_menu_bar(pane->viewer->menus, 5); }

void nav_ui_viewer_dispatch(NavApp *app, NavCommand command)
{
    NavPane *pane = &app->panes[app->active];
    if (pane->viewer && viewer_dispatch(pane->viewer, command)) nav_ui_viewer_close(pane);
}
