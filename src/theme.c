#include "nav_theme.h"
#include "nav.h"
#include "toml.h"
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef struct
{
    const char *path;
    NavStyle style;
} RoleSpec;

static const RoleSpec semantic_roles[] = {
    {"ui.background", NAV_STYLE_BACKGROUND},
    {"ui.surface", NAV_STYLE_SURFACE},
    {"ui.surface_alt", NAV_STYLE_SURFACE_ALT},
    {"ui.text", NAV_STYLE_TEXT},
    {"ui.text_dim", NAV_STYLE_TEXT_DIM},
    {"ui.accent", NAV_STYLE_ACCENT},
    {"ui.border", NAV_STYLE_BORDER},
    {"ui.border_active", NAV_STYLE_BORDER_ACTIVE},
    {"ui.selection", NAV_STYLE_SELECTION},
    {"ui.selection_inactive", NAV_STYLE_SELECTION_INACTIVE},
    {"ui.header", NAV_STYLE_HEADER},
    {"ui.status", NAV_STYLE_STATUS},
    {"ui.message", NAV_STYLE_MESSAGE},
    {"ui.error", NAV_STYLE_ERROR},
    {"ui.warning", NAV_STYLE_WARNING},
    {"ui.dialog", NAV_STYLE_DIALOG},
    {"ui.dialog_title", NAV_STYLE_DIALOG_TITLE},
    {"ui.menu", NAV_STYLE_MENU},
    {"ui.menu_selected", NAV_STYLE_MENU_SELECTED},
    {"ui.menu_disabled", NAV_STYLE_MENU_DISABLED},
    {"ui.menu_selected_disabled", NAV_STYLE_MENU_SELECTED_DISABLED},
    {"ui.keybar", NAV_STYLE_KEYBAR},
    {"ui.keybar_selected", NAV_STYLE_KEYBAR_SELECTED},
    {"ui.keybar_key", NAV_STYLE_KEYBAR_KEY},
    {"ui.keybar_disabled", NAV_STYLE_KEYBAR_DISABLED},
    {"ui.path", NAV_STYLE_PATH},
    {"ui.column_header", NAV_STYLE_COLUMN_HEADER},
    {"ui.viewer_line_number", NAV_STYLE_VIEWER_LINE_NUMBER},
    {"ui.viewer_search_match", NAV_STYLE_VIEWER_SEARCH_MATCH},
    {"ui.progress", NAV_STYLE_PROGRESS},
    {"ui.pane_title", NAV_STYLE_PANE_TITLE},
    {"ui.pane_title_active", NAV_STYLE_PANE_TITLE_ACTIVE},
    {"ui.file", NAV_STYLE_FILE}, {"ui.directory", NAV_STYLE_DIRECTORY}
};

/* The sole format-1 compatibility map. Rendering never consumes these names. */
static const RoleSpec legacy_roles[] = {
    {"tdx.head", NAV_STYLE_HEADER},
    {"tdx.mode", NAV_STYLE_STATUS},
    {"tdx.message", NAV_STYLE_MESSAGE},
    {"tdx.text", NAV_STYLE_TEXT},
    {"tdx.curl", NAV_STYLE_ACCENT},
    {"tdx.curl", NAV_STYLE_KEYBAR_KEY},
    {"tdx.help", NAV_STYLE_SURFACE_ALT},
    {"tdx.dialog", NAV_STYLE_DIALOG},
    {"tdx.edit_label", NAV_STYLE_DIALOG_TITLE},
    {"tdx.disabled", NAV_STYLE_TEXT_DIM},
    {"tdx.disabled", NAV_STYLE_KEYBAR_DISABLED},
    {"tdx.hilited_file", NAV_STYLE_SELECTION},
    {"tdx.menu_header", NAV_STYLE_KEYBAR},
    {"tdx.menu_selected", NAV_STYLE_KEYBAR_SELECTED},
    {"tdx.menu", NAV_STYLE_MENU},
    {"tdx.menu_disabled", NAV_STYLE_MENU_DISABLED},
    {"tdx.menu_item", NAV_STYLE_MENU_SELECTED},
    {"tdx.menu_item_bad", NAV_STYLE_MENU_SELECTED_DISABLED},
    {"viewer.line_number", NAV_STYLE_VIEWER_LINE_NUMBER},
    {"search.match", NAV_STYLE_VIEWER_SEARCH_MATCH},
    {"progress", NAV_STYLE_PROGRESS},
    {"navigator.pane_title", NAV_STYLE_PANE_TITLE},
    {"navigator.pane_title_active", NAV_STYLE_PANE_TITLE_ACTIVE}
};

