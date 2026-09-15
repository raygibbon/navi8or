#include "nav.h"
#include "nav_version.h"
#include "nav_terminal.h"
#include "nav_theme.h"
#include "nav_profile.h"
#include "nav_ui.h"
#include "nav_ui_core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static void draw_app(void *data);
static void dispatch(NavApp *app, NavCommand command);
static void draw_commander_menu_bar(void);
static void open_menu(NavApp *);
static void navigate_parent(NavApp *, NavPane *, bool);
static void activate(NavApp *, NavPane *);

static bool command_available(const NavApp *app, NavCommand command)
{
    const NavPane *source = &app->panes[app->active];
    const NavPane *destination = &app->panes[!app->active];
    const NavEntry *entry = nav_pane_selected(source);
    switch (command) {
    case NAV_CMD_DOWNLOAD:
    case NAV_CMD_VIEW:
        return entry && !(entry->flags & NAV_ENTRY_DIR) &&
               nav_provider_supports(source->provider, NAV_CAP_READ);
    case NAV_CMD_EDIT:
        return entry && !(entry->flags & NAV_ENTRY_DIR) &&
               nav_provider_supports(source->provider, NAV_CAP_EDIT_LOCAL);
    case NAV_CMD_COPY:
        return entry && !(entry->flags & NAV_ENTRY_DIR) &&
               nav_provider_supports(source->provider, NAV_CAP_READ) &&
               nav_provider_supports(destination->provider, NAV_CAP_WRITE);
    case NAV_CMD_MOVE:
        return entry && !(entry->flags & NAV_ENTRY_PARENT) &&
               nav_provider_supports(source->provider, NAV_CAP_RENAME) &&
               source->provider->rename_path &&
               (!nav_provider_supports(source->provider, NAV_CAP_EDIT_LOCAL) ||
                source->provider == destination->provider);
    case NAV_CMD_DELETE:
        return entry && !(entry->flags & NAV_ENTRY_PARENT) &&
               nav_provider_supports(source->provider, NAV_CAP_DELETE) &&
               source->provider->remove;
    case NAV_CMD_MKDIR:
        return nav_provider_supports(source->provider, NAV_CAP_MKDIR) &&
               source->provider->mkdir && source->provider->location_child;
    default:
        return true;
    }
}

static void set_message(NavApp *app, NavMessageKind kind, const char *message)
{
    app->status_kind = kind;
    snprintf(app->status, sizeof app->status, " %.*s",
             (int)sizeof app->status - 2, message);
}

static void set_status(NavApp *app, const char *message)
{
    set_message(app, NAV_MESSAGE_STATUS, message);
}

static void set_notice(NavApp *app, const char *message)
{
    set_message(app, NAV_MESSAGE_NOTICE, message);
}

static void set_warning(NavApp *app, const char *message)
{
    set_message(app, NAV_MESSAGE_WARNING, message);
}

static void set_error(NavApp *app, const char *message)
{
    set_message(app, NAV_MESSAGE_ERROR, message);
}

static bool entry_visible(const NavPane *pane, const NavEntry *entry) { return pane->filter[0] == 0 || strstr(entry->name, pane->filter) != NULL; }

/* The pane content model retains useful TDX/TDE mechanics, while Navi8or owns
 * the hierarchy, geometry, semantic roles, and modern/classic presentation. */
static void draw_pane_title(const NavPane *pane, int x, int y, int width,
                            char side, bool active, NavUiStyle profile)
{
    char provider[64], marker[8];
    NavStyle style = active ? NAV_STYLE_PANE_TITLE_ACTIVE : NAV_STYLE_PANE_TITLE;
    nav_provider_display_name(pane->provider, provider, sizeof provider);
    nav_ui_text(x, y, width, "", style);
    if (width > 2) nav_ui_text(x + 1, y, width - 2, provider, style);
    if (profile == NAV_UI_STYLE_CLASSIC) {
        snprintf(marker, sizeof marker, "[%c]", side);
        if (width >= 5) nav_ui_text(x + width - 4, y, 3, marker, style);
    }
}

static const char *utf8_tail(const char *text, size_t bytes)
{
    size_t length = strlen(text);
    const unsigned char *tail;
    if (length <= bytes) return text;
    tail = (const unsigned char *)text + length - bytes;
    while (*tail && (*tail & 0xc0u) == 0x80u) tail++;
    return (const char *)tail;
}

static void draw_pane_location(const NavPane *pane, int x, int y, int width)
{
    char line[NAV_PATH_MAX + 4];
    const char *path = pane->location.display_path;
    int available = width > 2 ? width - 2 : width;
    nav_ui_text(x, y, width, "", NAV_STYLE_PATH);
    if (available <= 0) return;
    if ((int)strlen(path) > available && available > 1) {
        const char *prefix = available >= 5 ? "..." : "<";
        int tail_width = available - (int)strlen(prefix);
        const char *tail = utf8_tail(path, (size_t)tail_width);
        snprintf(line, sizeof line, "%s%.*s", prefix, tail_width, tail);
    }
    else snprintf(line, sizeof line, "%s", path);
    nav_ui_text(x + (width > 2 ? 1 : 0), y, available, line, NAV_STYLE_PATH);
}

static void draw_pane_columns(const NavPane *pane, int x, int y, int width, const NavConfig *config)
{
    char row[NAV_PATH_MAX];
    int text_width = width > 2 ? width - 2 : 1;
    if (text_width >= (int)sizeof row) text_width = (int)sizeof row - 1;
    nav_format_panel_header_for_config(pane->view, pane->sort_mode, width, row, sizeof row, config);
    nav_ui_text(x, y, width, "", NAV_STYLE_COLUMN_HEADER);
    nav_ui_text(x + (width > 2 ? 1 : 0), y, text_width, row,
                NAV_STYLE_COLUMN_HEADER);
}

static void draw_pane_body(NavPane *pane, int x, int width, int body_top,
                           int body_height, bool active, const NavConfig *config)
{
    char row[NAV_PATH_MAX];
    const NavSymbols *symbols = nav_symbols_active();
    int visible = 0;
    nav_pane_set_layout(pane, width, body_height);
    for (int row_index = 0; row_index < body_height; row_index++)
        nav_ui_text(x, body_top + row_index, width, "", NAV_STYLE_TEXT);
    for (size_t i = 0; i < pane->listing.count; i++)
    {
        NavEntry *entry = &pane->listing.items[i];
        if (!entry_visible(pane, entry))
            continue;
        if (visible++ < pane->offset)
            continue;
        int relative = visible - 1 - pane->offset;
        int columns = pane->view == NAV_PANEL_BRIEF ? pane->visible_columns : 1;
        int column = pane->view == NAV_PANEL_BRIEF ? relative / pane->rows_per_column : 0;
        int row_number = pane->view == NAV_PANEL_BRIEF ? relative % pane->rows_per_column : relative;
        if (column >= columns || row_number >= body_height)
            continue;
        int base_width = width / columns;
        int item_x = x + column * base_width;
        int column_width = column + 1 == columns ? width - column * base_width : base_width;
        int text_width = column_width - 2;
        if (text_width < 1)
            text_width = 1;
        if (text_width >= (int)sizeof row) text_width = (int)sizeof row - 1;
        char display[NAV_NAME_MAX + 2];
        nav_entry_format_display_name(entry, display, sizeof display);
        if (pane->view == NAV_PANEL_FULL)
            nav_format_entry_full_for_config(entry, column_width, row, sizeof row, config);
        else
            snprintf(row, sizeof row, "%-*.*s", text_width, text_width, display);
        NavStyle row_style = visible - 1 == pane->selected
                                 ? (active ? NAV_STYLE_SELECTION
                                           : NAV_STYLE_SELECTION_INACTIVE)
                                 : entry->flags & NAV_ENTRY_DIR ? NAV_STYLE_DIRECTORY : NAV_STYLE_FILE;
        nav_term_glyph(item_x, body_top + row_number, entry->flags & NAV_ENTRY_PARENT ? symbols->parent : entry->flags & NAV_ENTRY_DIR ? symbols->directory : ' ',
                       row_style);
        nav_ui_text(item_x + 1, body_top + row_number, text_width, row, row_style);
    }
    if (nav_pane_visible_count(pane) == 0)
        nav_ui_text(x + 1, body_top, width - 2, "(no matches)", NAV_STYLE_TEXT);
}

static void draw_pane_summary(const NavPane *pane, int x, int y, int width,
                              bool active, NavUiStyle profile)
{
    char line[512] = {0}, size[32] = {0};
    NavStyle style = profile == NAV_UI_STYLE_CLASSIC ? NAV_STYLE_STATUS
                                                     : NAV_STYLE_TEXT_DIM;
    if (active && profile == NAV_UI_STYLE_CLASSIC) {
        const NavEntry *entry = nav_pane_selected(pane);
        nav_ui_text(x, y, width, "", style);
        if (entry && width > 2) {
            nav_format_entry_full(entry, width, line, sizeof line);
            nav_ui_text(x + 1, y, width - 2, line, style);
        }
        return;
    } else if (active) {
        const NavEntry *entry = nav_pane_selected(pane);
        if (entry) {
            if (entry->flags & NAV_ENTRY_DIR)
                snprintf(size, sizeof size, "<DIR>");
            else if (entry->flags & NAV_ENTRY_SIZE_KNOWN)
                nav_format_entry_size(entry, false, size, sizeof size);
            else
                snprintf(size, sizeof size, "-");
            snprintf(line, sizeof line, " Selected: %.*s  %s",
                     320, entry->name, size);
        }
    } else {
        int files = 0, directories = 0;
        uint64_t bytes = 0;
        bool known = false, approximate = false;
        for (size_t index = 0; index < pane->listing.count; index++) {
            const NavEntry *entry = &pane->listing.items[index];
            if (!entry_visible(pane, entry) || entry->flags & NAV_ENTRY_PARENT) continue;
            if (entry->flags & NAV_ENTRY_DIR) directories++;
            else {
                files++;
                if (entry->flags & NAV_ENTRY_SIZE_KNOWN) {
                    if (bytes > UINT64_MAX - entry->size) { bytes = UINT64_MAX; approximate = true; }
                    else bytes += entry->size;
                    known = true;
                    if (entry->flags & NAV_ENTRY_SIZE_APPROXIMATE) approximate = true;
                } else approximate = true;
            }
        }
        if (known) {
            nav_format_size(bytes, size, sizeof size);
            snprintf(line, sizeof line, " %s%s in %d files, %d dirs", approximate ? "~" : "", size, files, directories);
        } else snprintf(line, sizeof line, " %d files, %d dirs", files, directories);
    }
    if (pane->filter[0]) {
        size_t used = strlen(line);
        snprintf(line + used, sizeof line - used, "%sFilter: %.*s",
                 used ? "  |  " : " ", 120, pane->filter);
    }
    nav_ui_text(x, y, width, line, style);
}

