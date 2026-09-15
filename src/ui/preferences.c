/* One reusable list editor over the same NavConfig used by TOML loading. */
#include "nav_profile.h"
#include "nav_ui_core.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { HOME, TEMPLATES, COLOURS, LAYOUT, PANES, VIEWER, KEYS, SHORTCUTS, GENERAL, EDITOR, NETWORK } Page;
typedef enum { BOOLEAN, ENUM, TEXT } Kind;
typedef struct { const char *label; Kind kind; size_t offset; const char *const *values; int count; } Setting;
#define BOOL(label, field) {label, BOOLEAN, offsetof(NavConfig, field), NULL, 0}
#define ENUMS(label, field, values) {label, ENUM, offsetof(NavConfig, field), values, (int)(sizeof values / sizeof *values)}
static const char *const chrome[] = {"Modern", "Classic"};
static const char *const frames[] = {"ASCII", "Single", "Double", "Combine", "Combine Reverse", "Block"};
static const char *const separator[] = {"Auto", "Hidden", "Shown"};
static const char *const views[] = {"Brief", "Full"};
static const char *const sorts[] = {"Name", "Size", "Date"};
static const Setting layout[] = {
    BOOL("Show Menu", show_menu), BOOL("Show Status Bar", show_status), BOOL("Show Function Bar", show_function_bar),
    BOOL("Show App Identity", show_app_identity),
    {"Chrome", ENUM, offsetof(NavConfig, profile) + offsetof(NavTheme, style), chrome, 2},
    {"Border Style", ENUM, offsetof(NavConfig, profile) + offsetof(NavTheme, frame_style), frames, 6},
    {"Column Separator", ENUM, offsetof(NavConfig, column_separator), separator, 3},
    {"Frame Space", BOOLEAN, offsetof(NavConfig, profile) + offsetof(NavTheme, frame_space), NULL, 0},
    {"Frame Shadow", BOOLEAN, offsetof(NavConfig, profile) + offsetof(NavTheme, shadow), NULL, 0}};
static const Setting panes[] = {
    BOOL("Show Size", pane_show_size), BOOL("Show Modified", pane_show_modified), BOOL("Directories First", directories_first),
    BOOL("Show Hidden", show_hidden), BOOL("Case Sensitive Sort", case_sensitive_sort),
    ENUMS("View", panel_view, views), ENUMS("Sort", sort, sorts), BOOL("Sizes in Bytes", size_bytes),
    {"Date Format", TEXT, offsetof(NavConfig, date_format), NULL, 0}};
static const Setting viewer[] = {BOOL("Wrap", viewer_wrap), BOOL("Line Numbers", viewer_line_numbers), BOOL("Current Line", viewer_current_line)};
static const Setting shortcuts[] = {BOOL("Function Bar", show_function_bar), BOOL("Menu Keys", show_menu_keys), BOOL("Dialog/Viewer Keys", show_dialog_keys), BOOL("Help Keys", show_help_keys)};
static const Setting general[] = {BOOL("Confirm Delete", confirm_delete), BOOL("Confirm Overwrite", confirm_overwrite), BOOL("Remember Menu Position", menu_remember_position), BOOL("Location History", history_enabled)};
static const Setting external_editor[] = {{"Command", TEXT, offsetof(NavConfig, editor_command), NULL, 128}, BOOL("Wait for Editor", editor_wait)};
#undef BOOL
#undef ENUMS

typedef struct {
    NavApp *app; NavProfileSession session; Page page; size_t selected, top;
    NavInputContext context; NavUiRedrawFn redraw; void *data;
    char message[256]; NavCommand commands[NAV_CMD_COUNT]; size_t command_count;
    size_t category, action; int focus; /* 0 sidebar, 1 settings, 2 actions */
    NavProxyMode proxy;
} Editor;
static const char *const home[] = {"Profile Template", "Profile Name", "Colours", "Layout", "Shortcut Display", "Save Profile As"};
static const char *const categories[] = {"General", "Panels", "Viewer", "Editor", "Appearance/Profile", "Key Bindings", "Network"};
static const Page category_pages[] = {GENERAL, PANES, VIEWER, EDITOR, HOME, KEYS, NETWORK};
static const char *const proxy_options[] = {"System", "No Proxy"};
static const char *const templates[] = {"Classic DOS", "Solar Dark", "Solar Light", "Monochrome"};
static const char *const template_files[] = {"classic-dos", "solar-dark", "solar-light", "monochrome"};

