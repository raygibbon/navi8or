/* Shared live profile state and scoped, comment-preserving TOML updates.
 * tomlc99 has no writer. We update known assignments, never reserialize unknown
 * tables. Complex multiline assignments are refused rather than damaged. */
#include "nav_profile.h"
#include "toml.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static char *copy_bytes(const char *text, size_t n)
{
    char *copy = malloc(n + 1); if (!copy) return NULL;
    memcpy(copy, text, n); copy[n] = 0; return copy;
}

bool nav_settings_same(const NavConfig *a, const NavConfig *b)
{
    return a->proxy_mode == b->proxy_mode && a->confirm_delete == b->confirm_delete &&
        a->confirm_overwrite == b->confirm_overwrite && a->menu_remember_position == b->menu_remember_position &&
        a->history_enabled == b->history_enabled && a->editor_wait == b->editor_wait &&
        !strcmp(a->editor_command, b->editor_command);
}
bool nav_settings_dirty(const NavApp *app)
{ return app->settings_saved && !nav_settings_same(&app->config, app->settings_saved); }
void nav_settings_mark_saved(NavApp *app)
{
    if (!app->settings_saved) app->settings_saved = malloc(sizeof *app->settings_saved);
    if (app->settings_saved) *app->settings_saved = app->config;
}

void nav_profile_mark_saved(NavApp *app)
{
    if (!app->profile_saved) app->profile_saved = malloc(sizeof *app->profile_saved);
    if (app->profile_saved) *app->profile_saved = app->config;
    app->profile_dirty = false;
}
void nav_profile_changed(NavApp *app)
{ app->profile_dirty = !app->profile_saved || !nav_profile_same_ui(&app->config, app->profile_saved); }

int nav_profile_begin(NavApp *app, NavProfileSession *session)
{
    if (!app->settings_saved) { nav_settings_mark_saved(app); if (!app->settings_saved) return -1; }
    if (!app->profile_saved) { nav_profile_mark_saved(app); if (!app->profile_saved) return -1; }
    session->before = malloc(sizeof *session->before);
    if (!session->before) return -1;
    *session->before = app->config; session->dirty_before = app->profile_dirty;
    snprintf(session->status, sizeof session->status, "%s", app->status); session->status_kind = app->status_kind;
    session->hidden = app->show_hidden;
    for (int i = 0; i < 2; i++) {
        session->views[i] = app->panes[i].view; session->sorts[i] = app->panes[i].sort_mode;
        session->directories_first[i] = app->panes[i].directories_first; session->case_sensitive[i] = app->panes[i].case_sensitive_sort;
    }
    return 0;
}
void nav_profile_cancel(NavApp *app, NavProfileSession *session)
{
    if (session->before) app->config = *session->before;
    app->profile_dirty = session->dirty_before;
    snprintf(app->status, sizeof app->status, "%s", session->status); app->status_kind = session->status_kind;
    nav_profile_commit(session);
}
void nav_profile_commit(NavProfileSession *session)
{ free(session->before); session->before = NULL; }

bool nav_profile_same_ui(const NavConfig *a, const NavConfig *b)
{
#define SAME(field) if (a->field != b->field) return false
    SAME(show_menu); SAME(show_status); SAME(show_function_bar);
    SAME(show_app_identity);
    SAME(show_menu_keys); SAME(show_dialog_keys); SAME(show_help_keys);
    SAME(column_separator); SAME(pane_show_size); SAME(pane_show_modified); SAME(size_bytes);
    SAME(show_hidden); SAME(directories_first); SAME(case_sensitive_sort); SAME(sort); SAME(panel_view);
    SAME(viewer_line_numbers); SAME(viewer_wrap); SAME(viewer_current_line);
#undef SAME
    if (memcmp(a->profile.foreground, b->profile.foreground, sizeof a->profile.foreground) ||
        memcmp(a->profile.background, b->profile.background, sizeof a->profile.background) ||
        memcmp(&a->profile.symbols, &b->profile.symbols, sizeof a->profile.symbols) ||
        a->profile.style != b->profile.style || a->profile.frame_style != b->profile.frame_style ||
        a->profile.frame_space != b->profile.frame_space || a->profile.shadow != b->profile.shadow ||
        strcmp(a->profile.display_name, b->profile.display_name) || strcmp(a->date_format, b->date_format) || a->keymap.count != b->keymap.count) return false;
    /* Preserve priority within each command/context while ignoring unrelated
     * storage reordering caused by overlaying a sparse file. */
    for (NavInputContext context = 0; context < NAV_CONTEXT_COUNT; context++)
        for (NavCommand command = 0; command < NAV_CMD_COUNT; command++) {
            size_t i = 0, j = 0;
            for (;;) {
                while (i < a->keymap.count && (a->keymap.bindings[i].context != context || a->keymap.bindings[i].command != command)) i++;
                while (j < b->keymap.count && (b->keymap.bindings[j].context != context || b->keymap.bindings[j].command != command)) j++;
                if (i == a->keymap.count || j == b->keymap.count) { if (i != a->keymap.count || j != b->keymap.count) return false; break; }
                const NavBinding *x = &a->keymap.bindings[i++], *y = &b->keymap.bindings[j++];
                if (x->length != y->length || x->keys[0].key != y->keys[0].key || x->keys[0].modifiers != y->keys[0].modifiers ||
                    (x->length == 2 && (x->keys[1].key != y->keys[1].key || x->keys[1].modifiers != y->keys[1].modifiers))) return false;
            }
        }
    return true;
}