static void draw_global_status(const NavApp *app, const NavCommanderLayout *layout,
                               NavUiStyle profile)
{
    char line[NAV_PATH_MAX];
    NavStyle role = NAV_STYLE_STATUS;
    if (app->status_kind == NAV_MESSAGE_NOTICE) role = NAV_STYLE_MESSAGE;
    else if (app->status_kind == NAV_MESSAGE_WARNING) role = NAV_STYLE_WARNING;
    else if (app->status_kind == NAV_MESSAGE_ERROR) role = NAV_STYLE_ERROR;
    if (profile == NAV_UI_STYLE_CLASSIC) {
        const char *left = nav_path_basename(app->panes[0].location.display_path);
        const char *right = nav_path_basename(app->panes[1].location.display_path);
        if (!left || !*left) left = app->panes[0].location.display_path;
        if (!right || !*right) right = app->panes[1].location.display_path;
        snprintf(line, sizeof line, " L: %.255s | R: %.255s%s%.255s", left,
                 right, app->status[0] ? " |" : "", app->status);
    } else
        snprintf(line, sizeof line, "%s", app->status[0] ? app->status : " Ready");
    nav_ui_text(0, layout->status_row, layout->width, line, role);
}

static bool bar_available(void *data, NavCommand command)
{ return command_available(data, command); }
static void draw_function_key_bar(const NavApp *app, const NavCommanderLayout *layout)
{ nav_ui_command_bar(layout->key_bar_row, layout->width, nav_ui_active_context(app),
                      nav_ui_active_context(app) == NAV_CONTEXT_PANEL ? bar_available : NULL, (void *)app); }

static void draw_app(void *data)
{
    NavApp *app = data;
    int width = nav_term_width(), height = nav_term_height();
    NavCommanderLayout layout;
    const NavTheme *theme = nav_term_theme();
    NavUiStyle profile = theme ? theme->style : NAV_UI_STYLE_MODERN;
    nav_term_clear(NAV_STYLE_BACKGROUND);
    if (!nav_commander_layout_for_config(width, height, profile, &app->config, &layout))
    {
        nav_ui_text(0, 0, width, "Terminal too small", NAV_STYLE_MENU);
        return;
    }
    if (app->config.show_menu) {
        if (nav_ui_active_context(app) == NAV_CONTEXT_VIEWER) nav_ui_viewer_menu_bar(&app->panes[app->active]);
        else draw_commander_menu_bar();
    }
    if (nav_ui_viewer_fullscreen(&app->panes[app->active])) {
        nav_ui_viewer_draw(app, app->active);
        if (app->config.show_function_bar) draw_function_key_bar(app, &layout);
        nav_term_hide_cursor(); return;
    }
    for (int i = 0; i < 2; i++) {
        NavPane *pane = &app->panes[i];
        if (pane->content_mode == NAV_PANE_VIEWER) { nav_ui_viewer_draw(app, i); continue; }
        int x = layout.pane_x[i], w = layout.pane_width[i]; bool active = app->active == i;
        draw_pane_title(pane, x, layout.pane_title_row, w, i ? 'R' : 'L', active, profile);
        draw_pane_location(pane, x, layout.pane_location_row, w);
        draw_pane_columns(pane, x, layout.pane_column_header_row, w, &app->config);
        if (layout.pane_column_separator_row >= 0)
            nav_term_hline(x, layout.pane_column_separator_row, 0xc4, w, active ? NAV_STYLE_BORDER_ACTIVE : NAV_STYLE_BORDER);
        draw_pane_body(pane, x, w, layout.body_top, layout.body_height, active, &app->config);
        draw_pane_summary(pane, x, layout.summary_row, w, active, profile);
    }
    nav_term_vline(layout.divider, layout.pane_title_row, 0xb3,
                   layout.summary_row - layout.pane_title_row + 1,
                   NAV_STYLE_BORDER);
    if (app->config.show_status) {
        if (nav_ui_active_context(app) == NAV_CONTEXT_VIEWER) {
            char key[80], status[128];
            nav_ui_hint_key(NAV_CONTEXT_VIEWER, NAV_CMD_PANEL_SWITCH, false, key, sizeof key);
            snprintf(status, sizeof status, "Viewer  %s Switch Pane", key);
            nav_ui_text(0, layout.status_row, width, status, NAV_STYLE_STATUS);
        } else draw_global_status(app, &layout, profile);
    }
    if (app->config.show_function_bar) draw_function_key_bar(app, &layout);
    nav_term_hide_cursor();
}

static void present_app(NavApp *app)
{
    draw_app(app);
    nav_term_present();
}

static void restore_selection(NavPane *pane, const char *path)
{
    int visible = 0;
    for (size_t i = 0; i < pane->listing.count; i++)
        if (entry_visible(pane, &pane->listing.items[i]))
        {
            if (!strcmp(path, pane->listing.items[i].resource_id))
            {
                pane->selected = visible;
                break;
            }
            visible++;
        }
    nav_pane_clamp_selection(pane);
    nav_pane_ensure_visible(pane);
}

static void refresh_pane(NavApp *app, NavPane *pane)
{
    char selected[NAV_PATH_MAX] = {0}, error[256];
    const NavEntry *entry = nav_pane_selected(pane);
    if (entry)
        snprintf(selected, sizeof selected, "%s", entry->resource_id);
    if (nav_pane_refresh(pane, app->show_hidden, error, sizeof error))
    {
        set_error(app, error);
        return;
    }
    nav_pane_sort(pane, pane->sort_mode);
    if (selected[0])
        restore_selection(pane, selected);
}

static void show_help(void)
{ nav_ui_binding_help(NAV_CONTEXT_PANEL); }

typedef struct
{
    NavApp *app;
    const char *name;
    uint64_t done, total;
    bool total_known;
} ProgressContext;
static void draw_progress(uint64_t done, uint64_t total, bool total_known, void *data)
{
    ProgressContext *c = data;
    int width = nav_term_width(), height = nav_term_height(), box_width = width > 64 ? 64 : width - 2, bar_width = box_width - 8, percent = total_known && total ? (int)(done * 100 / total) : 0, filled = total_known && total ? (int)(done * (uint64_t)bar_width / total) : 0;
    char line[256], bar[128];
    c->done = done;
    c->total = total;
    c->total_known = total_known;
    draw_app(c->app);
    if (width < 20 || height < 8)
    {
        nav_term_present();
        return;
    }
    nav_ui_box((width - box_width) / 2, height / 2 - 3, box_width, 7, " Copying ", NAV_STYLE_DIALOG);
    nav_ui_text((width - box_width) / 2 + 2, height / 2 - 2, box_width - 4, c->name, NAV_STYLE_DIALOG);
    memset(bar, '-', (size_t)bar_width);
    for (int i = 0; i < filled && i < bar_width; i++)
        bar[i] = '=';
    bar[bar_width] = 0;
    if (total_known)
        snprintf(line, sizeof line, "[%s] %d%%", bar, percent);
    else
        snprintf(line, sizeof line, "[%s] ...", bar);
    nav_ui_text((width - box_width) / 2 + 2, height / 2, box_width - 4, line, NAV_STYLE_DIALOG);
    if (total_known)
        snprintf(line, sizeof line, "%llu / %llu bytes", (unsigned long long)done, (unsigned long long)total);
    else
        snprintf(line, sizeof line, "%llu bytes", (unsigned long long)done);
    nav_ui_text((width - box_width) / 2 + 2, height / 2 + 2, box_width - 4, line, NAV_STYLE_DIALOG);
    nav_term_present();
}

static void command_copy(NavApp *app)
{
    NavPane *source = &app->panes[app->active], *destination = &app->panes[!app->active];
    const NavEntry *entry = nav_pane_selected(source);
    char target[NAV_PATH_MAX], suggested[NAV_PATH_MAX], label[NAV_PATH_MAX + 32], error[256];
    NavEntry existing;
    bool overwrite = false;
    if (!entry) {
        set_warning(app, "No selected entry");
        return;
    }
    if (!nav_provider_supports(source->provider, NAV_CAP_READ)) {
        set_warning(app, "Source cannot be read");
        return;
    }
    if (!nav_provider_supports(destination->provider, NAV_CAP_WRITE)) {
        set_warning(app, "Destination is read-only");
        return;
    }
    if (entry->flags & NAV_ENTRY_DIR)
    {
        set_warning(app, "Directory copy is not implemented");
        return;
    }
    NavLocation target_location;
    if (destination->provider->location_child(destination->provider, &destination->location, entry->name, &target_location, error, sizeof error)) { set_error(app, error); return; }
    snprintf(target, sizeof target, "%s", target_location.resource_id);
    snprintf(suggested, sizeof suggested, "%s", target);
    snprintf(label, sizeof label, "To: ");
    if (nav_ui_prompt_text(" Copy ", label, target, sizeof target, draw_app, app))
        return;
    if (strcmp(target, suggested) &&
        (!destination->provider->location ||
         destination->provider->location(destination->provider, target,
                                         &target_location, error,
                                         sizeof error)))
    {
        set_error(app, error[0] ? error : "Invalid destination");
        return;
    }
    if (strcmp(target, suggested))
        snprintf(target, sizeof target, "%s", target_location.resource_id);
    if (nav_provider_supports(destination->provider, NAV_CAP_STAT) && !destination->provider->stat(destination->provider, target, &existing, error, sizeof error))
    {
        char question[NAV_NAME_MAX + 32];
        snprintf(question, sizeof question, "Overwrite \"%.*s\"?", NAV_NAME_MAX - 16, existing.name);
        if (app->config.confirm_overwrite && !nav_ui_confirm(question, draw_app, app))
            return;
        overwrite = true;
    }
    ProgressContext progress = {app, entry->name, 0, entry->size,
                                nav_entry_has_known_size(entry)};
    if (nav_transfer_copy_with_buffer(source->provider, entry->resource_id, destination->provider, target, overwrite, app->config.transfer_buffer_size, draw_progress, &progress, error, sizeof error))
        set_error(app, error);
    else {
        set_notice(app, "Copy complete");
        refresh_pane(app, destination);
    }
}

