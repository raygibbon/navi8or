#include "nav_theme.h"
#include "nav.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void assert_resolved(const NavTheme *theme)
{
    for (int style = 0; style < NAV_STYLE_COUNT; style++)
    {
        assert(theme->foreground[style] < 16);
        assert(theme->background[style] < 16);
    }
}

static void test_legacy(void)
{
    NavThemeResult result;
    assert(nav_theme_load_file("tests/fixtures/theme-v1.toml", "legacy",
                               &result) == 0);
    assert(result.loaded_from_file && !result.fallback);
    assert(result.theme.format == 1);
    assert(result.theme.style == NAV_UI_STYLE_CLASSIC);
    assert(strstr(result.warning, "legacy"));
    assert(result.theme.foreground[NAV_STYLE_SELECTION] == 14);
    assert(result.theme.background[NAV_STYLE_SELECTION] == 4);
    assert(result.theme.foreground[NAV_STYLE_MENU_SELECTED] == 15);
    assert(result.theme.background[NAV_STYLE_MENU_SELECTED] == 2);
    assert(result.theme.foreground[NAV_STYLE_PANE_TITLE_ACTIVE] == 14);
    assert(result.theme.background[NAV_STYLE_PANE_TITLE_ACTIVE] == 3);
    assert(result.theme.foreground[NAV_STYLE_KEYBAR_KEY] == 15);
    assert(result.theme.background[NAV_STYLE_KEYBAR_KEY] == 1);
    assert(result.theme.foreground[NAV_STYLE_KEYBAR_DISABLED] == 8);
    assert(result.theme.background[NAV_STYLE_KEYBAR_DISABLED] == 3);
    assert(result.theme.symbols.directory == 'D');
    assert(result.theme.frame_style == NAV_FRAME_DOUBLE);
    assert(!result.theme.frame_space && !result.theme.shadow);
    assert_resolved(&result.theme);
}