const char *nav_theme_role_name(NavStyle style)
{
    for (size_t i = 0; i < sizeof semantic_roles / sizeof *semantic_roles; i++)
        if (semantic_roles[i].style == style) return semantic_roles[i].path;
    return NULL;
}

const char *nav_theme_colour_name(unsigned index)
{
    static const char *names[] = {"black", "blue", "green", "cyan", "red", "magenta", "brown", "light_gray", "dark_gray", "light_blue", "light_green", "light_cyan", "light_red", "light_magenta", "yellow", "white"};
    return index < 16 ? names[index] : "black";
}

static int colour(const char *name)
{
    for (int index = 0; index < 16; index++)
        if (!strcasecmp(name, nav_theme_colour_name((unsigned)index)))
            return index;
    return -1;
}

static void set_style(NavTheme *theme, NavStyle style, uint8_t foreground,
                      uint8_t background)
{
    theme->foreground[style] = foreground;
    theme->background[style] = background;
}

static const NavTheme *classic_theme(void)
{
    static NavTheme theme;
    static bool initialized;
    if (!initialized)
    {
        snprintf(theme.name, sizeof theme.name, "classic-dos");
        snprintf(theme.display_name, sizeof theme.display_name, "Classic TDX");
        theme.format = 2;
        theme.style = NAV_UI_STYLE_CLASSIC;
        /* TDX/TDE defaults translated into Navi8or's semantic role model. */
        set_style(&theme, NAV_STYLE_BACKGROUND, 7, 1);
        set_style(&theme, NAV_STYLE_SURFACE, 7, 1);
        set_style(&theme, NAV_STYLE_SURFACE_ALT, 0, 3);
        set_style(&theme, NAV_STYLE_TEXT, 7, 1);
        set_style(&theme, NAV_STYLE_TEXT_DIM, 8, 7);
        set_style(&theme, NAV_STYLE_ACCENT, 15, 1);
        set_style(&theme, NAV_STYLE_BORDER, 11, 1);
        set_style(&theme, NAV_STYLE_BORDER_ACTIVE, 14, 3);
        set_style(&theme, NAV_STYLE_SELECTION, 15, 5);
        set_style(&theme, NAV_STYLE_SELECTION_INACTIVE, 7, 1);
        set_style(&theme, NAV_STYLE_HEADER, 11, 4);
        set_style(&theme, NAV_STYLE_STATUS, 15, 6);
        set_style(&theme, NAV_STYLE_MESSAGE, 15, 4);
        set_style(&theme, NAV_STYLE_ERROR, 15, 4);
        set_style(&theme, NAV_STYLE_WARNING, 15, 6);
        set_style(&theme, NAV_STYLE_DIALOG, 0, 7);
        set_style(&theme, NAV_STYLE_DIALOG_TITLE, 9, 7);
        set_style(&theme, NAV_STYLE_MENU, 0, 7);
        set_style(&theme, NAV_STYLE_MENU_SELECTED, 15, 2);
        set_style(&theme, NAV_STYLE_MENU_DISABLED, 8, 7);
        set_style(&theme, NAV_STYLE_MENU_SELECTED_DISABLED, 8, 2);
        set_style(&theme, NAV_STYLE_KEYBAR, 0, 3);
        set_style(&theme, NAV_STYLE_KEYBAR_SELECTED, 14, 3);
        set_style(&theme, NAV_STYLE_KEYBAR_KEY, 15, 1);
        set_style(&theme, NAV_STYLE_KEYBAR_DISABLED, 8, 3);
        set_style(&theme, NAV_STYLE_PATH, 15, 1);
        set_style(&theme, NAV_STYLE_COLUMN_HEADER, 11, 1);
        set_style(&theme, NAV_STYLE_VIEWER_LINE_NUMBER, 11, 1);
        set_style(&theme, NAV_STYLE_VIEWER_SEARCH_MATCH, 0, 14);
        set_style(&theme, NAV_STYLE_PROGRESS, 0, 7);
        set_style(&theme, NAV_STYLE_PANE_TITLE, 0, 3);
        set_style(&theme, NAV_STYLE_PANE_TITLE_ACTIVE, 14, 3);
        set_style(&theme, NAV_STYLE_FILE, 7, 1);
        set_style(&theme, NAV_STYLE_DIRECTORY, 7, 1);
        theme.symbols = *nav_symbols_classic_dos();
        theme.frame_style = NAV_FRAME_COMBINE;
        theme.frame_space = true;
        theme.shadow = true;
        theme.shadow_width = 1;
        initialized = true;
    }
    return &theme;
}