static const Setting *settings(Page page, size_t *count)
{
#define PAGE(id, array) if (page == id) { *count = sizeof array / sizeof *array; return array; }
    PAGE(LAYOUT, layout); PAGE(PANES, panes); PAGE(VIEWER, viewer); PAGE(SHORTCUTS, shortcuts);
    PAGE(GENERAL, general); PAGE(EDITOR, external_editor);
#undef PAGE
    *count = 0; return NULL;
}
static NavInputContext default_context(NavCommand command)
{
    if (command >= NAV_CMD_VIEWER_FULLSCREEN && command <= NAV_CMD_VIEWER_BACK)
        return NAV_CONTEXT_VIEWER;
    NavKeymap map; nav_keymap_defaults(&map);
    for (size_t i = 0; i < map.count; i++) if (map.bindings[i].command == command) return map.bindings[i].context;
    return NAV_CONTEXT_PANEL;
}
static size_t rows(Editor *editor)
{
    if (editor->page == HOME) return sizeof home / sizeof *home;
    if (editor->page == NETWORK) return 1;
    if (editor->page == TEMPLATES) return 4;
    if (editor->page == COLOURS) return NAV_STYLE_COUNT * 2;
    if (editor->page == KEYS) {
        editor->command_count = 0;
        for (NavCommand command = NAV_CMD_NONE + 1; command < NAV_CMD_COUNT; command++) {
            if (command == NAV_CMD_TEXT) continue;
            bool present = default_context(command) == editor->context;
            for (size_t i = 0; i < editor->app->config.keymap.count; i++) {
                const NavBinding *b = &editor->app->config.keymap.bindings[i];
                if (b->context == editor->context && b->command == command) present = true;
            }
            if (present) editor->commands[editor->command_count++] = command;
        }
        return editor->command_count + 1;
    }
    size_t count; settings(editor->page, &count); return count;
}
static void row_text(Editor *editor, size_t index, char *out, size_t size)
{
    NavConfig *config = &editor->app->config;
    if (editor->page == NETWORK) { snprintf(out, size, "Proxy Mode  [ %s v ]", proxy_options[editor->proxy]); return; }
    if (editor->page == HOME) {
        snprintf(out, size, "%s%s%s", home[index], index == 1 ? "  " : "", index == 1 ? config->profile.display_name : ""); return;
    }
    if (editor->page == TEMPLATES) { snprintf(out, size, "%s", templates[index]); return; }
    if (editor->page == COLOURS) {
        int role = (int)(index / 2); bool background = index % 2;
        const char *name = nav_theme_role_name((NavStyle)role);
        snprintf(out, size, "%s %s  %s", name ? name + 3 : "Unknown", background ? "background" : "foreground",
                 nav_theme_colour_name(background ? config->profile.background[role] : config->profile.foreground[role])); return;
    }
    if (editor->page == KEYS) {
        if (!index) { snprintf(out, size, "Context  %s", nav_context_name(editor->context)); return; }
        NavCommand command = editor->commands[index - 1]; char labels[200];
        nav_keymap_labels(&config->keymap, editor->context, command, labels, sizeof labels);
        bool explicit = false;
        for (size_t i = 0; i < config->keymap.count; i++) if (config->keymap.bindings[i].context == editor->context && config->keymap.bindings[i].command == command && config->keymap.bindings[i].configured) explicit = true;
        snprintf(out, size, "%s  %s  [%s]", nav_command_description(command), labels[0] ? labels : "Unbound", labels[0] ? explicit ? "Override" : "Inherited" : "Cleared"); return;
    }
    size_t count; const Setting *setting = settings(editor->page, &count) + index;
    const char *value = ""; char text[96]; unsigned char *field = (unsigned char *)config + setting->offset;
    if (setting->kind == BOOLEAN) value = *(bool *)field ? "[x]" : "[ ]";
    else if (setting->kind == TEXT) value = (char *)field;
    else {
        int n = *(int *)field; if (setting->offset == offsetof(NavConfig, column_separator)) n++;
        snprintf(text, sizeof text, "[ %s v ]", n >= 0 && n < setting->count ? setting->values[n] : "Invalid"); value = text;
    }
    snprintf(out, size, "%s  %s", setting->label, value);
}
static void draw_editor(void *data)
{
    Editor *editor = data;
    if (editor->redraw) editor->redraw(editor->data);
    int width = nav_term_width(), height = nav_term_height();
    if (width < 30 || height < 10) { nav_ui_text(0, 0, width, "Preferences: terminal too small", NAV_STYLE_WARNING); return; }
    int w = width > 94 ? 90 : width - 4, h = height > 25 ? 23 : height - 2;
    int x = (width - w) / 2, y = (height - h) / 2;
    bool dirty = editor->app->profile_dirty || nav_settings_dirty(editor->app) || editor->proxy != editor->app->config.proxy_mode;
    char title[128]; snprintf(title, sizeof title, " Preferences%s - %s ", dirty ? " *" : "", editor->app->config.profile.display_name);
    nav_ui_box(x, y, w, h, title, NAV_STYLE_DIALOG);
    int sidebar = w >= 65 ? 23 : 12, area = x + sidebar + 2, area_width = w - sidebar - 4;
    size_t category_top = editor->category >= (size_t)(h - 7) ? editor->category - (size_t)(h - 7) + 1 : 0;
    for (size_t i = category_top; i < sizeof categories / sizeof *categories && i - category_top < (size_t)(h - 7); i++)
        nav_ui_text(x + 2, y + 2 + (int)(i - category_top), sidebar - 2, categories[i],
                    i == editor->category ? editor->focus == 0 ? NAV_STYLE_SELECTION : NAV_STYLE_SELECTION_INACTIVE : NAV_STYLE_DIALOG);
    nav_ui_vertical_separator(x + sidebar, y + 1, h - 5);
    nav_ui_text(area, y + 1, area_width, categories[editor->category], NAV_STYLE_DIALOG_TITLE);
    size_t count = rows(editor), visible = (size_t)(h - 7);
    if (editor->selected >= count) editor->selected = count ? count - 1 : 0;
    if (editor->selected < editor->top) editor->top = editor->selected;
    if (editor->selected >= editor->top + visible) editor->top = editor->selected - visible + 1;
    for (size_t i = 0; i < visible; i++) {
        char text[320] = ""; size_t index = editor->top + i;
        if (index < count) row_text(editor, index, text, sizeof text);
        NavStyle style = index == editor->selected && editor->focus == 1 ? NAV_STYLE_SELECTION : NAV_STYLE_DIALOG;
        nav_ui_text(area, y + 2 + (int)i, area_width, text, style);
        if (index < count && editor->page == NETWORK)
            nav_ui_select_draw(area, y + 2 + (int)i, area_width, "Proxy Mode", proxy_options[editor->proxy], style);
        else if (index < count) {
            size_t n; const Setting *s = settings(editor->page, &n);
            if (s && s[index].kind == ENUM) {
                int value = *(int *)((unsigned char *)&editor->app->config + s[index].offset);
                if (s[index].offset == offsetof(NavConfig, column_separator)) value++;
                nav_ui_select_draw(area, y + 2 + (int)i, area_width, s[index].label,
                    value >= 0 && value < s[index].count ? s[index].values[value] : "Invalid", style);
            }
        }
    }
    static const char *const actions[] = {"[ Apply ]", "[ Save ]", "[ Cancel ]"};
    int button_width = (w - 4) / 3;
    for (size_t i = 0; i < 3; i++) nav_ui_text(x + 2 + (int)i * button_width, y + h - 4, button_width, actions[i], editor->focus == 2 && editor->action == i ? NAV_STYLE_SELECTION : NAV_STYLE_DIALOG);
    const char *message = editor->message;
    char navigation[256];
    NavCommand motion[] = {NAV_CMD_RIGHT, NAV_CMD_LEFT, NAV_CMD_DOWN};
    const char *motion_labels[] = {"Settings", "Categories", "Actions at end"};
    nav_ui_hints(NAV_CONTEXT_PREFERENCES, motion, motion_labels, 3, navigation, sizeof navigation);
    if (!message[0]) message = editor->page == NETWORK ? "System uses environment proxies; No Proxy connects directly." : navigation;
    nav_ui_text(x + 2, y + h - 3, w - 4, message, NAV_STYLE_MESSAGE);
    static const NavCommand commands[] = {NAV_CMD_ACCEPT, NAV_CMD_CANCEL, NAV_CMD_TEXT_DELETE, NAV_CMD_PROFILE_SAVE, NAV_CMD_PROFILE_APPLY, NAV_CMD_KEY_APPEND, NAV_CMD_KEY_SEQUENCE};
    static const char *const labels[] = {"Change", "Back/Cancel", "Clear", "Save", "Apply", "Add", "Two keys"};
    char hints[320]; nav_ui_hints(NAV_CONTEXT_PREFERENCES, commands, labels, sizeof commands / sizeof *commands, hints, sizeof hints);
    nav_ui_text(x + 2, y + h - 2, w - 4, hints, NAV_STYLE_TEXT_DIM);
    nav_term_hide_cursor();
}
static void live(Editor *editor)
{
    nav_profile_changed(editor->app);
    nav_ui_profile_apply(editor->app);
}
static bool capture(Editor *editor, NavCommand command, unsigned length, char *out, size_t size)
{
    NavBinding binding = {.length = length};
    for (unsigned i = 0; i < length; ) {
        snprintf(editor->message, sizeof editor->message, "Press new key for %s (%u/%u); Escape cancels capture", nav_command_description(command), i + 1, length);
        draw_editor(editor); nav_term_present();
        NavTermEvent event;
        if (nav_term_poll_event(&event, -1) <= 0) continue;
        if (event.type != NAV_TERM_EVENT_KEY) continue;
        if (event.key == NAV_KEY_ESCAPE) { editor->message[0] = 0; return false; }
        if (nav_key_capture_normalize(&event, &binding.keys[i])) {
            snprintf(editor->message, sizeof editor->message, "Key unavailable in the profile syntax"); continue;
        }
        i++;
    }
    nav_binding_format(&binding, out, size); return true;
}
static void edit_key(Editor *editor, bool append, unsigned length, bool clear)
{
    if (!editor->selected) { editor->context = (editor->context + 1) % NAV_CONTEXT_COUNT; editor->top = 0; return; }
    NavCommand command = editor->commands[editor->selected - 1];
    char storage[NAV_BINDING_MAX][80]; const char *keys[NAV_BINDING_MAX]; size_t count = 0;
    if (append) for (size_t i = 0; i < editor->app->config.keymap.count; i++) {
        const NavBinding *b = &editor->app->config.keymap.bindings[i];
        if (b->context != editor->context || b->command != command) continue;
        nav_binding_format(b, storage[count], sizeof storage[count]); keys[count] = storage[count]; count++;
    }
    if (!clear) {
        if (count == NAV_BINDING_MAX || !capture(editor, command, length, storage[count], sizeof storage[count])) return;
        keys[count] = storage[count]; count++;
    }
    int result = nav_keymap_replace(&editor->app->config.keymap, editor->context, command, keys, count, false, editor->message, sizeof editor->message);
    if (result == 1 && nav_ui_confirm(editor->message, draw_editor, editor))
        result = nav_keymap_replace(&editor->app->config.keymap, editor->context, command, keys, count, true, editor->message, sizeof editor->message);
    if (result == 0) { snprintf(editor->message, sizeof editor->message, "%s updated (preview)", nav_command_description(command)); live(editor); }
}