static void command_move(NavApp *app)
{
    NavPane *source = &app->panes[app->active], *destination = &app->panes[!app->active];
    const NavEntry *entry = nav_pane_selected(source);
    char target[NAV_PATH_MAX], renamed_name[NAV_NAME_MAX], error[256] = {0};
    bool same_provider = source->provider == destination->provider;
    bool local_style = nav_provider_supports(source->provider, NAV_CAP_EDIT_LOCAL);
    bool use_opposite_destination = same_provider && local_style;
    if (!entry) {
        set_warning(app, "No selected entry");
        return;
    }
    if (entry->flags & NAV_ENTRY_PARENT) {
        set_warning(app, "Parent entry cannot be renamed");
        return;
    }
    if (!nav_provider_supports(source->provider, NAV_CAP_RENAME) ||
        !source->provider->rename_path) {
        set_warning(app, "Rename is not supported here");
        return;
    }
    if (!same_provider && local_style) {
        set_warning(app, "Move between providers is not implemented");
        return;
    }
    NavLocation target_location;
    /* Local F6 retains its pane-to-pane move semantics.  Other rename
     * providers rename inside the active pane; their rename operation owns
     * any provider-specific destination validation. */
    if (use_opposite_destination) {
        if (!destination->provider->location_child ||
            destination->provider->location_child(destination->provider,
                                                   &destination->location,
                                                   entry->name,
                                                   &target_location, error,
                                                   sizeof error)) {
            set_error(app, error[0] ? error : "Destination cannot accept this entry");
            return;
        }
        snprintf(target, sizeof target, "%s", target_location.resource_id);
        if (nav_ui_prompt_text(" Move / Rename ", "To: ", target, sizeof target,
                            draw_app, app))
            return;
        if (!nav_leaf_name_copy(renamed_name, nav_path_basename(target))) {
            set_warning(app, "Rename target name is too long");
            return;
        }
        if (!source->provider->location ||
            source->provider->location(source->provider, target,
                                       &target_location, error, sizeof error)) {
            set_error(app, error[0] ? error : "Invalid destination");
            return;
        }
    } else {
        snprintf(target, sizeof target, "%s", entry->name);
        if (nav_ui_prompt_text(" Rename ", "Rename to: ", target, sizeof target,
                            draw_app, app))
            return;
        if (!nav_leaf_name_copy(renamed_name, target)) {
            set_warning(app, "Rename target name is too long");
            return;
        }
        if (!source->provider->location_child ||
            source->provider->location_child(source->provider,
                                              &source->location, target,
                                              &target_location, error,
                                              sizeof error)) {
            set_error(app, error[0] ? error : "Invalid rename destination");
            return;
        }
    }
    snprintf(target, sizeof target, "%s", target_location.resource_id);
    if (source->provider->rename_path(source->provider, entry->resource_id,
                                      target, error, sizeof error))
        set_error(app, error);
    else {
        /* A successful rename can leave the new leaf outside an active
         * filter.  Use the normal pane visibility rule before refreshing, so
         * failure paths leave both filter and selection untouched. */
        NavEntry renamed = *entry;
        if (!nav_leaf_name_copy(renamed.name, renamed_name)) {
            set_warning(app, "Rename target name is too long");
            return;
        }
        if (!entry_visible(source, &renamed))
            source->filter[0] = 0;
        set_notice(app, "Move complete");
        refresh_pane(app, source);
        restore_selection(source, target);
        if (use_opposite_destination && destination != source)
            refresh_pane(app, destination);
    }
}

static void command_mkdir(NavApp *app)
{
    NavPane *pane = &app->panes[app->active];
    char name[NAV_NAME_MAX] = {0}, error[256] = {0};
    if (!nav_provider_supports(pane->provider, NAV_CAP_MKDIR) ||
        !pane->provider->mkdir || !pane->provider->location_child) {
        set_warning(app, "Directory creation is not supported");
        return;
    }
    if (nav_ui_prompt_text(" Create Directory ", "Name: ", name, sizeof name, draw_app, app) || !name[0])
        return;
    NavLocation location;
    if (pane->provider->location_child(pane->provider, &pane->location, name,
                                       &location, error, sizeof error))
    {
        set_error(app, error[0] ? error : "Provider cannot create this directory");
        return;
    }
    if (pane->provider->mkdir(pane->provider, location.resource_id, error, sizeof error))
        set_error(app, error);
    else {
        set_notice(app, "Directory created");
        refresh_pane(app, pane);
    }
}

static void command_delete(NavApp *app)
{
    NavPane *pane = &app->panes[app->active];
    const NavEntry *entry = nav_pane_selected(pane);
    char question[NAV_NAME_MAX + 32], error[256] = {0};
    if (!entry) {
        set_warning(app, "No selected entry");
        return;
    }
    if (entry->flags & NAV_ENTRY_PARENT) {
        set_warning(app, "Parent entry cannot be deleted");
        return;
    }
    if (!nav_provider_supports(pane->provider, NAV_CAP_DELETE) ||
        !pane->provider->remove) {
        set_warning(app, "Deletion is not supported");
        return;
    }
    snprintf(question, sizeof question, "Delete \"%.*s\"?", NAV_NAME_MAX - 16, entry->name);
    if (app->config.confirm_delete && !nav_ui_confirm(question, draw_app, app))
        return;
    if (pane->provider->remove(pane->provider, entry->resource_id, error, sizeof error))
        set_error(app, error);
    else {
        set_notice(app, "Deleted");
        refresh_pane(app, pane);
    }
}

static void command_filter(NavApp *app)
{
    NavPane *pane = &app->panes[app->active];
    char original[NAV_NAME_MAX];
    snprintf(original, sizeof original, "%s", pane->filter);
    if (nav_ui_prompt_text(" Filter ", "Filter: ", pane->filter, sizeof pane->filter, draw_app, app))
    {
        snprintf(pane->filter, sizeof pane->filter, "%s", original);
    }
    nav_pane_clamp_selection(pane);
    nav_pane_ensure_visible(pane);
}

static void command_vault(NavApp *app);

static void command_open_location(NavApp *app)
{
    NavPane *pane = &app->panes[app->active];
    char location[NAV_PATH_MAX], error[256] = {0};
    snprintf(location, sizeof location, "%s", !strcmp(pane->provider->scheme, "local") ? pane->location.display_path : pane->location.resource_id);
    int action = nav_ui_location_prompt(location, sizeof location, draw_app, app);
    if (action < 0 || !location[0]) return;
    NavEntry resource; bool directory = true, owned = false;
    NavProvider *provider = nav_location_resolve(app, location, &resource, &directory,
                                                &owned, error, sizeof error);
    if (!provider) { set_error(app, error); return; }
    nav_http_provider_configure(provider, &app->config);
    if (owned && !strcmp(provider->scheme, "http") && !directory) {
        NavEntry metadata;
        int probe = provider->stat(provider, resource.resource_id, &metadata, error, sizeof error);
        if (probe && app->credential_store && nav_credential_store_is_locked(app->credential_store)) {
            if (nav_ui_confirm("Open Vault to unlock HTTP credentials?", draw_app, app)) {
                command_vault(app);
                probe = provider->stat(provider, resource.resource_id, &metadata, error, sizeof error);
            }
        }
        if (probe && (strstr(error, "401") || strstr(error, "403") || strstr(error, "locked"))) {
            set_error(app, error); nav_provider_destroy(provider); return;
        }
        if (!probe) {
            resource.size = metadata.size; resource.modified = metadata.modified;
            resource.flags = metadata.flags; directory = (metadata.flags & NAV_ENTRY_DIR) != 0;
        }
    }
    if (owned && resource.resource_id[0] && directory)
        snprintf(location, sizeof location, "%s", resource.resource_id);
    NavLocation origin = pane->location;
    if (action == 1 || !directory) {
        if (!resource.resource_id[0]) {
            NavLocation resolved;
            if (provider->location(provider, location, &resolved, error, sizeof error) ||
                provider->stat(provider, resolved.resource_id, &resource, error, sizeof error)) {
                set_error(app, error); if (owned) nav_provider_destroy(provider); return;
            }
        }
        if (action == 1) nav_ui_download(provider, &resource, &origin, &app->config, draw_app, app);
        else if (nav_ui_viewer_open(app, provider, &resource, owned, draw_app, app)) owned = false;
        if (owned) nav_provider_destroy(provider);
        if (action == 1) refresh_pane(app, pane);
        return;
    }
    if (pane->provider != provider) {
        NavPane replacement = *pane;
        NavProvider *old_provider = pane->provider;
        memset(&replacement.location, 0, sizeof replacement.location);
        memset(&replacement.listing, 0, sizeof replacement.listing);
        memset(&replacement.history, 0, sizeof replacement.history);
        replacement.history.current = -1;
        replacement.provider = provider;
        if (nav_pane_open(&replacement, location, app->show_hidden,
                          app->config.history_enabled, error, sizeof error)) {
            nav_listing_free(&replacement.listing);
            set_error(app, error[0] ? error : "Unable to open location");
            if (owned) nav_provider_destroy(provider);
            return;
        }
        nav_pane_sort(&replacement, replacement.sort_mode);
        nav_listing_free(&pane->listing);
        *pane = replacement;
        nav_provider_destroy(old_provider);
        set_notice(app, "Location opened");
    } else if (nav_pane_open(pane, location, app->show_hidden,
                             app->config.history_enabled, error, sizeof error))
        set_error(app, error[0] ? error : "Unable to open location");
    else
        nav_pane_sort(pane, pane->sort_mode);
}