static const NavTheme *modern_theme(void)
{
    static NavTheme theme;
    static bool initialized;
    if (!initialized)
    {
        snprintf(theme.name, sizeof theme.name, "solar-dark");
        snprintf(theme.display_name, sizeof theme.display_name, "Solar Dark");
        theme.format = 2;
        theme.style = NAV_UI_STYLE_MODERN;
        set_style(&theme, NAV_STYLE_BACKGROUND, 7, 0);
        set_style(&theme, NAV_STYLE_SURFACE, 7, 0);
        set_style(&theme, NAV_STYLE_SURFACE_ALT, 7, 8);
        set_style(&theme, NAV_STYLE_TEXT, 7, 0);
        set_style(&theme, NAV_STYLE_TEXT_DIM, 8, 0);
        set_style(&theme, NAV_STYLE_ACCENT, 3, 0);
        set_style(&theme, NAV_STYLE_BORDER, 8, 0);
        set_style(&theme, NAV_STYLE_BORDER_ACTIVE, 3, 0);
        set_style(&theme, NAV_STYLE_SELECTION, 0, 3);
        set_style(&theme, NAV_STYLE_SELECTION_INACTIVE, 7, 8);
        set_style(&theme, NAV_STYLE_HEADER, 11, 0);
        set_style(&theme, NAV_STYLE_STATUS, 8, 0);
        set_style(&theme, NAV_STYLE_MESSAGE, 11, 0);
        set_style(&theme, NAV_STYLE_ERROR, 15, 4);
        set_style(&theme, NAV_STYLE_WARNING, 14, 0);
        set_style(&theme, NAV_STYLE_DIALOG, 7, 8);
        set_style(&theme, NAV_STYLE_DIALOG_TITLE, 11, 8);
        set_style(&theme, NAV_STYLE_MENU, 7, 0);
        set_style(&theme, NAV_STYLE_MENU_SELECTED, 0, 3);
        set_style(&theme, NAV_STYLE_MENU_DISABLED, 8, 0);
        set_style(&theme, NAV_STYLE_MENU_SELECTED_DISABLED, 8, 3);
        set_style(&theme, NAV_STYLE_KEYBAR, 7, 0);
        set_style(&theme, NAV_STYLE_KEYBAR_SELECTED, 0, 3);
        set_style(&theme, NAV_STYLE_KEYBAR_KEY, 0, 3);
        set_style(&theme, NAV_STYLE_KEYBAR_DISABLED, 8, 0);
        set_style(&theme, NAV_STYLE_PATH, 8, 0);
        set_style(&theme, NAV_STYLE_COLUMN_HEADER, 8, 0);
        set_style(&theme, NAV_STYLE_VIEWER_LINE_NUMBER, 8, 0);
        set_style(&theme, NAV_STYLE_VIEWER_SEARCH_MATCH, 0, 14);
        set_style(&theme, NAV_STYLE_PROGRESS, 0, 3);
        set_style(&theme, NAV_STYLE_PANE_TITLE, 7, 0);
        set_style(&theme, NAV_STYLE_PANE_TITLE_ACTIVE, 3, 0);
        set_style(&theme, NAV_STYLE_FILE, 7, 0);
        set_style(&theme, NAV_STYLE_DIRECTORY, 7, 0);
        theme.symbols.directory = 0x25b8;
        theme.symbols.parent = 0x2191;
        theme.symbols.selected = 0x203a;
        theme.symbols.upload = 0x2191;
        theme.symbols.download = 0x2193;
        theme.frame_style = NAV_FRAME_SINGLE;
        theme.frame_space = false;
        theme.shadow = false;
        theme.shadow_width = 1;
        initialized = true;
    }
    return &theme;
}