bool nav_ui_profile_save(NavApp *app, bool save_as, NavUiRedrawFn redraw, void *data)
{
    char path[NAV_PATH_MAX], name[64] = "Custom", error[256];
    bool personal = save_as || nav_profile_is_template(app->config.profile_path) || !strcmp(app->config.profile_path, app->config.config_path);
    if (personal) {
        if (nav_ui_prompt_text("Save Profile As", "Profile name: ", name, sizeof name, redraw, data)) return false;
        if (nav_profile_user_path(name, path, sizeof path, error, sizeof error)) goto fail;
        if (nav_platform_access(path, F_OK) == 0 && !nav_ui_confirm("Replace this user profile?", redraw, data)) return false;
    } else snprintf(path, sizeof path, "%s", app->config.profile_path);
    /* Profile identity changes only if the atomic save succeeds. */
    char old_name[64]; snprintf(old_name, sizeof old_name, "%s", app->config.profile.display_name);
    if (personal) snprintf(app->config.profile.display_name, sizeof app->config.profile.display_name, "%s", name);
    if (nav_profile_save(&app->config, path, error, sizeof error)) {
        snprintf(app->config.profile.display_name, sizeof app->config.profile.display_name, "%s", old_name); goto fail;
    }
    nav_profile_mark_saved(app);
    snprintf(app->status, sizeof app->status, "Profile saved: %.200s", path);
    nav_ui_profile_apply(app); return true;
fail:
    { const char *lines[] = {error}; nav_ui_info(" Save Profile ", lines, 1); }
    return false;
}

