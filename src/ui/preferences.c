/* One reusable list editor over the same NavConfig used by TOML loading. */
#include "nav_profile.h"
#include "nav_ui_core.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { HOME, TEMPLATES, COLOURS, LAYOUT, PANES, VIEWER, KEYS, SHORTCUTS } Page;
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
#undef BOOL
#undef ENUMS

typedef struct {
    NavApp *app; NavProfileSession session; Page page; size_t selected, top;
    NavInputContext context; NavUiRedrawFn redraw; void *data;
    char message[256]; NavCommand commands[NAV_CMD_COUNT]; size_t command_count;
} Editor;
static const char *const home[] = {"Profile Template", "Profile Name", "Colours", "Layout", "Panels", "Viewer", "Key Bindings", "Shortcut Display", "Apply", "Save Profile", "Save Profile As", "Cancel"};
static const char *const templates[] = {"Classic DOS", "Solar Dark", "Solar Light", "Monochrome"};
static const char *const template_files[] = {"classic-dos", "solar-dark", "solar-light", "monochrome"};

static const Setting *settings(Page page, size_t *count)
{
#define PAGE(id, array) if (page == id) { *count = sizeof array / sizeof *array; return array; }
    PAGE(LAYOUT, layout); PAGE(PANES, panes); PAGE(VIEWER, viewer); PAGE(SHORTCUTS, shortcuts);
#undef PAGE
    *count = 0; return NULL;
}
static NavInputContext default_context(NavCommand command)
{
    NavKeymap map; nav_keymap_defaults(&map);
    for (size_t i = 0; i < map.count; i++) if (map.bindings[i].command == command) return map.bindings[i].context;
    return NAV_CONTEXT_PANEL;
}
static size_t rows(Editor *editor)
{
    if (editor->page == HOME) return sizeof home / sizeof *home;
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
        snprintf(text, sizeof text, "%s", n >= 0 && n < setting->count ? setting->values[n] : "Invalid"); value = text;
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
    char title[128]; snprintf(title, sizeof title, " Preferences%s - %s ", editor->app->profile_dirty ? " *" : "", editor->app->config.profile.display_name);
    nav_ui_box(x, y, w, h, title, NAV_STYLE_DIALOG);
    size_t count = rows(editor), visible = (size_t)(h - 5);
    if (editor->selected >= count) editor->selected = count ? count - 1 : 0;
    if (editor->selected < editor->top) editor->top = editor->selected;
    if (editor->selected >= editor->top + visible) editor->top = editor->selected - visible + 1;
    for (size_t i = 0; i < visible; i++) {
        char text[320] = ""; size_t index = editor->top + i;
        if (index < count) row_text(editor, index, text, sizeof text);
        nav_ui_text(x + 2, y + 1 + (int)i, w - 4, text, index == editor->selected ? NAV_STYLE_SELECTION : NAV_STYLE_DIALOG);
    }
    nav_ui_text(x + 2, y + h - 3, w - 4, editor->message, NAV_STYLE_MESSAGE);
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

void nav_ui_preferences(NavApp *app, NavUiRedrawFn redraw, void *data)
{
    Editor editor = {.app = app, .context = NAV_CONTEXT_PANEL, .redraw = redraw, .data = data};
    if (nav_profile_begin(app, &editor.session)) { const char *lines[] = {"Out of memory"}; nav_ui_info(" Preferences ", lines, 1); return; }
    bool committed = false, done = false;
    while (!done) {
        draw_editor(&editor); nav_term_present(); NavAction event;
        if (nav_ui_input(NAV_CONTEXT_PREFERENCES, &event) <= 0) continue;
        if (event.type != NAV_TERM_EVENT_KEY) continue;
        size_t count = rows(&editor);
        switch (event.command) {
        case NAV_CMD_UP: if (editor.selected) editor.selected--; break;
        case NAV_CMD_DOWN: if (editor.selected + 1 < count) editor.selected++; break;
        case NAV_CMD_HOME: editor.selected = 0; break;
        case NAV_CMD_END: editor.selected = count ? count - 1 : 0; break;
        case NAV_CMD_PAGE_UP: editor.selected = editor.selected > 10 ? editor.selected - 10 : 0; break;
        case NAV_CMD_PAGE_DOWN: editor.selected = editor.selected + 10 < count ? editor.selected + 10 : count - 1; break;
        case NAV_CMD_CANCEL: if (editor.page != HOME) { editor.page = HOME; editor.selected = editor.top = 0; } else done = true; break;
        case NAV_CMD_HELP: nav_ui_binding_help(NAV_CONTEXT_PREFERENCES); break;
        case NAV_CMD_PROFILE_APPLY: committed = done = true; break;
        case NAV_CMD_PROFILE_SAVE: case NAV_CMD_PROFILE_SAVE_AS:
            if (nav_ui_profile_save(app, event.command == NAV_CMD_PROFILE_SAVE_AS, draw_editor, &editor)) committed = done = true;
            break;
        case NAV_CMD_TEXT_DELETE: if (editor.page == KEYS) edit_key(&editor, false, 0, true); break;
        case NAV_CMD_KEY_APPEND: if (editor.page == KEYS) edit_key(&editor, true, 1, false); break;
        case NAV_CMD_KEY_SEQUENCE: if (editor.page == KEYS) edit_key(&editor, false, 2, false); break;
        case NAV_CMD_LEFT: case NAV_CMD_RIGHT: case NAV_CMD_ACCEPT: {
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
                case 4: editor.page = PANES; break; case 5: editor.page = VIEWER; break;
                case 6: editor.page = KEYS; break; case 7: editor.page = SHORTCUTS; break;
                case 8: committed = done = true; break;
                case 9: case 10:
                    if (nav_ui_profile_save(app, editor.selected == 10, draw_editor, &editor)) committed = done = true;
                    break;
                case 11: done = true; break;
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
                        app->config = *candidate; free(candidate);
                        live(&editor); editor.page = HOME; editor.selected = editor.top = 0;
                    }
                }
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
                    *(int *)field = (value + shift + setting->count + direction) % setting->count - shift;
                } else {
                    char value[64]; snprintf(value, sizeof value, "%s", (char *)field);
                    if (!nav_ui_prompt_text(setting->label, "Value: ", value, sizeof value, draw_editor, &editor) && value[0]) snprintf((char *)field, 64, "%s", value);
                }
                live(&editor);
            }
            break;
        }
        default: break;
        }
    }
    if (committed) {
        nav_profile_commit(&editor.session);
        if (app->profile_dirty) snprintf(app->status, sizeof app->status, "Profile modified (unsaved)");
    } else nav_profile_cancel(app, &editor.session);
    nav_ui_profile_apply(app);
    if (!committed) nav_ui_profile_restore_panes(app, &editor.session);
}