bool nav_profile_is_template(const char *path)
{
    char normalized[NAV_PATH_MAX];
    if (!path || !path[0]) return true;
    snprintf(normalized, sizeof normalized, "%s", path);
    for (char *p = normalized; *p; p++) { if (*p == '\\') *p = '/'; else *p = (char)tolower((unsigned char)*p); }
    const char *base = strrchr(normalized, '/'); base = base ? base + 1 : normalized;
    bool bundled = !strcmp(base, "classic-dos.toml") || !strcmp(base, "solar-dark.toml") ||
                   !strcmp(base, "solar-light.toml") || !strcmp(base, "monochrome.toml");
    return bundled && (strstr(normalized, "/themes/") || !strncmp(normalized, "themes/", 7) ||
                       strstr(normalized, "/share/navi8or/"));
}

int nav_profile_user_path(const char *name, char *path, size_t capacity, char *error, size_t size)
{
    char directory[NAV_PATH_MAX], profiles[NAV_PATH_MAX];
    if (!name[0] || strlen(name) > 63 || !strcmp(name, ".") || !strcmp(name, "..")) goto invalid;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 32 || strchr("/\\:*?\"<>|", *p)) goto invalid;
    if (nav_platform_config_dir(directory, sizeof directory) ||
        snprintf(profiles, sizeof profiles, "%s/profiles", directory) >= (int)sizeof profiles ||
        snprintf(path, capacity, "%s/%s.toml", profiles, name) >= (int)capacity) {
        snprintf(error, size, "unable to locate user profile directory"); return -1;
    }
    if ((nav_platform_mkdir(directory, 0700) && errno != EEXIST) ||
        (nav_platform_mkdir(profiles, 0700) && errno != EEXIST)) {
        snprintf(error, size, "cannot create %s: %s", profiles, strerror(errno)); return -1;
    }
    return 0;
invalid:
    snprintf(error, size, "profile name must be 1–63 characters without path or Windows filename characters"); return -1;
}

typedef struct { char **lines; size_t count; } Document;
static void document_free(Document *doc)
{ for (size_t i = 0; i < doc->count; i++) free(doc->lines[i]); free(doc->lines); }
static int insert(Document *doc, size_t at, const char *text)
{
    char *copy = strdup(text); if (!copy) return -1;
    char **lines = realloc(doc->lines, (doc->count + 1) * sizeof *lines);
    if (!lines) { free(copy); return -1; }
    doc->lines = lines;
    memmove(lines + at + 1, lines + at, (doc->count - at) * sizeof *lines);
    lines[at] = copy; doc->count++; return 0;
}
static char *skip_space(char *p) { while (*p == ' ' || *p == '\t' || *p == '\r') p++; return p; }
/* Known simple/dotted table headers; quoted spellings remain untouched and are
 * rejected by final TOML validation if a surgical update would duplicate them. */