static int choose_repository(NavApp *app, const char *title)
{
    NavUiMenuItem items[NAV_REPOSITORY_MAX];
    NavUiMenu menu;
    int saved = 0, selected;
    if (!app->config.repository_count) {
        set_warning(app, "No repositories are configured");
        return -1;
    }
    memset(items, 0, sizeof items);
    for (size_t index = 0; index < app->config.repository_count; index++) {
        items[index].line = app->config.repositories[index].name;
        items[index].command = (int)index;
        items[index].accelerator = index < 9 ? (char)('1' + index) : 0;
    }
    menu.label = title;
    menu.minor = items;
    menu.minor_count = app->config.repository_count;
    menu.current = 0;
    app->previous_mode = app->mode;
    app->mode = NAV_MODE_MENU;
    selected = nav_ui_pull_down(&menu, 1, &saved, draw_app, app);
    app->mode = app->previous_mode;
    return selected >= 0 && (size_t)selected < app->config.repository_count
               ? selected : -1;
}

static bool repository_name_available(const NavConfig *config,
                                      const char *name, int except)
{
    for (size_t index = 0; index < config->repository_count; index++)
        if ((int)index != except && !strcasecmp(config->repositories[index].name,
                                                name))
            return false;
    return true;
}

static bool parse_repository_toggle(NavApp *app, const char *label,
                                    const char *value, bool *output)
{
    if (!strcasecmp(value, "on") || !strcasecmp(value, "true") ||
        !strcasecmp(value, "yes")) {
        *output = true;
        return true;
    }
    if (!strcasecmp(value, "off") || !strcasecmp(value, "false") ||
        !strcasecmp(value, "no")) {
        *output = false;
        return true;
    }
    char error[96];
    snprintf(error, sizeof error, "%s must be on or off", label);
    set_warning(app, error);
    return false;
}

static void repository_pick_credential(NavApp *, NavRepository *);

static bool prompt_repository(NavApp *app, NavRepository *repository, int except)
{
    char normalized[NAV_URL_MAX], error[256] = {0};
    char verify[8], writable[8], mkdir_value[8], delete_value[8], rename_value[8];
    if (nav_ui_prompt_text(" Repository ", "Name: ", repository->name,
                        sizeof repository->name, draw_app, app) ||
        !repository->name[0])
        return false;
    if (!repository_name_available(&app->config, repository->name, except)) {
        set_warning(app, "Repository names must be unique");
        return false;
    }
    if (nav_ui_prompt_text(" Repository ", "URL: ", repository->url,
                        sizeof repository->url, draw_app, app) ||
        nav_repository_normalize_url(repository->url, normalized,
                                     sizeof normalized, error, sizeof error)) {
        if (error[0]) set_error(app, error);
        return false;
    }
    snprintf(repository->url, sizeof repository->url, "%s", normalized);
    repository_pick_credential(app, repository);
    snprintf(verify, sizeof verify, "%s", repository->tls_verify ? "on" : "off");
    if (nav_ui_prompt_text(" Repository ", "Verify TLS (on/off): ", verify,
                        sizeof verify, draw_app, app))
        return false;
    if (!parse_repository_toggle(app, "Verify TLS", verify,
                                 &repository->tls_verify))
        return false;
    snprintf(writable, sizeof writable, "%s", repository->writable ? "on" : "off");
    if (nav_ui_prompt_text(" Repository ", "Writable PUT (on/off): ", writable,
                        sizeof writable, draw_app, app))
        return false;
    if (!parse_repository_toggle(app, "Writable", writable,
                                 &repository->writable))
        return false;
    snprintf(mkdir_value, sizeof mkdir_value, "%s",
             repository->mkdir_enabled ? "on" : "off");
    if (nav_ui_prompt_text(" Repository ", "MkDir (on/off): ", mkdir_value,
                        sizeof mkdir_value, draw_app, app) ||
        !parse_repository_toggle(app, "MkDir", mkdir_value,
                                 &repository->mkdir_enabled))
        return false;
    snprintf(delete_value, sizeof delete_value, "%s",
             repository->delete_enabled ? "on" : "off");
    if (nav_ui_prompt_text(" Repository ", "Delete (on/off): ", delete_value,
                        sizeof delete_value, draw_app, app) ||
        !parse_repository_toggle(app, "Delete", delete_value,
                                 &repository->delete_enabled))
        return false;
    snprintf(rename_value, sizeof rename_value, "%s",
             repository->rename_enabled ? "on" : "off");
    if (nav_ui_prompt_text(" Repository ", "Rename (on/off): ", rename_value,
                        sizeof rename_value, draw_app, app) ||
        !parse_repository_toggle(app, "Rename", rename_value,
                                 &repository->rename_enabled))
        return false;
    if (!repository->tls_verify &&
        !nav_ui_confirm("TLS verification disabled; save insecure repository?",
                     draw_app, app))
        return false;
    return true;
}

static void command_repository_add(NavApp *app)
{
    NavRepository repository = {.tls_verify = true};
    char error[256] = {0};
    if (app->config.repository_count == NAV_REPOSITORY_MAX) {
        set_warning(app, "Repository limit reached");
        return;
    }
    if (!prompt_repository(app, &repository, -1)) return;
    app->config.repositories[app->config.repository_count++] = repository;
    if (nav_config_save_repositories(&app->config, error, sizeof error)) {
        app->config.repository_count--;
        set_error(app, error);
    } else set_notice(app, "Repository added");
}

static void command_repository_edit(NavApp *app)
{
    int index = choose_repository(app, "Edit Repository");
    NavRepository original, edited;
    char error[256] = {0};
    if (index < 0) return;
    original = edited = app->config.repositories[index];
    if (!prompt_repository(app, &edited, index)) return;
    app->config.repositories[index] = edited;
    if (nav_config_save_repositories(&app->config, error, sizeof error)) {
        app->config.repositories[index] = original;
        set_error(app, error);
    } else set_notice(app, "Repository updated; open panes keep their current settings");
}

static void command_repository_remove(NavApp *app)
{
    int index = choose_repository(app, "Remove Repository");
    NavRepository removed;
    char question[NAV_REPO_NAME_MAX + 40], error[256] = {0};
    if (index < 0) return;
    snprintf(question, sizeof question, "Remove repository \"%s\"?",
             app->config.repositories[index].name);
    if (!nav_ui_confirm(question, draw_app, app)) return;
    removed = app->config.repositories[index];
    memmove(&app->config.repositories[index],
            &app->config.repositories[index + 1],
            (app->config.repository_count - (size_t)index - 1) *
            sizeof app->config.repositories[0]);
    app->config.repository_count--;
    if (nav_config_save_repositories(&app->config, error, sizeof error)) {
        memmove(&app->config.repositories[index + 1],
                &app->config.repositories[index],
                (app->config.repository_count - (size_t)index) *
                sizeof app->config.repositories[0]);
        app->config.repositories[index] = removed;
        app->config.repository_count++;
        set_error(app, error);
    } else set_notice(app, "Repository removed; open panes remain available");
}

typedef struct {
    NavApp *app;
    int selected;
    bool exists;
    bool inline_repository;
} VaultScreen;

static void draw_vault(void *data)
{
    VaultScreen *screen = data;
    NavCredentialStore *store = screen->app->credential_store;
    if (screen->inline_repository) { draw_app(screen->app); return; }
    int width = nav_term_width(), height = nav_term_height();
    nav_term_clear(NAV_STYLE_BACKGROUND);
    if (screen->app->config.show_menu) draw_commander_menu_bar();
    if (width < 20 || height < 6) {
        nav_ui_text(0, 1, width, "Vault", NAV_STYLE_DIALOG_TITLE);
        nav_ui_text(0, 2, width, "Terminal too small", NAV_STYLE_WARNING);
        return;
    }
    nav_ui_box(1, 1, width - 2, height - 2, " Credential Vault ", NAV_STYLE_DIALOG);
    if (!screen->exists) {
        nav_ui_text(3, 3, width - 6, "No vault yet", NAV_STYLE_TEXT_DIM);
        const NavCommand commands[] = {NAV_CMD_VAULT_NEW, NAV_CMD_CANCEL}; const char *labels[] = {"Create Vault", "Close"};
        char hints[256]; nav_ui_hints(NAV_CONTEXT_VAULT, commands, labels, 2, hints, sizeof hints);
        nav_ui_text(3, 5, width - 6, hints, NAV_STYLE_DIALOG);
    } else if (nav_credential_store_is_locked(store)) {
        nav_ui_text(3, 3, width - 6, "Vault locked", NAV_STYLE_WARNING);
        const NavCommand commands[] = {NAV_CMD_VAULT_UNLOCK, NAV_CMD_CANCEL}; const char *labels[] = {"Unlock", "Close"};
        char hints[256]; nav_ui_hints(NAV_CONTEXT_VAULT, commands, labels, 2, hints, sizeof hints);
        nav_ui_text(3, 5, width - 6, hints, NAV_STYLE_DIALOG);
    } else {
        size_t count = nav_credential_store_count(store);
        nav_ui_text(3, 3, width - 6, "Name                         Type     Username", NAV_STYLE_TEXT_DIM);
        if (!count)
            nav_ui_text(3, 5, width - 6, "No credentials", NAV_STYLE_TEXT_DIM);
        for (size_t i = 0; i < count && 5 + (int)i < height - 3; i++) {
            const NavCredential *item = nav_credential_store_get(store, i);
            char line[512];
            snprintf(line, sizeof line, "%-28.28s %-8s %s", item->name,
                     item->type == NAV_CREDENTIAL_BASIC ? "Basic" : "Bearer",
                     item->type == NAV_CREDENTIAL_BASIC ? item->username : "-");
            nav_ui_text(3, 5 + (int)i, width - 6, line,
                        (int)i == screen->selected ? NAV_STYLE_SELECTION : NAV_STYLE_DIALOG);
        }
        const NavCommand commands[] = {NAV_CMD_VAULT_EDIT, NAV_CMD_VAULT_NEW, NAV_CMD_VAULT_DELETE, NAV_CMD_VAULT_LOCK, NAV_CMD_CANCEL};
        const char *labels[] = {"Edit", "New", "Delete", "Lock", "Close"};
        char hints[256]; nav_ui_hints(NAV_CONTEXT_VAULT, commands, labels, 5, hints, sizeof hints);
        nav_ui_text(3, height - 3, width - 6, hints, NAV_STYLE_DIALOG);
    }
    if (screen->app->status[0])
        nav_ui_text(3, height - 4, width - 6, screen->app->status,
                    screen->app->status_kind == NAV_MESSAGE_ERROR ? NAV_STYLE_ERROR :
                    screen->app->status_kind == NAV_MESSAGE_WARNING ? NAV_STYLE_WARNING :
                    NAV_STYLE_TEXT_DIM);
    nav_term_hide_cursor();
}

