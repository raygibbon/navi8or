#include "nav_theme.h"
#include <assert.h>
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
        [NAV_STYLE_PANE_TITLE] = 0, [NAV_STYLE_PANE_TITLE_ACTIVE] = 14
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
        {"amber-crt", NAV_UI_STYLE_CLASSIC},
        {"carbon", NAV_UI_STYLE_MODERN}, {"cde", NAV_UI_STYLE_CLASSIC},
        {"classic-dos", NAV_UI_STYLE_CLASSIC},
        {"commander", NAV_UI_STYLE_CLASSIC},
        {"dos-vga", NAV_UI_STYLE_CLASSIC},
        {"monochrome", NAV_UI_STYLE_CLASSIC},
        {"navi8or-classic", NAV_UI_STYLE_CLASSIC},
        {"nordic", NAV_UI_STYLE_MODERN}, {"paper", NAV_UI_STYLE_MODERN},
        {"phosphor", NAV_UI_STYLE_CLASSIC}, {"slate", NAV_UI_STYLE_MODERN},
        {"solar-dark", NAV_UI_STYLE_MODERN},
        {"solar-light", NAV_UI_STYLE_MODERN},
        {"violet-night", NAV_UI_STYLE_MODERN},
        {"workbench", NAV_UI_STYLE_CLASSIC}
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

int main(void)
{
    assert(nav_theme_classic_dos()->format == 2);
    assert(nav_theme_classic_dos()->style == NAV_UI_STYLE_CLASSIC);
    assert_resolved(nav_theme_classic_dos());
    test_legacy();
    test_v2();
    test_builtins();
    test_fallbacks_and_errors();
    return 0;
}