const NavTheme *nav_theme_default(void) { return modern_theme(); }

const NavTheme *nav_theme_classic_dos(void)
{
    return classic_theme();
}

static const toml_table_t *table_for_path(const toml_table_t *root,
                                          const char *path)
{
    char buffer[64];
    if (strlen(path) >= sizeof buffer)
        return NULL;
    snprintf(buffer, sizeof buffer, "%s", path);
    const toml_table_t *table = root;
    for (char *part = strtok(buffer, "."); part; part = strtok(NULL, "."))
    {
        table = toml_table_in(table, part);
        if (!table)
            return NULL;
    }
    return table;
}

static int apply_colour(const toml_table_t *table, const char *key,
                        uint8_t *slot, const char *path, char *error,
                        size_t error_size)
{
    toml_datum_t datum;
    int value;
    if (!toml_key_exists(table, key))
        return 0;
    datum = toml_string_in(table, key);
    if (!datum.ok)
    {
        snprintf(error, error_size, "%s.%s must be a colour name", path, key);
        return -1;
    }
    value = colour(datum.u.s);
    if (value < 0)
        snprintf(error, error_size, "%s.%s has unknown colour '%s'", path,
                 key, datum.u.s);
    free(datum.u.s);
    if (value < 0)
        return -1;
    *slot = (uint8_t)value;
    return 0;
}

static int apply_style(NavTheme *theme, const toml_table_t *root,
                       const RoleSpec *role, bool *provided, char *error,
                       size_t error_size)
{
    const toml_table_t *table = table_for_path(root, role->path);
    if (!table)
        return 0;
    if (apply_colour(table, "foreground", &theme->foreground[role->style],
                     role->path, error, error_size) ||
        apply_colour(table, "background", &theme->background[role->style],
                     role->path, error, error_size))
        return -1;
    provided[role->style] = true;
    return 0;
}

static void inherit_role(NavTheme *theme, bool *provided, NavStyle target,
                         NavStyle source)
{
    if (!provided[target])
    {
        theme->foreground[target] = theme->foreground[source];
        theme->background[target] = theme->background[source];
        provided[target] = true;
    }
}