static void wipe_text(char *text, size_t size)
{ nav_credential_secret_wipe(text, size); }

static void vault_create_file(VaultScreen *screen, const char *path)
{
    char password[1024] = {0}, confirmation[1024] = {0}, error[256];
    if (nav_ui_prompt_secret(" Create Vault ", "New master password: ", password,
                             sizeof password, draw_vault, screen) ||
        nav_ui_prompt_secret(" Create Vault ", "Confirm master password: ", confirmation,
                             sizeof confirmation, draw_vault, screen)) goto done;
    if (strcmp(password, confirmation)) set_warning(screen->app, "Master passwords do not match");
    else if (nav_credential_store_create_vault(&screen->app->credential_store,
                                                path, password, error,
                                                sizeof error))
        set_error(screen->app, error);
    else { screen->exists = true; set_notice(screen->app, "Vault created and unlocked"); }
done:
    wipe_text(password, sizeof password); wipe_text(confirmation, sizeof confirmation);
}

static void vault_unlock(VaultScreen *screen)
{
    char password[1024] = {0}, error[256];
    if (!nav_ui_prompt_secret(" Unlock Vault ", "Master password: ", password,
                              sizeof password, draw_vault, screen)) {
        if (nav_credential_store_unlock(screen->app->credential_store, password,
                                        error, sizeof error)) set_error(screen->app, error);
        else set_notice(screen->app, "Vault unlocked");
    }
    wipe_text(password, sizeof password);
}

static bool vault_prompt_secret(VaultScreen *screen, const char *label,
                                const char *confirmation_label, char *secret,
                                size_t capacity, bool optional)
{
    char confirmation[4096] = {0}; bool ok = false;
    if (nav_ui_prompt_secret(" Credential ", label, secret, capacity,
                             draw_vault, screen)) goto done;
    if (optional && !secret[0]) { ok = true; goto done; }
    if (!secret[0]) {
        set_warning(screen->app, strstr(confirmation_label, "token") ?
                    "Token must not be empty" : "Password must not be empty"); goto done;
    }
    if (nav_ui_prompt_secret(" Credential ", confirmation_label, confirmation,
                             sizeof confirmation, draw_vault, screen)) goto done;
    if (strcmp(secret, confirmation))
        set_warning(screen->app, strstr(confirmation_label, "token") ?
                    "Tokens do not match" : "Passwords do not match");
    else ok = true;
done:
    wipe_text(confirmation, sizeof confirmation); return ok;
}

/* A compact scrolling list built from the shared semantic controls. */
static int credential_choose(NavApp *app, const char *title,
                             const char *const *labels, size_t count, size_t selected)
{
    for (;;) {
        NavAction event;
        int width = nav_term_width(), height = nav_term_height();
        int rows = height - 7;
        if (rows < 1) rows = 1;
        size_t first = selected >= (size_t)rows ? selected - (size_t)rows + 1 : 0;
        draw_app(app);
        nav_ui_box(0, 1, width, height - 2, title, NAV_STYLE_DIALOG);
        for (size_t i = first; i < count && i - first < (size_t)rows; i++)
            nav_ui_text(2, 2 + (int)(i - first), width - 4, labels[i],
                        i == selected ? NAV_STYLE_SELECTION : NAV_STYLE_DIALOG);
        nav_ui_text(2, height - 4, width - 4, app->status,
                    app->status_kind == NAV_MESSAGE_ERROR ? NAV_STYLE_ERROR : NAV_STYLE_TEXT_DIM);
        const NavCommand commands[] = {NAV_CMD_UP, NAV_CMD_DOWN, NAV_CMD_ACCEPT, NAV_CMD_CANCEL};
        const char *labels[] = {"Up", "Down", "Select", "Back"}; char hints[256];
        nav_ui_hints(NAV_CONTEXT_PICKER, commands, labels, 4, hints, sizeof hints);
        nav_ui_text(2, height - 3, width - 4, hints, NAV_STYLE_TEXT_DIM);
        nav_term_hide_cursor(); nav_term_present();
        if (nav_ui_input(NAV_CONTEXT_PICKER, &event) <= 0) continue;
        if (event.type != NAV_TERM_EVENT_KEY) continue;
        if (event.command == NAV_CMD_CANCEL) return -1;
        if (event.command == NAV_CMD_UP) selected = selected ? selected - 1 : count - 1;
        else if (event.command == NAV_CMD_DOWN) selected = (selected + 1) % count;
        else if (event.command == NAV_CMD_ACCEPT) return (int)selected;
    }
}

static bool vault_new(VaultScreen *screen, const char *url, char *created)
{
    NavCredential item = {0}; char secret[4096] = {0}, error[256];
    const char *labels[2]; NavCredentialType types[2]; size_t count = 0;
    bool ok = false;
    if (!url || nav_provider_supports_credential_type(url, NAV_CREDENTIAL_BASIC)) {
        labels[count] = "Basic"; types[count++] = NAV_CREDENTIAL_BASIC;
    }
    if (!url || nav_provider_supports_credential_type(url, NAV_CREDENTIAL_BEARER)) {
        labels[count] = "Bearer"; types[count++] = NAV_CREDENTIAL_BEARER;
    }
    if (!count) { set_warning(screen->app, "Provider supports no available credential types"); return false; }
    for (;;) {
        int choice = credential_choose(screen->app, " Credential type ", labels, count, 0);
        if (choice < 0) break;
        item.type = types[choice];
        if (item.type == NAV_CREDENTIAL_BEARER) item.username[0] = 0;
        if (nav_ui_prompt_text(" Credential ", "Name: ", item.name, sizeof item.name,
                               draw_vault, screen) || !item.name[0]) continue;
        if (item.type == NAV_CREDENTIAL_BASIC &&
            nav_ui_prompt_text(" Credential ", "Username: ", item.username,
                               sizeof item.username, draw_vault, screen)) continue;
        if (!vault_prompt_secret(screen,
                item.type == NAV_CREDENTIAL_BASIC ? "Password: " : "Token: ",
                item.type == NAV_CREDENTIAL_BASIC ? "Confirm password: " : "Confirm token: ",
                secret, sizeof secret, false)) { wipe_text(secret, sizeof secret); continue; }
        if (nav_credential_store_put(screen->app->credential_store, &item, secret,
                                     false, error, sizeof error)) {
            set_error(screen->app, error); wipe_text(secret, sizeof secret); continue;
        }
        if (created) snprintf(created, NAV_CREDENTIAL_NAME_MAX, "%s", item.name);
        set_notice(screen->app, "Credential created"); ok = true; break;
    }
    wipe_text(secret, sizeof secret);
    return ok;
}

static void repository_pick_credential(NavApp *app, NavRepository *repository)
{
    char directory[NAV_PATH_MAX], path[NAV_PATH_MAX], error[256];
    VaultScreen screen = {.app = app, .inline_repository = true};
    if (nav_platform_config_dir(directory, sizeof directory) ||
        snprintf(path, sizeof path, "%s/vault.bin", directory) >= (int)sizeof path) {
        set_error(app, "Unable to determine vault path"); return;
    }
    screen.exists = nav_platform_access(path, F_OK) == 0;
    if (screen.exists && !app->credential_store &&
        nav_credential_store_open_vault(&app->credential_store, path, error, sizeof error)) {
        set_error(app, error); return;
    }
    for (;;) {
        const char *labels[NAV_CREDENTIAL_MAX + 4];
        const NavCredential *records[NAV_CREDENTIAL_MAX + 4] = {0};
        char lines[NAV_CREDENTIAL_MAX + 4][512];
        size_t count = 0, current = 0;
        bool locked = nav_credential_store_is_locked(app->credential_store);
        labels[count++] = "<none>";
        if (repository->credential[0]) {
            const NavCredential *found = NULL;
            for (size_t i = 0; i < nav_credential_store_count(app->credential_store); i++) {
                const NavCredential *record = nav_credential_store_get(app->credential_store, i);
                if (!strcmp(record->name, repository->credential)) { found = record; break; }
            }
            const char *state = locked && screen.exists ? "Vault locked" : !found ? "missing" :
                !nav_provider_supports_credential_type(repository->url, found->type) ? "unsupported by provider" : "selected";
            snprintf(lines[count], sizeof lines[count], "%.28s [%s]", repository->credential, state);
            current = count; labels[count] = lines[count]; count++;
        }
        for (size_t i = 0; i < nav_credential_store_count(app->credential_store); i++) {
            const NavCredential *record = nav_credential_store_get(app->credential_store, i);
            if (!nav_provider_supports_credential_type(repository->url, record->type)) continue;
            snprintf(lines[count], sizeof lines[count], "%.28s  %s  %s", record->name,
                     record->type == NAV_CREDENTIAL_BASIC ? "Basic" : "Bearer",
                     record->type == NAV_CREDENTIAL_BASIC ? record->username : "-");
            records[count] = record; labels[count] = lines[count]; count++;
        }
        size_t unlock = count;
        if (screen.exists && locked) labels[count++] = "Unlock Vault...";
        size_t add = count; labels[count++] = "+ Add credential...";
        int choice = credential_choose(app, " Repository Credential ", labels, count, current);
        if (choice < 0 || (current && choice == (int)current)) return;
        if (!choice) { repository->credential[0] = 0; return; }
        if (records[choice]) {
            snprintf(repository->credential, sizeof repository->credential, "%s", records[choice]->name);
            return;
        }
        if ((size_t)choice == unlock && locked && screen.exists) { vault_unlock(&screen); continue; }
        if ((size_t)choice == add) {
            if (!screen.exists) {
                set_notice(app, "Create an encrypted Vault for local credential storage");
                vault_create_file(&screen, path);
                if (!screen.exists) continue;
            }
            if (nav_credential_store_is_locked(app->credential_store)) {
                vault_unlock(&screen);
                if (nav_credential_store_is_locked(app->credential_store)) continue;
            }
            if (vault_new(&screen, repository->url, repository->credential)) return;
        }
    }
}