static bool header(const char *line, char *out, size_t size)
{
    const char *p = line; while (isspace((unsigned char)*p)) p++;
    if (*p != '[') return false;
    if (p[1] == '[') { snprintf(out, size, "@array"); return true; }
    const char *end = strchr(p + 1, ']'); if (!end) return false;
    size_t n = (size_t)(end - p - 1); if (n >= size) return false;
    memcpy(out, p + 1, n); out[n] = 0; return true;
}
static bool assignment(char *line, char *key, size_t size, char **value, char **comment)
{
    char *p = skip_space(line), *end; if (!*p || *p == '#' || *p == '[') return false;
    if (*p == '"' || *p == '\'') {
        char quote = *p; end = p + 1;
        while (*end) { if (quote == '"' && *end == '\\' && end[1]) end += 2; else if (*end == quote) break; else end++; }
        if (!*end) return false;
        char *raw = copy_bytes(p, (size_t)(end - p + 1)), *decoded = NULL;
        if (!raw || toml_rtos(raw, &decoded) || !decoded || strlen(decoded) >= size) { free(raw); free(decoded); return false; }
        snprintf(key, size, "%s", decoded); free(raw); free(decoded); end++;
    } else {
        end = p; while (isalnum((unsigned char)*end) || *end == '_' || *end == '-') end++;
        size_t n = (size_t)(end - p); if (!n || n >= size) return false;
        memcpy(key, p, n); key[n] = 0;
    }
    end = skip_space(end); if (*end != '=') return false;
    *value = skip_space(end + 1); *comment = NULL;
    char quote = 0; bool escape = false;
    for (char *q = *value; *q; q++) {
        if (escape) { escape = false; continue; }
        if (quote == '"' && *q == '\\') { escape = true; continue; }
        if (quote) { if (*q == quote) quote = 0; }
        else if (*q == '"' || *q == '\'') quote = *q;
        else if (*q == '#') { *comment = q; break; }
    }
    return true;
}
static int quoted(const char *text, char *out, size_t size)
{
    size_t used = 0; if (size < 3) return -1; out[used++] = '"';
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p < 32) return -1;
        if (used + 3 >= size) return -1;
        if (*p == '"' || *p == '\\') out[used++] = '\\';
        out[used++] = (char)*p;
    }
    out[used++] = '"'; out[used] = 0; return 0;
}
static int update(Document *doc, const char *section, const char *key, const char *value)
{
    char current[128] = "", parsed[128], formatted[512], key_text[160];
    bool bare = key[0] != 0;
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) if (!isalnum(*p) || *p >= 128) { if (*p != '_' && *p != '-') bare = false; }
    if (bare) snprintf(key_text, sizeof key_text, "%s", key);
    else if (quoted(key, key_text, sizeof key_text)) return -1;
    size_t insertion = doc->count; bool exists = false;
    for (size_t i = 0; i < doc->count; i++) {
        if (header(doc->lines[i], parsed, sizeof parsed)) {
            if (!strcmp(current, section)) insertion = i;
            snprintf(current, sizeof current, "%s", parsed);
            if (!strcmp(current, section)) { exists = true; insertion = i + 1; }
            continue;
        }
        if (strcmp(current, section)) continue;
        if (insertion == i) insertion = i + 1;
        char *old, *comment;
        if (!assignment(doc->lines[i], parsed, sizeof parsed, &old, &comment) || strcmp(parsed, key)) continue;
        /* Refuse multiline known values; preserve the original file on failure. */
        if (!strncmp(old, "\"\"\"", 3) || !strncmp(old, "'''", 3) ||
            (*old == '[' && !strchr(old, ']'))) return -1;
        if (value) {
            char *end = comment ? comment : old + strlen(old);
            while (end > old && isspace((unsigned char)end[-1])) end--;
            if ((size_t)(end - old) == strlen(value) && !strncmp(old, value, strlen(value))) return 0;
        }
        size_t needed = strlen(key_text) + (value ? strlen(value) : 0) + (comment ? strlen(comment) : 0) + 16;
        char *copy = malloc(needed); if (!copy) return -1;
        if (value) snprintf(copy, needed, "%s = %s%s%s", key_text, value, comment ? " " : "", comment ? comment : "");
        else snprintf(copy, needed, "%s", comment ? comment : "");
        size_t n = strlen(copy); if (!n || copy[n - 1] != '\n') { copy[n++] = '\n'; copy[n] = 0; }
        free(doc->lines[i]); doc->lines[i] = copy; return 0;
    }
    if (!value) return 0;
    if (!exists) {
        snprintf(formatted, sizeof formatted, "\n[%s]\n", section);
        if (insert(doc, doc->count, formatted)) return -1;
        insertion = doc->count;
    }
    size_t needed = strlen(key_text) + strlen(value) + 6;
    char *line = malloc(needed); if (!line) return -1;
    snprintf(line, needed, "%s = %s\n", key_text, value);
    int result = insert(doc, insertion, line); free(line); return result;
}