static void apply_v2_fallbacks(NavTheme *theme, bool *provided)
{
    if (!provided[NAV_STYLE_TEXT] && provided[NAV_STYLE_BACKGROUND])
        inherit_role(theme, provided, NAV_STYLE_TEXT, NAV_STYLE_BACKGROUND);
    inherit_role(theme, provided, NAV_STYLE_FILE, NAV_STYLE_TEXT);
    inherit_role(theme, provided, NAV_STYLE_DIRECTORY, NAV_STYLE_TEXT);
    inherit_role(theme, provided, NAV_STYLE_BACKGROUND, NAV_STYLE_TEXT);
    inherit_role(theme, provided, NAV_STYLE_SURFACE, NAV_STYLE_BACKGROUND);
    inherit_role(theme, provided, NAV_STYLE_SURFACE_ALT, NAV_STYLE_SURFACE);
    inherit_role(theme, provided, NAV_STYLE_TEXT_DIM, NAV_STYLE_TEXT);
    inherit_role(theme, provided, NAV_STYLE_ACCENT, NAV_STYLE_TEXT);
    inherit_role(theme, provided, NAV_STYLE_SELECTION, NAV_STYLE_ACCENT);
    inherit_role(theme, provided, NAV_STYLE_SELECTION_INACTIVE, NAV_STYLE_TEXT);
    inherit_role(theme, provided, NAV_STYLE_BORDER, NAV_STYLE_SURFACE_ALT);
    inherit_role(theme, provided, NAV_STYLE_BORDER_ACTIVE, NAV_STYLE_ACCENT);
    inherit_role(theme, provided, NAV_STYLE_STATUS, NAV_STYLE_SURFACE);
    inherit_role(theme, provided, NAV_STYLE_MESSAGE, NAV_STYLE_STATUS);
    inherit_role(theme, provided, NAV_STYLE_ERROR, NAV_STYLE_MESSAGE);
    inherit_role(theme, provided, NAV_STYLE_WARNING, NAV_STYLE_MESSAGE);
    inherit_role(theme, provided, NAV_STYLE_DIALOG, NAV_STYLE_SURFACE_ALT);
    inherit_role(theme, provided, NAV_STYLE_DIALOG_TITLE, NAV_STYLE_ACCENT);
    inherit_role(theme, provided, NAV_STYLE_MENU, NAV_STYLE_SURFACE);
    inherit_role(theme, provided, NAV_STYLE_MENU_SELECTED, NAV_STYLE_SELECTION);
    inherit_role(theme, provided, NAV_STYLE_MENU_DISABLED, NAV_STYLE_TEXT_DIM);
    inherit_role(theme, provided, NAV_STYLE_MENU_SELECTED_DISABLED,
                 NAV_STYLE_MENU_DISABLED);
    inherit_role(theme, provided, NAV_STYLE_KEYBAR, NAV_STYLE_STATUS);
    inherit_role(theme, provided, NAV_STYLE_KEYBAR_SELECTED, NAV_STYLE_ACCENT);
    inherit_role(theme, provided, NAV_STYLE_KEYBAR_KEY,
                 NAV_STYLE_KEYBAR_SELECTED);
    inherit_role(theme, provided, NAV_STYLE_KEYBAR_DISABLED,
                 NAV_STYLE_TEXT_DIM);
    inherit_role(theme, provided, NAV_STYLE_PATH, NAV_STYLE_ACCENT);
    inherit_role(theme, provided, NAV_STYLE_COLUMN_HEADER, NAV_STYLE_TEXT_DIM);
    inherit_role(theme, provided, NAV_STYLE_HEADER, NAV_STYLE_KEYBAR);
    inherit_role(theme, provided, NAV_STYLE_VIEWER_LINE_NUMBER,
                 NAV_STYLE_TEXT_DIM);
    inherit_role(theme, provided, NAV_STYLE_VIEWER_SEARCH_MATCH,
                 NAV_STYLE_SELECTION);
    inherit_role(theme, provided, NAV_STYLE_PROGRESS, NAV_STYLE_ACCENT);
    inherit_role(theme, provided, NAV_STYLE_PANE_TITLE, NAV_STYLE_KEYBAR);
    inherit_role(theme, provided, NAV_STYLE_PANE_TITLE_ACTIVE,
                 NAV_STYLE_KEYBAR_SELECTED);
}

static int apply_frame(NavTheme *theme, const toml_table_t *root,
                       const char *path, char *error, size_t error_size)
{
    const toml_table_t *table = table_for_path(root, path);
    toml_datum_t datum;
    if (!table)
        return 0;
    const char *style_key = !strcmp(path, "layout") ? "border_style" : "style";
    if (toml_key_exists(table, style_key))
    {
        static const char *names[] = {
            "ascii", "single", "double", "combine", "combine_reverse", "block"
        };
        bool found = false;
        datum = toml_string_in(table, style_key);
        if (!datum.ok)
        {
            snprintf(error, error_size, "%s.style must be a string", path);
            return -1;
        }
        for (int index = 0; index < 6; index++)
            if (!strcasecmp(datum.u.s, names[index]))
            {
                theme->frame_style = (NavFrameStyle)index;
                found = true;
            }
        if (!found)
            snprintf(error, error_size, "%s.style has unknown value '%s'",
                     path, datum.u.s);
        free(datum.u.s);
        if (!found)
            return -1;
    }
    if (toml_key_exists(table, "space"))
    {
        datum = toml_bool_in(table, "space");
        if (!datum.ok)
        {
            snprintf(error, error_size, "%s.space must be boolean", path);
            return -1;
        }
        theme->frame_space = datum.u.b != 0;
    }
    if (toml_key_exists(table, "shadow"))
    {
        datum = toml_bool_in(table, "shadow");
        if (!datum.ok)
        {
            snprintf(error, error_size, "%s.shadow must be boolean", path);
            return -1;
        }
        theme->shadow = datum.u.b != 0;
    }
    return 0;
}

