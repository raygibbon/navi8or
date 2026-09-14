#include "nav_profile.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{ FILE *f = fopen(path, "wb"); assert(f); assert(fputs(text, f) >= 0); assert(!fclose(f)); }
static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb"); assert(f); assert(!fseek(f, 0, SEEK_END)); long n = ftell(f); assert(n >= 0); rewind(f);
    char *text = malloc((size_t)n + 1); assert(text); assert(fread(text, 1, (size_t)n, f) == (size_t)n); text[n] = 0; fclose(f); return text;
}
static NavCommand resolve(const NavKeymap *map, NavInputContext context, const char *sequence)
{
    NavKeymap single = {0}; char error[128]; assert(!nav_keymap_bind(&single, context, sequence, "file.copy", error, sizeof error));
    NavInput state = {0}; NavAction action = {0};
    for (unsigned k = 0; k < single.bindings[0].length; k++) {
        NavKeyStroke key = single.bindings[0].keys[k]; NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = key.key, .modifiers = key.modifiers};
        action = nav_input_resolve(&state, map, context, &event);
    }
    return action.command;
}
#ifdef _WIN32
int main(int argc, char **argv)
{
    char root[NAV_PATH_MAX], path[NAV_PATH_MAX], error[512]; assert(argc == 2);
    snprintf(root, sizeof root, "%s", argv[1]); assert(!_putenv_s("APPDATA", root));
#else
int main(void)
{
    char root[] = "/tmp/nav-profile-editor-XXXXXX", path[NAV_PATH_MAX], error[512]; assert(mkdtemp(root));
    assert(!setenv("XDG_CONFIG_HOME", root, 1));
#endif
    NavApp *app = calloc(1, sizeof *app); NavConfig *loaded = malloc(sizeof *loaded); assert(app && loaded);
    assert(!nav_config_load(&app->config, error, sizeof error));
    const char *templates[] = {"classic-dos", "solar-dark", "solar-light", "monochrome"};
    for (size_t t = 0; t < 4; t++) {
        char template_path[128]; snprintf(template_path, sizeof template_path, "themes/%s.toml", templates[t]);
        char *example = read_text(template_path);
        assert(strstr(example, "[shortcuts]") && strstr(example, "[keys]"));
        assert(strstr(example, "location =") && strstr(example, "preferences ="));
        free(example);
        assert(!nav_config_load_file(loaded, template_path, error, sizeof error));
        assert(loaded->show_menu_keys && loaded->show_help_keys && loaded->show_dialog_keys && loaded->show_function_bar);
        assert(resolve(&loaded->keymap, NAV_CONTEXT_PANEL, "Ctrl+L") == NAV_CMD_OPEN_LOCATION);
        for (size_t b = 0; b < app->config.keymap.count; b++) {
            const NavBinding *binding = &app->config.keymap.bindings[b];
            char sequence[96]; assert(nav_binding_format(binding, sequence, sizeof sequence));
            if (resolve(&loaded->keymap, binding->context, sequence) != resolve(&app->config.keymap, binding->context, sequence)) {
                fprintf(stderr, "%s %s %s: %s vs %s\n", templates[t], nav_context_name(binding->context), sequence, nav_command_name(resolve(&loaded->keymap, binding->context, sequence)), nav_command_name(resolve(&app->config.keymap, binding->context, sequence)));
                abort();
            }
        }
        if (t == 1) assert(nav_profile_same_ui(loaded, &app->config));
    }
    char *modern_example = read_text("docs/examples/keymap.toml");
    assert(strstr(modern_example, "[keys]") && !strstr(modern_example, "[keys.global]")); free(modern_example);
    assert(!nav_config_load_file(loaded, "docs/examples/keymap.toml", error, sizeof error));
    assert(resolve(&loaded->keymap, NAV_CONTEXT_VIEWER, "Alt+D") == NAV_CMD_DOWNLOAD);
    NavProfileSession session; assert(!nav_profile_begin(app, &session));
    app->config.profile.foreground[NAV_STYLE_FILE] = 14; nav_profile_changed(app);
    assert(app->profile_dirty);
    app->config.profile.foreground[NAV_STYLE_FILE] = 7; nav_profile_changed(app); assert(!app->profile_dirty);
    app->config.profile.foreground[NAV_STYLE_FILE] = 14; nav_profile_changed(app);
    assert(!nav_profile_same_ui(&app->config, session.before));
    nav_profile_cancel(app, &session); assert(!app->profile_dirty && app->config.profile.foreground[NAV_STYLE_FILE] == 7);
    assert(!strcmp(nav_command_description(NAV_CMD_OPEN_LOCATION), "Open Location"));
    NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = 'C', .modifiers = NAV_MOD_CTRL}; NavKeyStroke capture;
    assert(!nav_key_capture_normalize(&event, &capture) && capture.key == 'c' && capture.modifiers == NAV_MOD_CTRL);
    event.key = NAV_KEY_INSERT; event.modifiers = 0; assert(!nav_key_capture_normalize(&event, &capture) && capture.key == NAV_KEY_INSERT);
    const char *copy[] = {"Ctrl+C", "Ctrl+F C"};
    assert(!nav_keymap_replace(&app->config.keymap, NAV_CONTEXT_PANEL, NAV_CMD_COPY, copy, 2, false, error, sizeof error));
    assert(resolve(&app->config.keymap, NAV_CONTEXT_PANEL, "Ctrl+C") == NAV_CMD_COPY);
    assert(resolve(&app->config.keymap, NAV_CONTEXT_PANEL, "Ctrl+F C") == NAV_CMD_COPY);
    assert(resolve(&app->config.keymap, NAV_CONTEXT_PANEL, "F5") != NAV_CMD_COPY);
    char labels[256]; assert(nav_keymap_labels(&app->config.keymap, NAV_CONTEXT_PANEL, NAV_CMD_COPY, labels, sizeof labels));
    assert(strstr(labels, "Ctrl+C") && strstr(labels, "Ctrl+F C"));
    const char *move[] = {"Ctrl+C"}; NavKeymap before = app->config.keymap;
    assert(nav_keymap_replace(&app->config.keymap, NAV_CONTEXT_PANEL, NAV_CMD_MOVE, move, 1, false, error, sizeof error) == 1);
    assert(strstr(error, "Ctrl+C") && strstr(error, "Copy") && strstr(error, "Move"));
    assert(!memcmp(&before, &app->config.keymap, sizeof before));
    assert(!nav_keymap_replace(&app->config.keymap, NAV_CONTEXT_PANEL, NAV_CMD_MOVE, move, 1, true, error, sizeof error));
    assert(resolve(&app->config.keymap, NAV_CONTEXT_PANEL, "Ctrl+C") == NAV_CMD_MOVE);
    assert(!nav_keymap_replace(&app->config.keymap, NAV_CONTEXT_PANEL, NAV_CMD_MOVE, NULL, 0, false, error, sizeof error));
    assert(resolve(&app->config.keymap, NAV_CONTEXT_PANEL, "Ctrl+C") != NAV_CMD_MOVE);
    const char *new_copy[] = {"F5", "Ctrl+C"}; assert(!nav_keymap_replace(&app->config.keymap, NAV_CONTEXT_PANEL, NAV_CMD_COPY, new_copy, 2, true, error, sizeof error));
    assert(nav_profile_is_template("themes/classic-dos.toml"));
    assert(nav_profile_is_template("Z:\\repo\\themes\\classic-dos.toml"));
    assert(!nav_profile_is_template("/config/profiles/classic-dos.toml"));
    char *template_before = read_text("themes/classic-dos.toml");
    assert(nav_profile_save(&app->config, "themes/classic-dos.toml", error, sizeof error));
    char *template_after = read_text("themes/classic-dos.toml"); assert(!strcmp(template_before, template_after)); free(template_before); free(template_after);
    assert(!nav_profile_user_path("Ray DOS", path, sizeof path, error, sizeof error));
#ifdef _WIN32
    assert(strstr(path, "/Navi8or/profiles/Ray DOS.toml"));
#else
    assert(strstr(path, "/nav/profiles/Ray DOS.toml"));
#endif
    write_text(path, "# Keep this close to Far Manager\n[profile]\nname=\"Ray DOS\" # identity\n[colors]\nfile=\"yellow\" # keep this comment\n[some_future_section]\nfoo = \"bar\"\narr = [\n  \"one\",\n  \"two\"\n]\n[keys]\ncopy=\"F5\" # binding comment\n[keys.panel]\n\"F6\"=\"file.move\" # old move\n");
    snprintf(app->config.profile.display_name, sizeof app->config.profile.display_name, "Ray DOS");
    app->config.show_menu_keys = app->config.show_dialog_keys = app->config.show_help_keys = false;
    app->config.show_function_bar = false; app->config.profile.background[NAV_STYLE_FILE] = 4;
    assert(!nav_profile_save(&app->config, path, error, sizeof error));
    char *saved = read_text(path);
    assert(strstr(saved, "Keep this close") && strstr(saved, "# identity") && strstr(saved, "# keep this comment") && strstr(saved, "binding comment"));
    assert(strstr(saved, "[some_future_section]\nfoo = \"bar\"\narr = [\n  \"one\",\n  \"two\"\n]"));
    free(saved);
    if (nav_config_load_file(loaded, path, error, sizeof error)) { fprintf(stderr, "%s\n", error); abort(); }
    assert(nav_profile_same_ui(&app->config, loaded));
    assert(!loaded->show_menu_keys && !loaded->show_dialog_keys && !loaded->show_help_keys && !loaded->show_function_bar);
    assert(resolve(&loaded->keymap, NAV_CONTEXT_PANEL, "Ctrl+C") == NAV_CMD_COPY);
    assert(resolve(&loaded->keymap, NAV_CONTEXT_VIEWER, "F10") == NAV_CMD_VIEWER_CLOSE);
    assert(!nav_profile_begin(app, &session)); app->config.show_menu = false; nav_profile_commit(&session); assert(!app->config.show_menu);
    /* Repeated saves are stable and keep comments/unknown data. */
    assert(!nav_profile_save(loaded, path, error, sizeof error));
    char *first = read_text(path); assert(!nav_profile_save(loaded, path, error, sizeof error)); char *second = read_text(path); assert(!strcmp(first, second)); free(first); free(second);
    write_text(path, "[profile]\n[shortcuts]\nshow_menu_keys=123\n"); assert(nav_config_load_file(loaded, path, error, sizeof error));
    write_text(path, "[profile]\nname=\"\"\"\nmultiline name\n\"\"\"\n"); char *unsafe_before = read_text(path);
    assert(nav_profile_save(&app->config, path, error, sizeof error)); char *unsafe_after = read_text(path); assert(!strcmp(unsafe_before, unsafe_after)); free(unsafe_before); free(unsafe_after);
    free(app->profile_saved); free(app); free(loaded); puts("Profile editing, capture, conflict resolution, persistence and TOML preservation: passed"); return 0;
}