static bool has_table(const Document *doc, const char *name)
{
    for (size_t i = 0; i < doc->count; i++) { char table[128]; if (header(doc->lines[i], table, sizeof table) && !strcmp(table, name)) return true; }
    return false;
}
static bool has_assignment(const Document *doc, const char *section, const char *name)
{
    char current[128] = "";
    for (size_t i = 0; i < doc->count; i++) {
        char key[128], *value, *comment;
        if (header(doc->lines[i], key, sizeof key)) { snprintf(current, sizeof current, "%s", key); continue; }
        if (!strcmp(current, section) && assignment(doc->lines[i], key, sizeof key, &value, &comment) && !strcmp(key, name)) return true;
    }
    return false;
}
static int binding_array(const NavKeymap *map, NavInputContext context, NavCommand command, char *out, size_t size)
{
    snprintf(out, size, "[");
    bool first = true;
    for (size_t i = 0; i < map->count; i++) {
        const NavBinding *b = &map->bindings[i]; if (b->context != context || b->command != command) continue;
        char sequence[80], text[164]; nav_binding_format(b, sequence, sizeof sequence);
        if (quoted(sequence, text, sizeof text)) return -1;
        size_t used = strlen(out), n = strlen(text);
        if (used + n + 4 >= size) return -1;
        snprintf(out + used, size - used, "%s%s", first ? "" : ", ", text); first = false;
    }
    size_t used = strlen(out); if (used + 2 >= size) return -1;
    snprintf(out + used, size - used, "]"); return 0;
}

/* Operational settings use the same surgical writer, but never profile data.
 * Validate the complete document before atomically replacing nav.toml. */
int nav_config_save_settings(const NavConfig *config, char *error, size_t size)
{
    Document doc = {0};
    const char *path = config->config_path;
    FILE *file = nav_platform_fopen(path, "rb");
    if (!file) goto failure;
    char *line = NULL; size_t capacity = 0;
    while (nav_platform_getline(&line, &capacity, file) >= 0)
        if (insert(&doc, doc.count, line)) { free(line); fclose(file); goto failure; }
    free(line); bool failed = ferror(file); fclose(file);
    if (failed) goto failure;
    if (nav_config_validate(config, error, size) || nav_profile_is_template(path) ||
        has_table(&doc, "profile") || has_assignment(&doc, "theme", "format")) goto failure;
#define SET(section, key, value) do { if (update(&doc, section, key, value)) goto failure; } while (0)
    SET("network", "proxy_mode", config->proxy_mode == NAV_PROXY_NONE ? "\"none\"" : "\"system\"");
    SET("general", "confirm_delete", config->confirm_delete ? "true" : "false");
    SET("general", "confirm_overwrite", config->confirm_overwrite ? "true" : "false");
    SET("menu", "remember_position", config->menu_remember_position ? "true" : "false");
    SET("history", "enabled", config->history_enabled ? "true" : "false");
    SET("editor", "wait", config->editor_wait ? "true" : "false");
    char value[256]; if (quoted(config->editor_command, value, sizeof value)) goto failure;
    SET("editor", "command", value);
    /* app.confirm_delete is a supported legacy alias with higher precedence. */
    if (has_assignment(&doc, "app", "confirm_delete"))
        SET("app", "confirm_delete", config->confirm_delete ? "true" : "false");
#undef SET
    size_t length = 1;
    for (size_t i = 0; i < doc.count; i++) length += strlen(doc.lines[i]);
    char *text = malloc(length); if (!text) goto failure;
    char *end = text;
    for (size_t i = 0; i < doc.count; i++) { size_t n = strlen(doc.lines[i]); memcpy(end, doc.lines[i], n); end += n; }
    *end = 0;
    char reason[256]; toml_table_t *root = toml_parse(text, reason, sizeof reason); free(text);
    if (!root) goto failure;
    toml_free(root);
    char temporary[NAV_PATH_MAX];
    if (snprintf(temporary, sizeof temporary, "%s.tmp-XXXXXX", path) >= (int)sizeof temporary) goto failure;
    int fd = nav_platform_mkstemp(temporary); if (fd < 0) goto failure;
    file = fdopen(fd, "wb");
    if (!file) { close(fd); nav_platform_unlink(temporary); goto failure; }
    failed = false;
    for (size_t i = 0; i < doc.count; i++) if (fputs(doc.lines[i], file) == EOF) failed = true;
    if (fflush(file) || nav_platform_sync(fd)) failed = true;
    if (fclose(file)) failed = true;
    NavConfig *check = malloc(sizeof *check);
    if (!check) failed = true;
    else {
        if (nav_config_load_operational_file(check, temporary, error, size) || !nav_settings_same(config, check)) failed = true;
        free(check);
    }
    if (!failed && nav_platform_replace(temporary, path)) failed = true;
    if (failed) { nav_platform_unlink(temporary); goto failure; }
    document_free(&doc); return 0;
failure:
    snprintf(error, size, "%s: cannot safely save operational settings; original retained", path);
    document_free(&doc); return -1;
}