static bool save_editor(Editor *editor, bool save_as)
{
    NavApp *app = editor->app;
    if ((app->profile_dirty || save_as) && !nav_ui_profile_save(app, save_as, draw_editor, editor)) return false;
    NavProxyMode old = app->config.proxy_mode; app->config.proxy_mode = editor->proxy;
    if (nav_settings_dirty(app)) {
        if (nav_config_save_settings(&app->config, editor->message, sizeof editor->message)) {
            app->config.proxy_mode = old; return false;
        }
        nav_settings_mark_saved(app);
        snprintf(app->status, sizeof app->status, "Preferences saved");
    }
    return true;
}

static bool discard_editor(Editor *editor)
{
    return (nav_profile_same_ui(&editor->app->config, editor->session.before) &&
            nav_settings_same(&editor->app->config, editor->session.before) &&
            editor->proxy == editor->session.before->proxy_mode) ||
        nav_ui_confirm("Discard unsaved Preferences changes?", draw_editor, editor);
}

void nav_ui_preferences(NavApp *app, NavUiRedrawFn redraw, void *data)
{
    Editor editor = {.app = app, .context = NAV_CONTEXT_PANEL, .redraw = redraw, .data = data, .page = GENERAL, .proxy = app->config.proxy_mode};
    if (nav_profile_begin(app, &editor.session)) { const char *lines[] = {"Out of memory"}; nav_ui_info(" Preferences ", lines, 1); return; }
    bool committed = false, done = false;
    while (!done) {
        draw_editor(&editor); nav_term_present(); NavAction event;
        if (nav_ui_input(NAV_CONTEXT_PREFERENCES, &event) <= 0) continue;
        if (event.type != NAV_TERM_EVENT_KEY) continue;
        size_t count = rows(&editor);
        if (event.command == NAV_CMD_TEXT && event.text == ' ') event.command = NAV_CMD_ACCEPT;
        if (editor.focus == 0 && (event.command == NAV_CMD_UP || event.command == NAV_CMD_DOWN || event.command == NAV_CMD_HOME || event.command == NAV_CMD_END)) {
            if (event.command == NAV_CMD_UP && editor.category) editor.category--;
            if (event.command == NAV_CMD_DOWN && editor.category + 1 < sizeof categories / sizeof *categories) editor.category++;
            if (event.command == NAV_CMD_HOME) editor.category = 0;
            if (event.command == NAV_CMD_END) editor.category = sizeof categories / sizeof *categories - 1;
            editor.page = category_pages[editor.category]; editor.selected = editor.top = 0; editor.message[0] = 0; continue;
        }
        if (editor.focus == 0 && (event.command == NAV_CMD_RIGHT || event.command == NAV_CMD_ACCEPT)) { editor.focus = 1; continue; }
        if (editor.focus == 1 && event.command == NAV_CMD_LEFT) {
            if (editor.category == 4 && editor.page != HOME) editor.page = HOME;
            else editor.focus = 0;
            editor.selected = editor.top = 0; continue;
        }
        if (editor.focus == 1 && event.command == NAV_CMD_DOWN && editor.selected + 1 == count) { editor.focus = 2; continue; }
        if (editor.focus == 2) {
            if (event.command == NAV_CMD_LEFT && editor.action) { editor.action--; continue; }
            if (event.command == NAV_CMD_RIGHT && editor.action < 2) { editor.action++; continue; }
            if (event.command == NAV_CMD_UP) { editor.focus = 1; continue; }
            if (event.command == NAV_CMD_ACCEPT) event.command = editor.action == 0 ? NAV_CMD_PROFILE_APPLY : editor.action == 1 ? NAV_CMD_PROFILE_SAVE : NAV_CMD_CANCEL;
        }
        switch (event.command) {
        case NAV_CMD_UP: if (editor.selected) editor.selected--; break;
        case NAV_CMD_DOWN: if (editor.selected + 1 < count) editor.selected++; break;
        case NAV_CMD_HOME: editor.selected = 0; break;
        case NAV_CMD_END: editor.selected = count ? count - 1 : 0; break;
        case NAV_CMD_PAGE_UP: editor.selected = editor.selected > 10 ? editor.selected - 10 : 0; break;
        case NAV_CMD_PAGE_DOWN: editor.selected = editor.selected + 10 < count ? editor.selected + 10 : count - 1; break;
        case NAV_CMD_CANCEL:
            if (editor.focus == 1) {
                if (editor.category == 4 && editor.page != HOME) editor.page = HOME;
                else editor.focus = 0;
                editor.selected = editor.top = 0;
            } else if (discard_editor(&editor)) done = true;
            break;
        case NAV_CMD_HELP: nav_ui_binding_help(NAV_CONTEXT_PREFERENCES); break;
        case NAV_CMD_PROFILE_APPLY: committed = done = true; break;
        case NAV_CMD_PROFILE_SAVE: case NAV_CMD_PROFILE_SAVE_AS:
            if (save_editor(&editor, event.command == NAV_CMD_PROFILE_SAVE_AS)) committed = done = true;
            break;
        case NAV_CMD_TEXT_DELETE: if (editor.focus == 1 && editor.page == KEYS) edit_key(&editor, false, 0, true); break;
        case NAV_CMD_KEY_APPEND: if (editor.focus == 1 && editor.page == KEYS) edit_key(&editor, true, 1, false); break;
        case NAV_CMD_KEY_SEQUENCE: if (editor.focus == 1 && editor.page == KEYS) edit_key(&editor, false, 2, false); break;
        case NAV_CMD_LEFT: case NAV_CMD_RIGHT: case NAV_CMD_ACCEPT: {
            if (editor.focus != 1) break;
            int direction = event.command == NAV_CMD_LEFT ? -1 : 1;
            if (editor.page == HOME) {
                switch (editor.selected) {
                case 0: editor.page = TEMPLATES; break;
                case 1: {
                    char name[64]; snprintf(name, sizeof name, "%s", app->config.profile.display_name);
                    if (!nav_ui_prompt_text("Profile", "Profile name: ", name, sizeof name, draw_editor, &editor) && name[0]) {
                        snprintf(app->config.profile.display_name, sizeof app->config.profile.display_name, "%s", name); live(&editor);
                    } break;
                }
                case 2: editor.page = COLOURS; break; case 3: editor.page = LAYOUT; break;
                case 4: editor.page = SHORTCUTS; break;
                case 5: if (save_editor(&editor, true)) committed = done = true; break;
                }
                editor.selected = editor.top = 0;
            } else if (editor.page == TEMPLATES) {
                if (app->profile_dirty && !nav_ui_confirm("Discard unsaved preview and change profile?", draw_editor, &editor)) break;
                NavThemeResult theme; nav_theme_load_result(template_files[editor.selected], &theme);
                if (theme.fallback || theme.error[0]) snprintf(editor.message, sizeof editor.message, "%s", theme.error);
                else {
                    NavConfig *candidate = malloc(sizeof *candidate);
                    if (!candidate) snprintf(editor.message, sizeof editor.message, "Out of memory");
                    else {
                        if (nav_config_load_file(candidate, theme.path, editor.message, sizeof editor.message)) { free(candidate); break; }
                        candidate->proxy_mode = app->config.proxy_mode;
                        candidate->confirm_delete = app->config.confirm_delete;
                        candidate->confirm_overwrite = app->config.confirm_overwrite;
                        candidate->menu_remember_position = app->config.menu_remember_position;
                        candidate->history_enabled = app->config.history_enabled;
                        candidate->history_max_entries = app->config.history_max_entries;
                        candidate->transfer_buffer_size = app->config.transfer_buffer_size;
                        candidate->editor_wait = app->config.editor_wait;
                        candidate->editor_arg_count = app->config.editor_arg_count;
                        memcpy(candidate->editor_args, app->config.editor_args, sizeof candidate->editor_args);
                        candidate->repository_count = app->config.repository_count;
                        memcpy(candidate->repositories, app->config.repositories, sizeof candidate->repositories);
                        snprintf(candidate->editor_command, sizeof candidate->editor_command, "%s", app->config.editor_command);
                        snprintf(candidate->config_path, sizeof candidate->config_path, "%s", app->config.config_path);
                        app->config = *candidate; free(candidate);
                        live(&editor); editor.page = HOME; editor.selected = editor.top = 0;
                    }
                }
            } else if (editor.page == NETWORK) {
                int value = editor.proxy;
                if (nav_ui_select(" Proxy Mode ", proxy_options, 2, &value, NAV_CONTEXT_PREFERENCES, draw_editor, &editor)) editor.proxy = (NavProxyMode)value;
            } else if (editor.page == COLOURS) {
                int role = (int)(editor.selected / 2); uint8_t *colour = editor.selected % 2 ? &app->config.profile.background[role] : &app->config.profile.foreground[role];
                *colour = (uint8_t)((*colour + 16 + direction) % 16); live(&editor);
            } else if (editor.page == KEYS) edit_key(&editor, false, 1, false);
            else {
                size_t n; const Setting *setting = settings(editor.page, &n) + editor.selected;
                unsigned char *field = (unsigned char *)&app->config + setting->offset;
                if (setting->kind == BOOLEAN) *(bool *)field = !*(bool *)field;
                else if (setting->kind == ENUM) {
                    int value = *(int *)field, shift = setting->offset == offsetof(NavConfig, column_separator) ? 1 : 0;
                    value += shift;
                    if (nav_ui_select(setting->label, setting->values, (size_t)setting->count, &value, NAV_CONTEXT_PREFERENCES, draw_editor, &editor)) *(int *)field = value - shift;
                } else {
                    char value[128]; size_t capacity = setting->count ? (size_t)setting->count : 64;
                    snprintf(value, capacity, "%s", (char *)field);
                    if (!nav_ui_prompt_text(setting->label, "Value: ", value, capacity, draw_editor, &editor) && value[0]) snprintf((char *)field, capacity, "%s", value);
                }
                live(&editor);
            }
            break;
        }
        default: break;
        }
    }
    if (committed) {
        app->config.proxy_mode = editor.proxy;
        nav_profile_commit(&editor.session);
        if (app->profile_dirty) snprintf(app->status, sizeof app->status, "Profile modified (unsaved)");
        else if (nav_settings_dirty(app)) snprintf(app->status, sizeof app->status, "Settings modified (unsaved)");
    } else { nav_profile_cancel(app, &editor.session); nav_profile_changed(app); }
    nav_ui_profile_apply(app);
    if (!committed) nav_ui_profile_restore_panes(app, &editor.session);
}