static void test_v2(void)
{
    static const uint8_t foreground[NAV_STYLE_COUNT] = {
        [NAV_STYLE_BACKGROUND] = 15, [NAV_STYLE_SURFACE] = 7,
        [NAV_STYLE_SURFACE_ALT] = 0, [NAV_STYLE_TEXT] = 7,
        [NAV_STYLE_TEXT_DIM] = 8, [NAV_STYLE_ACCENT] = 14,
        [NAV_STYLE_BORDER] = 3, [NAV_STYLE_BORDER_ACTIVE] = 11,
        [NAV_STYLE_SELECTION] = 15, [NAV_STYLE_SELECTION_INACTIVE] = 7,
        [NAV_STYLE_HEADER] = 11, [NAV_STYLE_STATUS] = 15,
        [NAV_STYLE_MESSAGE] = 15, [NAV_STYLE_ERROR] = 15,
        [NAV_STYLE_WARNING] = 0, [NAV_STYLE_DIALOG] = 0,
        [NAV_STYLE_DIALOG_TITLE] = 1, [NAV_STYLE_MENU] = 0,
        [NAV_STYLE_MENU_SELECTED] = 0, [NAV_STYLE_MENU_DISABLED] = 8,
        [NAV_STYLE_MENU_SELECTED_DISABLED] = 8, [NAV_STYLE_KEYBAR] = 0,
        [NAV_STYLE_KEYBAR_SELECTED] = 14, [NAV_STYLE_KEYBAR_KEY] = 15,
        [NAV_STYLE_KEYBAR_DISABLED] = 8, [NAV_STYLE_PATH] = 11,
        [NAV_STYLE_COLUMN_HEADER] = 8, [NAV_STYLE_VIEWER_LINE_NUMBER] = 8,
        [NAV_STYLE_VIEWER_SEARCH_MATCH] = 0, [NAV_STYLE_PROGRESS] = 0,
        [NAV_STYLE_PANE_TITLE] = 0, [NAV_STYLE_PANE_TITLE_ACTIVE] = 14,
        [NAV_STYLE_FILE] = 7, [NAV_STYLE_DIRECTORY] = 7
    };
    static const uint8_t background[NAV_STYLE_COUNT] = {
        [NAV_STYLE_BACKGROUND] = 0, [NAV_STYLE_SURFACE] = 1,
        [NAV_STYLE_SURFACE_ALT] = 3, [NAV_STYLE_TEXT] = 0,
        [NAV_STYLE_TEXT_DIM] = 0, [NAV_STYLE_ACCENT] = 1,
        [NAV_STYLE_BORDER] = 0, [NAV_STYLE_BORDER_ACTIVE] = 1,
        [NAV_STYLE_SELECTION] = 5, [NAV_STYLE_SELECTION_INACTIVE] = 1,
        [NAV_STYLE_HEADER] = 4, [NAV_STYLE_STATUS] = 2,
        [NAV_STYLE_MESSAGE] = 4, [NAV_STYLE_ERROR] = 4,
        [NAV_STYLE_WARNING] = 14, [NAV_STYLE_DIALOG] = 7,
        [NAV_STYLE_DIALOG_TITLE] = 7, [NAV_STYLE_MENU] = 7,
        [NAV_STYLE_MENU_SELECTED] = 3, [NAV_STYLE_MENU_DISABLED] = 0,
        [NAV_STYLE_MENU_SELECTED_DISABLED] = 3, [NAV_STYLE_KEYBAR] = 3,
        [NAV_STYLE_KEYBAR_SELECTED] = 3, [NAV_STYLE_KEYBAR_KEY] = 1,
        [NAV_STYLE_KEYBAR_DISABLED] = 3, [NAV_STYLE_PATH] = 0,
        [NAV_STYLE_COLUMN_HEADER] = 0, [NAV_STYLE_VIEWER_LINE_NUMBER] = 0,
        [NAV_STYLE_VIEWER_SEARCH_MATCH] = 14, [NAV_STYLE_PROGRESS] = 7,
        [NAV_STYLE_PANE_TITLE] = 3, [NAV_STYLE_PANE_TITLE_ACTIVE] = 3
    };
    NavThemeResult result;
    assert(nav_theme_load_file("tests/fixtures/theme-v2.toml", "semantic",
                               &result) == 0);
    assert(result.theme.format == 2);
    assert(result.theme.style == NAV_UI_STYLE_MODERN);
    assert(!strcmp(result.theme.display_name, "Semantic fixture"));
    for (int style = 0; style < NAV_STYLE_COUNT; style++)
    {
        assert(result.theme.foreground[style] == foreground[style]);
        assert(result.theme.background[style] == background[style]);
    }
    assert_resolved(&result.theme);
}

static void test_builtins(void)
{
    static const struct { const char *name; NavUiStyle style; } themes[] = {
        {"classic-dos", NAV_UI_STYLE_CLASSIC},
        {"monochrome", NAV_UI_STYLE_CLASSIC},
        {"solar-dark", NAV_UI_STYLE_MODERN},
        {"solar-light", NAV_UI_STYLE_MODERN}
    };
    for (size_t index = 0; index < sizeof themes / sizeof *themes; index++)
    {
        char path[256];
        NavThemeResult result;
        snprintf(path, sizeof path, "themes/%s.toml", themes[index].name);
        assert(nav_theme_load_file(path, themes[index].name, &result) == 0);
        assert(result.theme.format == 2);
        assert(result.theme.style == themes[index].style);
        assert(!result.warning[0]);
        assert_resolved(&result.theme);
        assert(result.theme.foreground[NAV_STYLE_KEYBAR_KEY] !=
                   result.theme.foreground[NAV_STYLE_KEYBAR_DISABLED] ||
               result.theme.background[NAV_STYLE_KEYBAR_KEY] !=
                   result.theme.background[NAV_STYLE_KEYBAR_DISABLED]);
    }
}

