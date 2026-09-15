#include "nav_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static NavAction press(NavInput *input, NavKeymap *map, NavInputContext context, const char *name)
{
    NavKeyStroke key;
    assert(nav_key_parse(name, &key) == 0);
    NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = key.key, .modifiers = key.modifiers};
    return nav_input_resolve(input, map, context, &event);
}
int main(void)
{
    NavKeymap map; NavInput input = {0}; char error[256], label[80]; NavKeyStroke key;
    nav_keymap_defaults(&map);
    assert(map.count > 100 && map.count < NAV_BINDING_MAX);
    const struct { const char *key; NavCommand command; } examples[] = {
        {"Ctrl+Q", NAV_CMD_QUIT}, {"Ctrl+\\", NAV_CMD_MENU},
        {"F3", NAV_CMD_VIEW}, {"F5", NAV_CMD_COPY}, {"F10", NAV_CMD_QUIT},
        {"Tab", NAV_CMD_PANEL_SWITCH}, {"Enter", NAV_CMD_OPEN}
    };
    for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
        assert(press(&input, &map, NAV_CONTEXT_PANEL, examples[i].key).command == examples[i].command);
    assert(press(&input, &map, NAV_CONTEXT_PANEL, "Ctrl+F").command == NAV_CMD_NONE && input.pending);
    assert(press(&input, &map, NAV_CONTEXT_PANEL, "C").command == NAV_CMD_COPY && !input.pending);
    press(&input, &map, NAV_CONTEXT_PANEL, "Ctrl+F");
    assert(press(&input, &map, NAV_CONTEXT_PANEL, "Escape").command == NAV_CMD_NONE && !input.pending);
    press(&input, &map, NAV_CONTEXT_PANEL, "Ctrl+F");
    assert(press(&input, &map, NAV_CONTEXT_PANEL, "Q").command == NAV_CMD_NONE && !input.pending);
    press(&input, &map, NAV_CONTEXT_PANEL, "Ctrl+F");
    assert(press(&input, &map, NAV_CONTEXT_DIALOG, "C").command == NAV_CMD_TEXT && !input.pending);
    assert(press(&input, &map, NAV_CONTEXT_DIALOG, "Ctrl+Q").command == NAV_CMD_CANCEL);
    assert(press(&input, &map, NAV_CONTEXT_DIALOG, "F5").command == NAV_CMD_NONE);
    assert(press(&input, &map, NAV_CONTEXT_MENU, "Escape").command == NAV_CMD_CANCEL);
    assert(press(&input, &map, NAV_CONTEXT_VIEWER, "Ctrl+Q").command == NAV_CMD_VIEWER_CLOSE);
    assert(press(&input, &map, NAV_CONTEXT_VIEWER, "Ctrl+S").command == NAV_CMD_FIND);
    assert(press(&input, &map, NAV_CONTEXT_VIEWER, "F5").command == NAV_CMD_FIND_NEXT);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_PANEL, "F5", "file.view", error, sizeof error) == 0);
    assert(press(&input, &map, NAV_CONTEXT_PANEL, "F5").command == NAV_CMD_VIEW);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_PANEL, "f5", "file.copy", error, sizeof error) != 0);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_PANEL, "F12", "bad.command", error, sizeof error) != 0);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_PANEL, "Ctrl+F", "file.copy", error, sizeof error) != 0);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_PANEL, "Ctrl+X C", "file.copy", error, sizeof error) == 0);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_PANEL, "Ctrl+X", "file.copy", error, sizeof error) != 0);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_GLOBAL, "F12", "help.open", error, sizeof error) == 0);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_PANEL, "F12", "none", error, sizeof error) == 0);
    assert(nav_keymap_bind(&map, NAV_CONTEXT_VIEWER, "F12", "viewer.close", error, sizeof error) == 0);
    assert(press(&input, &map, NAV_CONTEXT_PANEL, "F12").command == NAV_CMD_NONE);
    assert(press(&input, &map, NAV_CONTEXT_VIEWER, "F12").command == NAV_CMD_VIEWER_CLOSE);
    assert(nav_keymap_label(&map, NAV_CONTEXT_PANEL, NAV_CMD_COPY, label, sizeof label) > 0);
    assert(strcmp(label, "Ctrl+F C") == 0);
    assert(nav_key_parse("Ctrl+Ctrl+Q", &key) != 0);
    assert(nav_key_parse("Ctrl+", &key) != 0);
    assert(nav_key_parse("F13", &key) != 0);
    assert(nav_key_parse("Ctrl+Alt+Shift+F3", &key) == 0);
    assert(key.modifiers == (NAV_MOD_CTRL | NAV_MOD_ALT | NAV_MOD_SHIFT));
    assert(nav_keymap_bind(&map, NAV_CONTEXT_VIEWER, "Shift+W", "viewer.lines", error, sizeof error) == 0);
    assert(press(&input, &map, NAV_CONTEXT_VIEWER, "Shift+W").command == NAV_CMD_LINES);
    assert(press(&input, &map, NAV_CONTEXT_VIEWER, "W").command == NAV_CMD_WRAP);
    press(&input, &map, NAV_CONTEXT_PANEL, "Ctrl+F");
    NavTermEvent resize = {.type = NAV_TERM_EVENT_RESIZE, .width = 100, .height = 30};
    assert(nav_input_resolve(&input, &map, NAV_CONTEXT_PANEL, &resize).width == 100 && !input.pending);
    for (unsigned c = 0; c < NAV_CMD_COUNT; c++) assert(nav_command_parse(nav_command_name(c)) == c);
    puts("input/command/context/keymap tests passed");
    return 0;
}