static int apply_symbol(const toml_table_t *table, const char *key,
                        uint32_t *slot, char *error, size_t error_size)
{
    toml_datum_t datum;
    int64_t codepoint = -1;
    int consumed;
    bool valid;
    if (!toml_key_exists(table, key))
        return 0;
    datum = toml_string_in(table, key);
    if (!datum.ok)
    {
        snprintf(error, error_size, "symbols.%s must be a string", key);
        return -1;
    }
    consumed = toml_utf8_to_ucs(datum.u.s, (int)strlen(datum.u.s), &codepoint);
    valid = consumed > 0 && (size_t)consumed == strlen(datum.u.s) &&
            codepoint >= 0 && codepoint <= 0x10ffff;
    if (!valid)
        snprintf(error, error_size, "symbols.%s must be one character", key);
    free(datum.u.s);
    if (!valid)
        return -1;
    *slot = (uint32_t)codepoint;
    return 0;
}

static int apply_style_profile(NavTheme *theme, const toml_table_t *root,
                               char *error, size_t error_size)
{
    const toml_table_t *ui = toml_table_in(root, "ui");
    toml_datum_t datum;
    if (!ui || !toml_key_exists(ui, "style"))
        return 0;
    datum = toml_string_in(ui, "style");
    if (!datum.ok)
    {
        snprintf(error, error_size, "ui.style must be 'modern' or 'classic'");
        return -1;
    }
    if (!strcasecmp(datum.u.s, "modern"))
        theme->style = NAV_UI_STYLE_MODERN;
    else if (!strcasecmp(datum.u.s, "classic"))
        theme->style = NAV_UI_STYLE_CLASSIC;
    else
    {
        snprintf(error, error_size, "ui.style has unknown value '%s'",
                 datum.u.s);
        free(datum.u.s);
        return -1;
    }
    free(datum.u.s);
    return 0;
}