static void vault_edit(VaultScreen *screen)
{
    const NavCredential *current = nav_credential_store_get(
        screen->app->credential_store, (size_t)screen->selected);
    NavCredential item; char secret[4096] = {0}, error[256];
    if (!current) return;
    item = *current; /* Names are immutable in vault format v1. */
    if (item.type == NAV_CREDENTIAL_BASIC &&
        nav_ui_prompt_text(" Edit Credential ", "Username: ", item.username,
                           sizeof item.username, draw_vault, screen)) goto done;
    if (!vault_prompt_secret(screen,
            item.type == NAV_CREDENTIAL_BASIC ? "New password (blank keeps current): "
                                              : "New token (blank keeps current): ",
            item.type == NAV_CREDENTIAL_BASIC ? "Confirm password: " : "Confirm token: ",
            secret, sizeof secret, true)) goto done;
    if (nav_credential_store_put(screen->app->credential_store, &item,
                                 secret[0] ? secret : NULL, true,
                                 error, sizeof error)) set_error(screen->app, error);
    else set_notice(screen->app, "Credential updated");
done:
    wipe_text(secret, sizeof secret);
}

static void command_vault(NavApp *app)
{
    char directory[NAV_PATH_MAX], path[NAV_PATH_MAX], error[256];
    VaultScreen screen = {.app = app}; bool close = false;
    if (nav_platform_config_dir(directory, sizeof directory) ||
        snprintf(path, sizeof path, "%s/vault.bin", directory) >= (int)sizeof path) {
        set_error(app, "Unable to determine vault path"); return;
    }
    screen.exists = nav_platform_access(path, F_OK) == 0;
    if (screen.exists && !app->credential_store &&
        nav_credential_store_open_vault(&app->credential_store, path, error,
                                        sizeof error)) {
        set_error(app, error); return;
    }
    while (!close) {
        NavAction event; size_t count;
        draw_vault(&screen); nav_term_present();
        if (nav_ui_input(NAV_CONTEXT_VAULT, &event) <= 0 || event.type != NAV_TERM_EVENT_KEY) continue;
        if (event.command == NAV_CMD_CANCEL) close = true;
        else if (!screen.exists && event.command == NAV_CMD_VAULT_NEW) vault_create_file(&screen, path);
        else if (screen.exists && nav_credential_store_is_locked(app->credential_store) &&
                 (event.command == NAV_CMD_ACCEPT || event.command == NAV_CMD_VAULT_UNLOCK)) vault_unlock(&screen);
        else if (screen.exists && !nav_credential_store_is_locked(app->credential_store)) {
            count = nav_credential_store_count(app->credential_store);
            if (event.command == NAV_CMD_UP && screen.selected > 0) screen.selected--;
            else if (event.command == NAV_CMD_DOWN && screen.selected + 1 < (int)count) screen.selected++;
            else if (event.command == NAV_CMD_VAULT_NEW) vault_new(&screen, NULL, NULL);
            else if (event.command == NAV_CMD_VAULT_EDIT) vault_edit(&screen);
            else if (event.command == NAV_CMD_VAULT_DELETE && count) {
                const NavCredential *item = nav_credential_store_get(app->credential_store, (size_t)screen.selected);
                char question[160]; snprintf(question, sizeof question, "Delete credential \"%s\"?", item->name);
                if (nav_ui_confirm(question, draw_vault, &screen)) {
                    if (nav_credential_store_delete(app->credential_store, item->name, error, sizeof error)) set_error(app, error);
                    else { if (screen.selected && screen.selected >= (int)count - 1) screen.selected--; set_notice(app, "Credential deleted"); }
                }
            } else if (event.command == NAV_CMD_VAULT_LOCK) {
                nav_credential_store_lock(app->credential_store); screen.selected = 0; set_notice(app, "Vault locked");
            }
        }
    }
}

typedef struct {
    NavApp *app;
    NavInput input;
    int64_t last_paint, last_poll;
    bool cancelled;
} ListingScreen;
static int64_t listing_milliseconds(void)
{ return (int64_t)nav_platform_milliseconds(); }
static void repository_progress(const NavListProgress *progress, void *data)
{
    ListingScreen *screen = data;
    int64_t now = listing_milliseconds();
    if (screen->last_paint && now - screen->last_paint < 100) return;
    screen->last_paint = now;
    char received[32], total[32], hint[96], text[256];
    nav_format_size(progress->bytes_received, received, sizeof received);
    nav_format_size(progress->total_bytes, total, sizeof total);
    NavCommand cancel = NAV_CMD_CANCEL;
    nav_ui_hints(NAV_CONTEXT_DIALOG, &cancel, NULL, 1, hint, sizeof hint);
    snprintf(text, sizeof text, "Loading repository... %s%s%s   %zu entries%s%s",
             received, progress->total_known ? " / " : "", progress->total_known ? total : "",
             progress->entries, hint[0] ? "   " : "", hint);
    set_status(screen->app, text);
    NavCommanderLayout layout;
    nav_commander_layout_for_config(nav_term_width(), nav_term_height(), nav_term_theme()->style,
                                    &screen->app->config, &layout);
    if (layout.status_row >= 0) draw_global_status(screen->app, &layout, nav_term_theme()->style);
    nav_term_present();
}
static bool repository_cancel(void *data)
{
    ListingScreen *screen = data;
    int64_t now = listing_milliseconds();
    if (screen->last_poll && now - screen->last_poll < 50) return screen->cancelled;
    screen->last_poll = now;
    NavTermEvent event;
    if (nav_term_poll_event(&event, 0) > 0) {
        if (event.type == NAV_TERM_EVENT_RESIZE) present_app(screen->app);
        if (nav_input_resolve(&screen->input, nav_ui_keymap(), NAV_CONTEXT_DIALOG, &event).command == NAV_CMD_CANCEL)
            screen->cancelled = true;
    }
    return screen->cancelled;
}
static void command_repository_open(NavApp *app)
{
    int index = choose_repository(app, "Open Repository");
    NavPane *pane = &app->panes[app->active], replacement;
    NavProvider *provider, *old_provider;
    char error[256] = {0};
    if (index < 0) return;
    provider = nav_provider_create_repository(&app->config.repositories[index],
                                        app->credential_store, error,
                                        sizeof error);
    if (!provider) { set_error(app, error); return; }
    nav_http_provider_configure(provider, &app->config);
    replacement = *pane;
    memset(&replacement.location, 0, sizeof replacement.location);
    memset(&replacement.listing, 0, sizeof replacement.listing);
    memset(&replacement.history, 0, sizeof replacement.history);
    replacement.history.current = -1;
    replacement.provider = provider;
    replacement.filter[0] = 0;
    ListingScreen screen = {.app = app};
    NavListOptions options = {.progress = repository_progress, .cancel = repository_cancel, .userdata = &screen};
    set_status(app, "Loading repository...");
    present_app(app);
    if (nav_pane_open_progress(&replacement, app->config.repositories[index].url,
                      app->show_hidden, app->config.history_enabled,
                      &options, error, sizeof error)) {
        nav_listing_free(&replacement.listing);
        nav_provider_destroy(provider);
        if (screen.cancelled) set_notice(app, "Repository listing cancelled; previous pane retained");
        else set_error(app, error[0] ? error : "Unable to open repository");
        return;
    }
    if (replacement.listing.count > 10000) {
        char text[128]; snprintf(text, sizeof text, "Sorting %zu entries...", replacement.listing.count - 1);
        set_status(app, text); present_app(app);
    }
    nav_pane_sort(&replacement, replacement.sort_mode);
    old_provider = pane->provider;
    nav_listing_free(&pane->listing);
    *pane = replacement;
    nav_provider_destroy(old_provider);
    set_notice(app, "Repository opened");
}

static void command_edit(NavApp *app)
{
    const NavEntry *entry = nav_pane_selected(&app->panes[app->active]);
    char error[256];
    const char *arguments[NAV_EDITOR_ARG_MAX];
    for (size_t index = 0; index < app->config.editor_arg_count; index++)
        arguments[index] = app->config.editor_args[index];
    NavEditorConfig editor = {app->config.editor_command, arguments, app->config.editor_arg_count, app->config.editor_wait};
    if (!entry || entry->flags & NAV_ENTRY_DIR || !nav_provider_supports(app->panes[app->active].provider, NAV_CAP_EDIT_LOCAL))
        return;
    nav_term_shutdown();
    if (nav_platform_launch_editor(&editor, entry->resource_id, error, sizeof error))
        set_error(app, error);
    nav_term_set_theme(&app->config.profile);
    if (nav_term_init() < 0)
    {
        set_error(app, "Unable to resume terminal");
        app->running = false;
    }
}

static void command_open_config(NavApp *app)
{
    char error[256];
    const char *path = app->config.config_path;
    const char *arguments[NAV_EDITOR_ARG_MAX];
    if (!path[0]) { set_error(app, "Configuration path is unavailable"); return; }
    for (size_t index = 0; index < app->config.editor_arg_count; index++)
        arguments[index] = app->config.editor_args[index];
    NavEditorConfig editor = {app->config.editor_command, arguments, app->config.editor_arg_count, app->config.editor_wait};
    nav_term_shutdown();
    if (nav_platform_launch_editor(&editor, path, error, sizeof error))
        set_error(app, error);
    nav_term_set_theme(&app->config.profile);
    if (nav_term_init() < 0)
        app->running = false;
}

static bool profile_ui_initialized;
static NavPanelView profile_view;
static NavSortMode profile_sort;
static bool profile_hidden, profile_dirs_first, profile_case_sensitive;