int nav_profile_save(NavConfig *config, const char *path, char *error, size_t size)
{
    if (nav_profile_is_template(path) || !strcmp(path, config->config_path)) {
        snprintf(error, size, "template/operational config is read-only; use Save As"); return -1;
    }
    Document doc = {0}; NavConfig *base = malloc(sizeof *base);
    if (!base) { snprintf(error, size, "out of memory"); return -1; }
    char source[NAV_PATH_MAX] = "";
    FILE *file = nav_platform_fopen(path, "rb");
    if (file) snprintf(source, sizeof source, "%s", path);
    else if (errno != ENOENT) goto io_error;
    else if (config->profile_path[0] && strcmp(config->profile_path, config->config_path)) {
        snprintf(source, sizeof source, "%s", config->profile_path);
        file = nav_platform_fopen(source, "rb"); if (!file) goto io_error;
    } else {
        NavThemeResult palette; nav_theme_load_result(config->theme_name, &palette);
        if (palette.loaded_from_file) { snprintf(source, sizeof source, "%s", palette.path); file = nav_platform_fopen(source, "rb"); if (!file) goto io_error; }
    }
    if (file) {
        char *line = NULL; size_t capacity = 0; ssize_t n;
        while ((n = nav_platform_getline(&line, &capacity, file)) >= 0) {
            (void)n; if (insert(&doc, doc.count, line)) { free(line); fclose(file); goto edit_error; }
        }
        free(line); bool failed = ferror(file); fclose(file); if (failed) goto io_error;
    }
    if (nav_config_load_file(base, source[0] ? source : NULL, error, size)) { document_free(&doc); free(base); return -1; }
    /* Existing operational configs cannot be used as a Save destination. */
    const char *operations[] = {"app", "editor", "vault", "transfer", "repositories", "network", "credentials", "proxy", "cache"};
    for (size_t i = 0; i < sizeof operations / sizeof *operations; i++) if (has_table(&doc, operations[i])) {
        snprintf(error, size, "%s: operational configuration cannot be a UI profile", path); document_free(&doc); free(base); return -1;
    }
    bool convert = base->profile.format == 1;
    char value[256];
#define PUT(section, key, val) do { if (update(&doc, section, key, val)) goto edit_error; } while (0)
#define STR(section, key, val) do { if (quoted(val, value, sizeof value)) goto edit_error; PUT(section, key, value); } while (0)
#define BOOL(section, key, field) do { if (config->field != base->field) PUT(section, key, config->field ? "true" : "false"); } while (0)
    if (strcmp(config->profile.display_name, base->profile.display_name) || !has_table(&doc, "profile")) STR("profile", "name", config->profile.display_name);
    if (convert || !has_table(&doc, "profile")) PUT("profile", "format", "2");
    /* A profile metadata table defaults to format 2; the legacy theme metadata
     * stays intact. Explicit format is only needed to convert format 1. */
    if (convert) PUT("profile", "format", "2");
    if (convert || config->profile.style != base->profile.style) STR("ui", "style", config->profile.style == NAV_UI_STYLE_CLASSIC ? "classic" : "modern");
    bool text_changed = memcmp(config->profile.foreground + NAV_STYLE_TEXT, base->profile.foreground + NAV_STYLE_TEXT, 1) ||
                        memcmp(config->profile.background + NAV_STYLE_TEXT, base->profile.background + NAV_STYLE_TEXT, 1);
    /* Flat foreground/background aliases also affect background/surface. When
     * those roles diverge, materialize all three and retain their comments. */
    bool split = config->profile.foreground[NAV_STYLE_BACKGROUND] != config->profile.foreground[NAV_STYLE_TEXT] ||
                 config->profile.foreground[NAV_STYLE_SURFACE] != config->profile.foreground[NAV_STYLE_TEXT] ||
                 config->profile.background[NAV_STYLE_BACKGROUND] != config->profile.background[NAV_STYLE_TEXT] ||
                 config->profile.background[NAV_STYLE_SURFACE] != config->profile.background[NAV_STYLE_TEXT];
    bool aliases = has_assignment(&doc, "colors", "foreground") || has_assignment(&doc, "colors", "background");
    if (aliases && split) { PUT("colors", "foreground", NULL); PUT("colors", "background", NULL); }
    for (int role = 0; role < NAV_STYLE_COUNT; role++) {
        const char *section = nav_theme_role_name((NavStyle)role); if (!section) continue;
        bool force = convert || (text_changed && (role == NAV_STYLE_FILE || role == NAV_STYLE_DIRECTORY)) ||
                     (aliases && split && (role == NAV_STYLE_TEXT || role == NAV_STYLE_BACKGROUND || role == NAV_STYLE_SURFACE));
        bool fg = force || config->profile.foreground[role] != base->profile.foreground[role];
        bool bg = force || config->profile.background[role] != base->profile.background[role];
        if (fg) STR(section, "foreground", nav_theme_colour_name(config->profile.foreground[role]));
        if (bg) STR(section, "background", nav_theme_colour_name(config->profile.background[role]));
        char colors[96]; snprintf(colors, sizeof colors, "colors.%s", section + 3);
        if (has_table(&doc, colors)) {
            if (fg) STR(colors, "foreground", nav_theme_colour_name(config->profile.foreground[role]));
            if (bg) STR(colors, "background", nav_theme_colour_name(config->profile.background[role]));
        }
    }
    static const struct { const char *name; NavStyle role; bool background; } flat[] = {
        {"foreground", NAV_STYLE_TEXT, false}, {"background", NAV_STYLE_TEXT, true}, {"border", NAV_STYLE_BORDER, false},
        {"file", NAV_STYLE_FILE, false}, {"directory", NAV_STYLE_DIRECTORY, false}, {"selected_fg", NAV_STYLE_SELECTION, false}, {"selected_bg", NAV_STYLE_SELECTION, true},
        {"menu_fg", NAV_STYLE_MENU, false}, {"menu_bg", NAV_STYLE_MENU, true}, {"menu_selected_fg", NAV_STYLE_MENU_SELECTED, false}, {"menu_selected_bg", NAV_STYLE_MENU_SELECTED, true},
        {"status_fg", NAV_STYLE_STATUS, false}, {"status_bg", NAV_STYLE_STATUS, true}, {"function_key_fg", NAV_STYLE_KEYBAR, false}, {"function_key_bg", NAV_STYLE_KEYBAR, true}};
    for (size_t i = 0; i < sizeof flat / sizeof *flat; i++) if (has_assignment(&doc, "colors", flat[i].name)) {
        const uint8_t *now = flat[i].background ? config->profile.background : config->profile.foreground;
        const uint8_t *old = flat[i].background ? base->profile.background : base->profile.foreground;
        if (now[flat[i].role] != old[flat[i].role]) STR("colors", flat[i].name, nav_theme_colour_name(now[flat[i].role]));
    }
    static const char *frames[] = {"ascii", "single", "double", "combine", "combine_reverse", "block"};
    if (convert || config->profile.frame_style != base->profile.frame_style) {
        STR("ui.frame", "style", frames[config->profile.frame_style]);
        if (has_assignment(&doc, "layout", "border_style")) STR("layout", "border_style", frames[config->profile.frame_style]);
    }
    if (convert || config->profile.frame_space != base->profile.frame_space) { PUT("ui.frame", "space", config->profile.frame_space ? "true" : "false"); if (has_assignment(&doc, "layout", "space")) PUT("layout", "space", config->profile.frame_space ? "true" : "false"); }
    if (convert || config->profile.shadow != base->profile.shadow) { PUT("ui.frame", "shadow", config->profile.shadow ? "true" : "false"); if (has_assignment(&doc, "layout", "shadow")) PUT("layout", "shadow", config->profile.shadow ? "true" : "false"); }
    BOOL("layout", "show_menu", show_menu); BOOL("layout", "show_status", show_status);
    BOOL("layout", "show_function_bar", show_function_bar);
    BOOL("layout", "show_app_identity", show_app_identity);
    if (config->column_separator != base->column_separator) {
        if (config->column_separator < 0) STR("layout", "show_column_separator", "auto");
        else PUT("layout", "show_column_separator", config->column_separator ? "true" : "false");
    }
    BOOL("panes", "show_size", pane_show_size); BOOL("panes", "show_modified", pane_show_modified);
    BOOL("panes", "directories_first", directories_first); BOOL("panes", "show_hidden", show_hidden); BOOL("panes", "case_sensitive_sort", case_sensitive_sort);
    if (config->panel_view != base->panel_view) STR("panes", "view", config->panel_view == NAV_PANEL_FULL ? "full" : "brief");
    if (config->sort != base->sort) STR("panes", "sort", config->sort == NAV_SORT_DATE ? "date" : config->sort == NAV_SORT_SIZE ? "size" : "name");
    if (config->size_bytes != base->size_bytes) STR("panes", "size_format", config->size_bytes ? "bytes" : "auto");
    if (strcmp(config->date_format, base->date_format)) STR("panes", "date_format", config->date_format);
    BOOL("viewer", "wrap", viewer_wrap); BOOL("viewer", "line_numbers", viewer_line_numbers); BOOL("viewer", "current_line", viewer_current_line);
    BOOL("shortcuts", "show_function_bar", show_function_bar); BOOL("shortcuts", "show_menu_keys", show_menu_keys);
    BOOL("shortcuts", "show_dialog_keys", show_dialog_keys); BOOL("shortcuts", "show_help_keys", show_help_keys);
    const char *symbol_names[] = {"directory", "parent", "selected", "upload", "download"};
    const uint32_t symbols[] = {config->profile.symbols.directory, config->profile.symbols.parent, config->profile.symbols.selected, config->profile.symbols.upload, config->profile.symbols.download};
    const uint32_t old_symbols[] = {base->profile.symbols.directory, base->profile.symbols.parent, base->profile.symbols.selected, base->profile.symbols.upload, base->profile.symbols.download};
    for (size_t i = 0; i < 5; i++) if (convert || symbols[i] != old_symbols[i]) { char utf8[7]; int n = toml_ucs_to_utf8(symbols[i], utf8); if (n <= 0) goto edit_error; utf8[n] = 0; STR("symbols", symbol_names[i], utf8); }
    char *array = malloc(NAV_BINDING_MAX * 170u), *old_array = malloc(NAV_BINDING_MAX * 170u);
    if (!array || !old_array) { free(array); free(old_array); goto edit_error; }
    for (NavInputContext context = 0; context < NAV_CONTEXT_COUNT; context++) for (NavCommand command = 1; command < NAV_CMD_COUNT; command++) {
        if (command == NAV_CMD_TEXT) continue;
        if (binding_array(&config->keymap, context, command, array, NAV_BINDING_MAX * 170u) || binding_array(&base->keymap, context, command, old_array, NAV_BINDING_MAX * 170u)) { free(array); free(old_array); goto edit_error; }
        if (!strcmp(array, old_array)) continue;
        /* Preserve a user's flat command assignment and its comment if present. */
        char target[128], key[128]; bool root_key = false;
        char section[128] = "";
        for (size_t i = 0; i < doc.count; i++) {
            char parsed[128], *old, *comment;
            if (header(doc.lines[i], parsed, sizeof parsed)) { snprintf(section, sizeof section, "%s", parsed); continue; }
            if (!assignment(doc.lines[i], parsed, sizeof parsed, &old, &comment)) continue;
            if (!strcmp(section, "keys") && nav_command_parse(parsed) == command) {
                NavKeymap defaults; nav_keymap_defaults(&defaults); NavInputContext owner = NAV_CONTEXT_PANEL;
                for (size_t j = 0; j < defaults.count; j++) if (defaults.bindings[j].command == command) { owner = defaults.bindings[j].context; break; }
                if (owner == context) { snprintf(key, sizeof key, "%s", parsed); root_key = true; }
            }
            char legacy[128]; snprintf(legacy, sizeof legacy, "keys.%s", nav_context_name(context));
            if (!strcmp(section, legacy)) {
                char *name = NULL;
                const char *end = comment ? comment : old + strlen(old);
                while (end > old && isspace((unsigned char)end[-1])) end--;
                char *raw = copy_bytes(old, (size_t)(end - old));
                if (raw && !toml_rtos(raw, &name)) { bool remove = nav_command_parse(name) == command; free(name); if (remove && update(&doc, section, parsed, NULL)) { free(raw); free(array); free(old_array); goto edit_error; } }
                free(raw);
            }
        }
        snprintf(target, sizeof target, "keys.%s.commands", nav_context_name(context));
        bool scoped = has_assignment(&doc, target, nav_command_name(command)) || has_assignment(&doc, target, nav_command_config_name(command));
        if (scoped) {
            snprintf(key, sizeof key, "%s", has_assignment(&doc, target, nav_command_config_name(command)) ? nav_command_config_name(command) : nav_command_name(command));
        } else {
            NavKeymap defaults; nav_keymap_defaults(&defaults); NavInputContext owner = NAV_CONTEXT_PANEL;
            for (size_t j = 0; j < defaults.count; j++) if (defaults.bindings[j].command == command) { owner = defaults.bindings[j].context; break; }
            if (root_key || owner == context) { snprintf(target, sizeof target, "keys"); if (!root_key) snprintf(key, sizeof key, "%s", nav_command_config_name(command)); }
            else snprintf(key, sizeof key, "%s", nav_command_name(command));
        }
        if (update(&doc, target, key, array)) { free(array); free(old_array); goto edit_error; }
    }
    free(array); free(old_array);
#undef PUT
#undef STR
#undef BOOL
    size_t length = 1; for (size_t i = 0; i < doc.count; i++) length += strlen(doc.lines[i]);
    char *text = malloc(length); if (!text) goto edit_error;
    char *end = text; for (size_t i = 0; i < doc.count; i++) { size_t n = strlen(doc.lines[i]); memcpy(end, doc.lines[i], n); end += n; } *end = 0;
    char reason[256]; toml_table_t *root = toml_parse(text, reason, sizeof reason); free(text);
    if (!root) { snprintf(error, size, "%s: refusing unsafe TOML update: %s", path, reason); document_free(&doc); free(base); return -1; }
    toml_free(root);
    char temporary[NAV_PATH_MAX];
    if (snprintf(temporary, sizeof temporary, "%s.tmp-XXXXXX", path) >= (int)sizeof temporary) goto edit_error;
    int fd = nav_platform_mkstemp(temporary); if (fd < 0) goto io_error;
    file = fdopen(fd, "wb"); if (!file) { close(fd); nav_platform_unlink(temporary); goto io_error; }
    bool failed = false;
    for (size_t i = 0; i < doc.count; i++) if (fputs(doc.lines[i], file) == EOF) failed = true;
    if (fflush(file) || nav_platform_sync(fd)) failed = true;
    if (fclose(file)) failed = true;
    /* Validate values/key conflicts through the same loader before replacing. */
    if (!failed && nav_config_load_file(base, temporary, error, size)) { nav_platform_unlink(temporary); document_free(&doc); free(base); return -1; }
    if (!failed && !nav_profile_same_ui(config, base)) {
        snprintf(error, size, "%s: scoped update could not preserve the requested profile; original retained", path);
        nav_platform_unlink(temporary); document_free(&doc); free(base); return -1;
    }
    if (failed || nav_platform_replace(temporary, path)) { nav_platform_unlink(temporary); goto io_error; }
    snprintf(config->profile_path, sizeof config->profile_path, "%s", path); config->explicit_config = true; config->profile.format = 2;
    document_free(&doc); free(base); return 0;
io_error:
    snprintf(error, size, "%s: %s", path, strerror(errno)); document_free(&doc); free(base); return -1;
edit_error:
    snprintf(error, size, "%s: allocation failure or unsupported multiline assignment; edit this value manually", path);
    document_free(&doc); free(base); return -1;
}