static void test_fallbacks_and_errors(void)
{
    char directory[] = "/tmp/nav-theme-XXXXXX";
    char path[256];
    NavThemeResult result;
    assert(mkdtemp(directory));
    snprintf(path, sizeof path, "%s/fallback.toml", directory);
    write_text(path,
               "[theme]\nformat = 2\nname = \"Fallback fixture\"\n"
               "[ui.background]\nforeground = \"white\"\n"
               "background = \"black\"\n");
    assert(nav_theme_load_file(path, "fallback", &result) == 0);
    assert(result.theme.foreground[NAV_STYLE_TEXT] == 15);
    assert(result.theme.background[NAV_STYLE_TEXT] == 0);
    assert(result.theme.foreground[NAV_STYLE_BORDER_ACTIVE] == 15);
    assert(result.theme.background[NAV_STYLE_BORDER_ACTIVE] == 0);
    assert(result.theme.foreground[NAV_STYLE_PATH] == 15);
    assert(result.theme.background[NAV_STYLE_PATH] == 0);
    assert(result.theme.foreground[NAV_STYLE_COLUMN_HEADER] == 15);
    assert(result.theme.background[NAV_STYLE_COLUMN_HEADER] == 0);
    assert_resolved(&result.theme);

    snprintf(path, sizeof path, "%s/invalid.toml", directory);
    write_text(path,
               "[theme]\nformat = 2\n"
               "[ui.text]\nforeground = \"ultraviolet\"\n");
    assert(nav_theme_load_file(path, "invalid", &result) != 0);
    assert(result.fallback);
    assert(strstr(result.error, "unknown colour"));

    snprintf(path, sizeof path, "%s/invalid-style.toml", directory);
    write_text(path, "[theme]\nformat = 2\n[ui]\nstyle = \"ornate\"\n");
    assert(nav_theme_load_file(path, "invalid-style", &result) != 0);
    assert(strstr(result.error, "ui.style"));

    snprintf(path, sizeof path, "%s/malformed.toml", directory);
    write_text(path, "[theme\nformat = 2\n");
    assert(nav_theme_load_file(path, "malformed", &result) != 0);
    assert(strstr(result.error, "parse error"));

    snprintf(path, sizeof path, "%s/fallback.toml", directory);
    assert(unlink(path) == 0);
    snprintf(path, sizeof path, "%s/invalid.toml", directory);
    assert(unlink(path) == 0);
    snprintf(path, sizeof path, "%s/malformed.toml", directory);
    assert(unlink(path) == 0);
    snprintf(path, sizeof path, "%s/invalid-style.toml", directory);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

static NavCommand resolve(const NavKeymap *map, const char *key)
{
    NavKeyStroke stroke; NavInput input = {0};
    assert(nav_key_parse(key, &stroke) == 0);
    NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = stroke.key,
                         .modifiers = stroke.modifiers};
    return nav_input_resolve(&input, map, NAV_CONTEXT_PANEL, &event).command;
}