static int parse_theme(FILE *file, const char *id, const NavTheme *base, NavThemeResult *result)
{
    char parse_error[256] = {0};
    bool provided[NAV_STYLE_COUNT] = {false};
    const toml_table_t *theme_info;
    const RoleSpec *roles;
    size_t role_count;
    int format_number = base ? 2 : 1;
    toml_table_t *root = toml_parse_file(file, parse_error, sizeof parse_error);
    if (!root)
    {
        snprintf(result->error, sizeof result->error, "theme parse error: %s",
                 parse_error);
        return -1;
    }
    if (toml_table_in(root, "tdx")) format_number = 1;
    theme_info = toml_table_in(root, "profile");
    if (!theme_info) theme_info = toml_table_in(root, "theme");
    if (theme_info && toml_key_exists(theme_info, "format"))
    {
        toml_datum_t format = toml_int_in(theme_info, "format");
        if (!format.ok || (format.u.i != 1 && format.u.i != 2))
        {
            snprintf(result->error, sizeof result->error,
                     "theme.format must be 1 or 2");
            toml_free(root);
            return -1;
        }
        format_number = (int)format.u.i;
    }
    result->theme = base ? *base : format_number == 1 ? *classic_theme() : *modern_theme();
    result->theme.format = format_number;
    if (format_number == 1) result->theme.style = NAV_UI_STYLE_CLASSIC;
    snprintf(result->theme.name, sizeof result->theme.name, "%s", id);
    if (theme_info && toml_key_exists(theme_info, "name") &&
        (!base || toml_table_in(root, "profile") || toml_key_exists(theme_info, "format")))
    {
        toml_datum_t name = toml_string_in(theme_info, "name");
        if (!name.ok)
        {
            snprintf(result->error, sizeof result->error,
                     "theme.name must be a string");
            toml_free(root);
            return -1;
        }
        snprintf(result->theme.display_name, sizeof result->theme.display_name,
                 "%s", name.u.s);
        free(name.u.s);
    }
    if (result->theme.format == 1)
    {
        roles = legacy_roles;
        role_count = sizeof legacy_roles / sizeof *legacy_roles;
        snprintf(result->warning, sizeof result->warning,
                 "legacy theme format 1; format 2 is recommended");
    }
    else
    {
        roles = semantic_roles;
        role_count = sizeof semantic_roles / sizeof *semantic_roles;
        if (apply_style_profile(&result->theme, root, result->error,
                                sizeof result->error))
        {
            toml_free(root);
            return -1;
        }
    }
    for (size_t index = 0; index < role_count; index++)
        if (apply_style(&result->theme, root, &roles[index], provided,
                        result->error, sizeof result->error))
        {
            toml_free(root);
            return -1;
        }
    /* Palette-only legacy loaders retain their established role inheritance.
       Profiles overlay a resolved theme, preserving every omitted role. */
    if (!base) apply_v2_fallbacks(&result->theme, provided);
    for (size_t index = 0; index < sizeof semantic_roles / sizeof *semantic_roles; index++) {
        char path[64];
        snprintf(path, sizeof path, "colors.%s", semantic_roles[index].path + 3);
        RoleSpec role = {path, semantic_roles[index].style};
        if (apply_style(&result->theme, root, &role, provided, result->error, sizeof result->error)) {
            toml_free(root); return -1;
        }
    }

    if (apply_frame(&result->theme, root,
                    result->theme.format == 1 ? "tdx.frame" : "ui.frame",
                    result->error, sizeof result->error))
    {
        toml_free(root);
        return -1;
    }
    const toml_table_t *colors = toml_table_in(root, "colors");
    if (!colors && toml_key_exists(root, "colors")) { snprintf(result->error, sizeof result->error, "colors must be a table"); toml_free(root); return -1; }
    if (colors) {
        static const struct { const char *key; NavStyle role; bool background; } aliases[] = {
            {"foreground", NAV_STYLE_TEXT, false}, {"background", NAV_STYLE_TEXT, true},
            {"file", NAV_STYLE_FILE, false}, {"directory", NAV_STYLE_DIRECTORY, false},
            {"border", NAV_STYLE_BORDER, false},
            {"selected_fg", NAV_STYLE_SELECTION, false}, {"selected_bg", NAV_STYLE_SELECTION, true},
            {"menu_fg", NAV_STYLE_MENU, false}, {"menu_bg", NAV_STYLE_MENU, true},
            {"menu_selected_fg", NAV_STYLE_MENU_SELECTED, false}, {"menu_selected_bg", NAV_STYLE_MENU_SELECTED, true},
            {"status_fg", NAV_STYLE_STATUS, false}, {"status_bg", NAV_STYLE_STATUS, true},
            {"function_key_fg", NAV_STYLE_KEYBAR, false}, {"function_key_bg", NAV_STYLE_KEYBAR, true}
        };
        for (int i = 0; ; i++) {
            const char *key = toml_key_in(colors, i); if (!key) break;
            if (toml_table_in(colors, key)) {
                bool known = false;
                for (size_t j = 0; j < sizeof semantic_roles / sizeof *semantic_roles; j++)
                    if (!strcmp(key, semantic_roles[j].path + 3)) known = true;
                if (!known) { snprintf(result->error, sizeof result->error, "unknown colors role: %s", key); toml_free(root); return -1; }
                const toml_table_t *role_table = toml_table_in(colors, key);
                for (int k = 0; ; k++) {
                    const char *field = toml_key_in(role_table, k); if (!field) break;
                    if (strcmp(field, "foreground") && strcmp(field, "background")) {
                        snprintf(result->error, sizeof result->error, "unsupported colors.%s.%s", key, field); toml_free(root); return -1;
                    }
                }
                continue;
            }
            bool known = false;
            for (size_t j = 0; j < sizeof aliases / sizeof *aliases; j++) if (!strcmp(key, aliases[j].key)) {
                NavStyle role = aliases[j].role;
                uint8_t *slot = aliases[j].background ? &result->theme.background[role] : &result->theme.foreground[role];
                if (apply_colour(colors, key, slot, "colors", result->error, sizeof result->error)) { toml_free(root); return -1; }
                provided[role] = true; known = true;
                if (role == NAV_STYLE_TEXT) {
                    uint8_t *background = aliases[j].background ? result->theme.background : result->theme.foreground;
                    background[NAV_STYLE_BACKGROUND] = background[NAV_STYLE_SURFACE] = *slot;
                }
            }
            if (!known) { snprintf(result->error, sizeof result->error, "unknown colors key: %s", key); toml_free(root); return -1; }
        }
    }
    if (base && provided[NAV_STYLE_TEXT]) {
        inherit_role(&result->theme, provided, NAV_STYLE_FILE, NAV_STYLE_TEXT);
        inherit_role(&result->theme, provided, NAV_STYLE_DIRECTORY, NAV_STYLE_TEXT);
    }
    if (apply_frame(&result->theme, root, "layout", result->error, sizeof result->error)) {
        toml_free(root); return -1;
    }
    {
        const toml_table_t *symbols = toml_table_in(root, "symbols");
        if (symbols &&
            (apply_symbol(symbols, "selected", &result->theme.symbols.selected,
                          result->error, sizeof result->error) ||
             apply_symbol(symbols, "directory", &result->theme.symbols.directory,
                          result->error, sizeof result->error) ||
             apply_symbol(symbols, "parent", &result->theme.symbols.parent,
                          result->error, sizeof result->error) ||
             apply_symbol(symbols, "upload", &result->theme.symbols.upload,
                          result->error, sizeof result->error) ||
             apply_symbol(symbols, "download", &result->theme.symbols.download,
                          result->error, sizeof result->error)))
        {
            toml_free(root);
            return -1;
        }
    }
    toml_free(root);
    result->loaded_from_file = true;
    return 0;
}