void nav_ui_profile_apply(NavApp *app)
{
    bool hidden_changed = !profile_ui_initialized || profile_hidden != app->config.show_hidden;
    nav_ui_input_configure(&app->config);
    nav_term_set_theme(&app->config.profile);
    for (int i = 0; i < 2; i++) nav_http_provider_configure(app->panes[i].provider, &app->config);
    if (hidden_changed) app->show_hidden = app->config.show_hidden;
    for (int i = 0; i < 2; i++) {
        bool resort = !profile_ui_initialized || profile_sort != app->config.sort ||
                      profile_dirs_first != app->config.directories_first || profile_case_sensitive != app->config.case_sensitive_sort;
        if (!profile_ui_initialized || profile_view != app->config.panel_view) app->panes[i].view = app->config.panel_view;
        app->panes[i].history_enabled = app->config.history_enabled;
        if (!profile_ui_initialized || profile_dirs_first != app->config.directories_first) app->panes[i].directories_first = app->config.directories_first;
        if (!profile_ui_initialized || profile_case_sensitive != app->config.case_sensitive_sort) app->panes[i].case_sensitive_sort = app->config.case_sensitive_sort;
        if (hidden_changed && profile_ui_initialized) refresh_pane(app, &app->panes[i]);
        if (resort) nav_pane_sort(&app->panes[i], app->config.sort);
        nav_pane_clamp_selection(&app->panes[i]); nav_pane_ensure_visible(&app->panes[i]);
    }
    profile_view = app->config.panel_view; profile_sort = app->config.sort;
    profile_hidden = app->config.show_hidden; profile_dirs_first = app->config.directories_first;
    profile_case_sensitive = app->config.case_sensitive_sort; profile_ui_initialized = true;
}

void nav_ui_profile_restore_panes(NavApp *app, const NavProfileSession *session)
{
    bool hidden_changed = app->show_hidden != session->hidden; app->show_hidden = session->hidden;
    for (int i = 0; i < 2; i++) {
        app->panes[i].view = session->views[i]; app->panes[i].directories_first = session->directories_first[i];
        app->panes[i].case_sensitive_sort = session->case_sensitive[i];
        if (hidden_changed) refresh_pane(app, &app->panes[i]);
        nav_pane_sort(&app->panes[i], session->sorts[i]);
        nav_pane_clamp_selection(&app->panes[i]); nav_pane_ensure_visible(&app->panes[i]);
    }
}

static void command_reload_config(NavApp *app)
{
    if ((app->profile_dirty || nav_settings_dirty(app)) && !nav_ui_confirm(nav_settings_dirty(app) ? "Discard unsaved settings and reload?" : "Discard unsaved profile changes and reload?", draw_app, app)) return;
    NavConfig candidate;
    NavThemeResult theme_result;
    static NavTheme running_theme;
    char error[256] = {0};
    if (nav_config_load_file(&candidate, app->config.explicit_config ? app->config.profile_path : NULL, error, sizeof error))
    {
        set_error(app, error[0] ? error : "Configuration reload failed");
        return;
    }
    theme_result.theme = candidate.profile;
    app->config = candidate;
    nav_profile_mark_saved(app);
    nav_settings_mark_saved(app);
    profile_ui_initialized = false; nav_ui_profile_apply(app);
    nav_ui_input_configure(&app->config);
    app->show_hidden = candidate.show_hidden;
    running_theme = theme_result.theme;
    nav_term_set_theme(&running_theme);
    for (int index = 0; index < 2; index++)
    {
        app->panes[index].directories_first = candidate.directories_first;
        app->panes[index].case_sensitive_sort = candidate.case_sensitive_sort;
        app->panes[index].history_enabled = candidate.history_enabled;
        app->panes[index].view = candidate.panel_view;
        app->panes[index].history.limit = candidate.history_max_entries > NAV_HISTORY_MAX ? NAV_HISTORY_MAX : (int)candidate.history_max_entries;
        refresh_pane(app, &app->panes[index]);
    }
    set_notice(app, "Configuration reloaded");
}

static void command_theme_info(NavApp *app)
{
    char name[128], id[128], style[64], source[512], reason[320];
    NavThemeResult result;
    nav_theme_load_result(app->config.theme_name, &result);
    result.theme = app->config.profile;
    if (app->config.profile_path[0]) { result.fallback = false;
        snprintf(result.path, sizeof result.path, "%s", app->config.profile_path);
    }
    snprintf(name, sizeof name, "Theme: %s", result.theme.display_name);
    snprintf(id, sizeof id, "Identity: %s", app->config.profile_path[0] ?
             nav_profile_is_template(app->config.profile_path) ? "bundled template" : "user profile" : "normal configuration");
    snprintf(style, sizeof style, "Style: %s",
             result.theme.style == NAV_UI_STYLE_CLASSIC ? "Classic" : "Modern");
    snprintf(source, sizeof source, "Source: %.*s", (int)sizeof source - 9, result.path);
    if (result.fallback)
    {
        snprintf(reason, sizeof reason, "Reason: %s", result.error[0] ? result.error : "theme file could not be loaded");
        const char *lines[] = {name, id, style, source, reason};
        nav_ui_info(" Current Theme ", lines, sizeof lines / sizeof *lines);
    }
    else
    {
        const char *lines[] = {name, id, style, source};
        nav_ui_info(" Current Theme ", lines, sizeof lines / sizeof *lines);
    }
}

static void dispatch(NavApp *app, NavCommand command)
{
    NavPane *pane = &app->panes[app->active];
    /* Focus belongs to the application, not to either content controller. */
    if (command == NAV_CMD_PANEL_SWITCH) {
        if (!nav_ui_viewer_fullscreen(pane)) app->active = !app->active;
        return;
    }
    if (pane->content_mode == NAV_PANE_VIEWER && command != NAV_CMD_QUIT &&
        command != NAV_CMD_PREFERENCES && command != NAV_CMD_PROFILE_SAVE &&
        command != NAV_CMD_PROFILE_SAVE_AS && command != NAV_CMD_VAULT) {
        nav_ui_viewer_dispatch(app, command); return;
    }
    switch (command)
    {
    case NAV_CMD_MENU: open_menu(app); break;
    case NAV_CMD_OPEN: activate(app, pane); break;
    case NAV_CMD_PANEL_PARENT: navigate_parent(app, pane, true); break;
    case NAV_CMD_PANEL_SWAP: {
        NavPane temporary = app->panes[0];
        app->panes[0] = app->panes[1]; app->panes[1] = temporary; break;
    }
    case NAV_CMD_HISTORY_BACK:
    case NAV_CMD_HISTORY_FORWARD: {
        const NavLocation *location = command == NAV_CMD_HISTORY_BACK ?
            nav_history_back(&pane->history) : nav_history_forward(&pane->history);
        char error[256];
        if (location && !nav_pane_load(pane, location, app->show_hidden, false, error, sizeof error))
            nav_pane_sort(pane, pane->sort_mode);
        break;
    }
    case NAV_CMD_UP: nav_pane_move(pane, -1); break;
    case NAV_CMD_DOWN: nav_pane_move(pane, 1); break;
    case NAV_CMD_LEFT:
        if (pane->view == NAV_PANEL_BRIEF) nav_pane_move(pane, -pane->rows_per_column);
        break;
    case NAV_CMD_RIGHT:
        if (pane->view == NAV_PANEL_BRIEF) nav_pane_move(pane, pane->rows_per_column);
        break;
    case NAV_CMD_HOME: pane->selected = 0; break;
    case NAV_CMD_END: pane->selected = nav_pane_visible_count(pane) - 1; break;
    case NAV_CMD_PAGE_UP: nav_pane_page(pane, -1); break;
    case NAV_CMD_PAGE_DOWN: nav_pane_page(pane, 1); break;
    case NAV_CMD_RENAME: command_move(app); break;
    case NAV_CMD_NONE:
        break;
    case NAV_CMD_OPEN_LOCATION:
        command_open_location(app);
        break;
    case NAV_CMD_DOWNLOAD: {
        const NavEntry *entry = nav_pane_selected(pane);
        if (entry && !(entry->flags & NAV_ENTRY_DIR)) {
            NavLocation origin = pane->location;
            nav_ui_download(pane->provider, entry, &origin, &app->config, draw_app, app);
            refresh_pane(app, pane);
        }
        break;
    }
    case NAV_CMD_VIEW:
    {
        const NavEntry *entry = nav_pane_selected(pane);
        if (entry && !(entry->flags & NAV_ENTRY_DIR) && nav_provider_supports(pane->provider, NAV_CAP_READ))
        {
            nav_ui_viewer_open(app, pane->provider, entry, false, draw_app, app);
        }
        break;
    }
    case NAV_CMD_PROPERTIES:
    {
        const NavEntry *entry = nav_pane_selected(pane);
        if (entry)
            nav_show_properties(entry, (entry->flags & NAV_ENTRY_DIR) ? "Type:      Directory" : "Type:      File");
        break;
    }
    case NAV_CMD_EDIT:
        command_edit(app);
        break;
    case NAV_CMD_COPY:
        command_copy(app);
        break;
    case NAV_CMD_MOVE:
        command_move(app);
        break;
    case NAV_CMD_DELETE:
        command_delete(app);
        break;
    case NAV_CMD_MKDIR:
        command_mkdir(app);
        break;
    case NAV_CMD_PREFERENCES: nav_ui_preferences(app, draw_app, app); break;
    case NAV_CMD_PROFILE_SAVE: nav_ui_profile_save(app, false, draw_app, app); break;
    case NAV_CMD_PROFILE_SAVE_AS: nav_ui_profile_save(app, true, draw_app, app); break;
    case NAV_CMD_QUIT:
        if ((!app->profile_dirty && !nav_settings_dirty(app)) || nav_ui_confirm(nav_settings_dirty(app) ? "Discard unsaved settings and quit?" : "Discard unsaved profile changes and quit?", draw_app, app)) app->running = false;
        break;
    case NAV_CMD_REFRESH:
        refresh_pane(app, pane);
        break;
    case NAV_CMD_HIDDEN:
        app->show_hidden = !app->show_hidden;
        refresh_pane(app, &app->panes[0]);
        refresh_pane(app, &app->panes[1]);
        break;
    case NAV_CMD_SORT_NAME:
        nav_pane_sort(pane, NAV_SORT_NAME);
        break;
    case NAV_CMD_SORT_SIZE:
        nav_pane_sort(pane, NAV_SORT_SIZE);
        break;
    case NAV_CMD_SORT_DATE:
        nav_pane_sort(pane, NAV_SORT_DATE);
        break;
    case NAV_CMD_PANEL_BRIEF:
        pane->view = NAV_PANEL_BRIEF;
        nav_pane_ensure_visible(pane);
        break;
    case NAV_CMD_PANEL_FULL:
        pane->view = NAV_PANEL_FULL;
        nav_pane_ensure_visible(pane);
        break;
    case NAV_CMD_FILTER:
        command_filter(app);
        break;
    case NAV_CMD_HELP:
        show_help();
        break;
    case NAV_CMD_ABOUT:
    {
        const char *lines[] = {NAV_APP_IDENTITY, "Keyboard-first local and remote repository navigator", "TDX/TDE interaction and visual conventions"};
        nav_ui_info(" About Navi8or ", lines, 3);
        break;
    }
    case NAV_CMD_OPEN_CONFIG:
        command_open_config(app);
        break;
    case NAV_CMD_RELOAD_CONFIG:
        command_reload_config(app);
        break;
    case NAV_CMD_THEME_INFO:
        command_theme_info(app);
        break;
    case NAV_CMD_REPOSITORY_OPEN:
        command_repository_open(app);
        break;
    case NAV_CMD_REPOSITORY_ADD:
        command_repository_add(app);
        break;
    case NAV_CMD_REPOSITORY_EDIT:
        command_repository_edit(app);
        break;
    case NAV_CMD_REPOSITORY_REMOVE:
        command_repository_remove(app);
        break;
    case NAV_CMD_VAULT:
        command_vault(app);
        break;
    default:
        break;
    }
}