static void test_profiles(void)
{
    char directory[] = "/tmp/nav-profile-XXXXXX", path[512], error[512];
    NavConfig defaults, config; assert(mkdtemp(directory));
    assert(setenv("XDG_CONFIG_HOME", directory, 1) == 0);
    assert(nav_config_load(&defaults, error, sizeof error) == 0);
    assert(defaults.show_menu && defaults.show_status && defaults.show_function_bar);
    assert(defaults.pane_show_size && defaults.pane_show_modified);
    assert(resolve(&defaults.keymap, "F5") == NAV_CMD_COPY);
    DIR *shipped = opendir("themes"); assert(shipped);
    size_t files = 0; struct dirent *item;
    while ((item = readdir(shipped))) { size_t n = strlen(item->d_name); if (n > 5 && !strcmp(item->d_name + n - 5, ".toml")) files++; }
    assert(closedir(shipped) == 0 && files == 4);
    const char *profiles[] = {"classic-dos", "solar-dark", "solar-light", "monochrome"};
    for (size_t i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "themes/%s.toml", profiles[i]);
        assert(nav_config_load_file(&config, path, error, sizeof error) == 0);
        assert(!strcmp(config.profile_path, path));
        assert_resolved(&config.profile);
    }
    assert(nav_config_load_file(&config, "tests/fixtures/theme-v1.toml", error, sizeof error) == 0);
    assert(config.profile.format == 1 && config.profile.style == NAV_UI_STYLE_CLASSIC);
    snprintf(path, sizeof path, "%s/custom.toml", directory);
    write_text(path, "[profile]\nname=\"Custom\"\n[colors]\nfile=\"yellow\"\n"
                     "[layout]\nshow_menu=false\nshow_status=false\nshow_function_bar=false\nshow_column_separator=true\n"
                     "[panes]\nshow_size=false\nsize_format=\"bytes\"\ndate_format=\"%Y\"\n"
                     "[viewer]\nwrap=true\n[keys]\ncopy=[\"F6\",\"Ctrl+C\"]\nmove=\"F5\"\n");
    assert(nav_config_load_file(&config, path, error, sizeof error) == 0);
    assert(config.profile.foreground[NAV_STYLE_FILE] == 14);
    assert(config.profile.foreground[NAV_STYLE_DIRECTORY] == defaults.profile.foreground[NAV_STYLE_DIRECTORY]);
    assert(!config.show_menu && !config.show_status && !config.show_function_bar);
    assert(config.column_separator == 1 && !config.pane_show_size && config.size_bytes && config.viewer_wrap);
    assert(config.pane_show_modified && !strcmp(config.date_format, "%Y"));
    assert(resolve(&config.keymap, "F6") == NAV_CMD_COPY);
    assert(resolve(&config.keymap, "Ctrl+C") == resolve(&defaults.keymap, "F5"));
    assert(resolve(&config.keymap, "F5") == NAV_CMD_MOVE);
    NavFunctionKeySegment segments[12];
    size_t count = nav_function_key_layout(120, &config.keymap, NAV_CONTEXT_PANEL, segments, 12);
    bool copy = false, move = false;
    for (size_t i = 0; i < count; i++) {
        if (segments[i].key == 6) copy = segments[i].command == NAV_CMD_COPY && !strcmp(segments[i].label, "Copy");
        if (segments[i].key == 5) move = segments[i].command == NAV_CMD_MOVE && !strcmp(segments[i].label, "Move");
    }
    assert(copy && move);
    NavCommanderLayout before, after;
    assert(nav_commander_layout_for_style(120, 30, defaults.profile.style, &before));
    assert(nav_commander_layout_for_config(120, 30, defaults.profile.style, &defaults, &after));
    assert(before.body_top == after.body_top && before.body_bottom == after.body_bottom);
    write_text(path, "[keys]\ncopy=\"F5\"\nmove=\"F5\"\n");
    assert(nav_config_load_file(&config, path, error, sizeof error) != 0);
    assert(strstr(error, path) && strstr(error, "conflict"));
    write_text(path, "[profile]\n[layout]\nshow_menu=\"no\"\n");
    assert(nav_config_load_file(&config, path, error, sizeof error) != 0 && strstr(error, "boolean"));
    write_text(path, "[profile\n");
    assert(nav_config_load_file(&config, path, error, sizeof error) != 0 && strstr(error, path));
    write_text(path, "[viewer]\nwrap=true\n");
    assert(nav_config_load_file(&config, path, error, sizeof error) == 0 && config.viewer_wrap);
    assert(config.show_menu && resolve(&config.keymap, "F5") == NAV_CMD_COPY);
    assert(unlink(path) == 0);
    assert(nav_config_load_file(&config, path, error, sizeof error) != 0);
    assert(strstr(error, path) && strstr(error, "No such file"));
    /* The normal config is layered, never replaced by an explicit sparse UI profile. */
    write_text(defaults.config_path, "[viewer]\nline_numbers=true\n");
    write_text(path, "[profile]\nname=\"Sparse\"\n");
    assert(nav_config_load_file(&config, path, error, sizeof error) == 0 && config.viewer_line_numbers);
    assert(config.profile.foreground[NAV_STYLE_TEXT] == defaults.profile.foreground[NAV_STYLE_TEXT]);
    assert(unlink(path) == 0);
}

int main(void)
{
    assert(nav_theme_classic_dos()->format == 2);
    assert(nav_theme_classic_dos()->style == NAV_UI_STYLE_CLASSIC);
    assert_resolved(nav_theme_classic_dos());
    test_profiles();
    test_legacy();
    test_v2();
    test_builtins();
    test_fallbacks_and_errors();
    return 0;
}