int nav_theme_load_file(const char *path, const char *id, NavThemeResult *result)
{
    FILE *file;
    if (!result)
        return -1;
    memset(result, 0, sizeof *result);
    result->theme = *modern_theme();
    result->fallback = true;
    snprintf(result->path, sizeof result->path, "%s", path ? path : "");
    if (!path || !id)
    {
        snprintf(result->error, sizeof result->error,
                 "theme path or identifier is unavailable");
        return -1;
    }
    file = nav_platform_fopen(path, "r");
    if (!file)
    {
        snprintf(result->error, sizeof result->error, "cannot read %s: %s", path, strerror(errno));
        return -1;
    }
    if (parse_theme(file, id, NULL, result))
    {
        fclose(file);
        result->theme = *modern_theme();
        result->fallback = true;
        return -1;
    }
    fclose(file);
    result->fallback = false;
    return 0;
}

int nav_theme_overlay_file(const char *path, const NavTheme *base, NavThemeResult *result)
{
    memset(result, 0, sizeof *result);
    snprintf(result->path, sizeof result->path, "%s", path);
    FILE *file = nav_platform_fopen(path, "r");
    if (!file) {
        snprintf(result->error, sizeof result->error, "cannot read %s: %s", path, strerror(errno));
        return -1;
    }
    int status = parse_theme(file, base->name, base, result);
    fclose(file);
    return status;
}

int nav_theme_load_result(const char *name, NavThemeResult *result)
{
    char directory[NAV_PATH_MAX], path[NAV_PATH_MAX];
    if (!result)
        return -1;
    if (!name || nav_platform_config_dir(directory, sizeof directory) ||
        snprintf(path, sizeof path, "%s/themes/%s.toml", directory, name) >=
            (int)sizeof path)
    {
        memset(result, 0, sizeof *result);
        result->theme = *modern_theme();
        result->fallback = true;
        snprintf(result->path, sizeof result->path, "compiled fallback");
        snprintf(result->error, sizeof result->error,
                 "theme path is unavailable");
        return -1;
    }
    if (nav_platform_access(path, F_OK) != 0 && !strchr(name, '/') && !strchr(name, '\\')) {
        snprintf(path, sizeof path, "themes/%s.toml", name);
    }
    return nav_theme_load_file(path, name, result);
}

const NavTheme *nav_theme_load(const char *name)
{
    static NavTheme loaded;
    NavThemeResult result;
    if (nav_theme_load_result(name, &result))
        return modern_theme();
    loaded = result.theme;
    return &loaded;
}