#define ITEM(label, command, key) {label, command, NULL, false, false, key}
#define DISABLED(label, key) {label, NAV_CMD_NONE, NULL, true, false, key}
#define SEPARATOR {NULL, NAV_CMD_NONE, NULL, true, true, 0}
static const NavUiMenuItem file_items[] = {ITEM("Enter URL / Location...", NAV_CMD_OPEN_LOCATION, 'o'), ITEM("Properties", NAV_CMD_PROPERTIES, 'p'), SEPARATOR, ITEM("Open Configuration", NAV_CMD_OPEN_CONFIG, 'c'), ITEM("Reload Configuration", NAV_CMD_RELOAD_CONFIG, 'r'), ITEM("Current Theme", NAV_CMD_THEME_INFO, 't'), SEPARATOR, ITEM("Quit", NAV_CMD_QUIT, 'q')};
static const NavUiMenuItem view_items[] = {ITEM("Refresh", NAV_CMD_REFRESH, 'r'), ITEM("Show Hidden", NAV_CMD_HIDDEN, 'h'), SEPARATOR, ITEM("Brief", NAV_CMD_PANEL_BRIEF, 'b'), ITEM("Full", NAV_CMD_PANEL_FULL, 'f'), SEPARATOR, ITEM("Sort By Name", NAV_CMD_SORT_NAME, 'n'), ITEM("Sort By Size", NAV_CMD_SORT_SIZE, 's'), ITEM("Sort By Date", NAV_CMD_SORT_DATE, 'd')};
static NavUiMenuItem command_items[] = {ITEM("View", NAV_CMD_VIEW, 'v'), ITEM("Edit", NAV_CMD_EDIT, 'e'), ITEM("Copy", NAV_CMD_COPY, 'c'), ITEM("Move/Rename", NAV_CMD_MOVE, 'm'), ITEM("Make Directory", NAV_CMD_MKDIR, 'a'), ITEM("Delete", NAV_CMD_DELETE, 'd'), SEPARATOR, ITEM("Filter", NAV_CMD_FILTER, 'f')};
static const NavUiMenuItem repository_items[] = {
    ITEM("Open Repository", NAV_CMD_REPOSITORY_OPEN, 'o'),
    ITEM("Add Repository", NAV_CMD_REPOSITORY_ADD, 'a'),
    ITEM("Edit Repository", NAV_CMD_REPOSITORY_EDIT, 'e'),
    ITEM("Remove Repository", NAV_CMD_REPOSITORY_REMOVE, 'r'),
    SEPARATOR,
    ITEM("Credential Vault", NAV_CMD_VAULT, 'v')
};
static const NavUiMenuItem help_items[] = {ITEM("Keys", NAV_CMD_HELP, 'k'), ITEM("About Navi8or", NAV_CMD_ABOUT, 'a')};
static const NavUiMenuItem options_items[] = {ITEM("Preferences", NAV_CMD_PREFERENCES, 'p'), ITEM("Save Profile", NAV_CMD_PROFILE_SAVE, 's'), ITEM("Save Profile As", NAV_CMD_PROFILE_SAVE_AS, 'a')};
static NavUiMenu menus[] = {{"File", file_items, sizeof file_items / sizeof *file_items, 0}, {"View", view_items, sizeof view_items / sizeof *view_items, 0}, {"Command", command_items, sizeof command_items / sizeof *command_items, 0}, {"Repositories", repository_items, sizeof repository_items / sizeof *repository_items, 0}, {"Options", options_items, sizeof options_items / sizeof *options_items, 0}, {"Help", help_items, sizeof help_items / sizeof *help_items, 0}};
static void draw_commander_menu_bar(void)
{
    nav_ui_draw_menu_bar(menus, sizeof menus / sizeof *menus);
}
static int saved_major_menu;
static void open_menu(NavApp *app)
{
    int command;
    /* Menu state follows capabilities, never a provider name. */
    command_items[0].disabled = !command_available(app, NAV_CMD_VIEW);
    command_items[1].disabled = !command_available(app, NAV_CMD_EDIT);
    command_items[2].disabled = !command_available(app, NAV_CMD_COPY);
    command_items[3].disabled = !command_available(app, NAV_CMD_MOVE);
    command_items[4].disabled = !command_available(app, NAV_CMD_MKDIR);
    command_items[5].disabled = !command_available(app, NAV_CMD_DELETE);
    if (!app->config.menu_remember_position)
    {
        saved_major_menu = 0;
        for (size_t index = 0; index < sizeof menus / sizeof *menus; index++)
            menus[index].current = 0;
    }
    app->previous_mode = app->mode;
    app->mode = NAV_MODE_MENU;
    command = nav_ui_pull_down(menus, sizeof menus / sizeof *menus, &saved_major_menu, draw_app, app);
    app->mode = app->previous_mode;
    assert(app->mode == NAV_MODE_COMMANDER);
    if (command >= 0)
        dispatch(app, (NavCommand)command);
}

static void navigate_parent(NavApp *app, NavPane *pane, bool history)
{
    char parent[NAV_PATH_MAX], error[256] = {0};
    NavLocation parent_location;
    (void)parent;
    if (pane->provider->location_parent && pane->provider->location_parent(pane->provider, &pane->location, &parent_location, error, sizeof error) == 0 && nav_pane_load(pane, &parent_location, app->show_hidden, history && app->config.history_enabled, error, sizeof error) == 0)
        nav_pane_sort(pane, pane->sort_mode);
    else if (error[0])
        set_error(app, error);
}
static void activate(NavApp *app, NavPane *pane)
{
    const NavEntry *entry = nav_pane_selected(pane);
    char error[256];
    if (!entry)
        return;
    if (!(entry->flags & NAV_ENTRY_DIR))
    {
        dispatch(app, NAV_CMD_VIEW);
        return;
    }
    if (entry->flags & NAV_ENTRY_PARENT)
    {
        navigate_parent(app, pane, true);
        return;
    }
    NavLocation child;
    if (!pane->provider->location || pane->provider->location(pane->provider, entry->resource_id, &child, error, sizeof error) || nav_pane_load(pane, &child, app->show_hidden, app->config.history_enabled, error, sizeof error))
        set_error(app, error);
    else
        nav_pane_sort(pane, pane->sort_mode);
}

int nav_ui_run(NavApp *app)
{
    nav_settings_mark_saved(app);
    /* Ctrl+\\ is Navi8or's TDX-derived menu key, not a process-quit request. */
    nav_ui_input_configure(&app->config);
    nav_ui_workspace(NAV_CONTEXT_PANEL);
    profile_ui_initialized = false; nav_ui_profile_apply(app);
    nav_platform_console_signals();
    nav_term_set_theme(&app->config.profile);
    if (nav_term_init() < 0)
    {
        fprintf(stderr, "nav: terminal initialization failed\n");
        return 1;
    }
    set_status(app, "Ready");
    app->mode = NAV_MODE_COMMANDER;
    while (app->running)
    {
        if (nav_ui_quit_requested()) {
            if ((!app->profile_dirty && !nav_settings_dirty(app)) || nav_ui_confirm(nav_settings_dirty(app) ? "Discard unsaved settings and quit?" : "Discard unsaved profile changes and quit?", draw_app, app)) break;
            nav_ui_input_configure(&app->config);
        }
        NavAction event;
        NavInputContext context = nav_ui_active_context(app);
        if (nav_ui_workspace_context() != context) nav_ui_workspace(context);
        present_app(app);
        if (nav_ui_input(context, &event) <= 0)
            continue;
        if (event.type == NAV_TERM_EVENT_RESIZE)
        {
            for (int i = 0; i < 2; i++)
            {
                if (app->panes[i].content_mode == NAV_PANE_VIEWER) continue;
                nav_pane_clamp_selection(&app->panes[i]);
                nav_pane_ensure_visible(&app->panes[i]);
            }
            continue;
        }
        if (event.type != NAV_TERM_EVENT_KEY)
            continue;
        dispatch(app, event.command);
        NavPane *pane = &app->panes[app->active];
        if (pane->content_mode == NAV_PANE_FILES) {
            nav_pane_clamp_selection(pane);
            nav_pane_ensure_visible(pane);
        }
    }
    for (int i = 0; i < 2; i++) nav_ui_viewer_close(&app->panes[i]);
    nav_term_shutdown();
    free(app->profile_saved); app->profile_saved = NULL;
    free(app->settings_saved); app->settings_saved = NULL;
    return 0;
}
