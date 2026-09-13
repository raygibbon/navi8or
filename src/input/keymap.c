#include "nav_input.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool equal(NavKeyStroke a, NavKeyStroke b)
{ return a.key == b.key && a.modifiers == b.modifiers; }
int nav_keymap_bind(NavKeymap *map, NavInputContext context, const char *sequence,
                   const char *name, char *error, size_t size)
{
    NavBinding binding = {.context = context, .configured = true};
    char text[96];
    binding.command = nav_command_parse(name);
    if (context >= NAV_CONTEXT_COUNT || binding.command >= NAV_CMD_COUNT ||
        strlen(sequence) >= sizeof text) goto invalid;
    snprintf(text, sizeof text, "%s", sequence);
    char *second = strchr(text, ' ');
    if (second) { *second++ = 0; if (strchr(second, ' ')) goto invalid; }
    binding.length = second ? 2 : 1;
    if (nav_key_parse(text, &binding.keys[0]) ||
        (second && nav_key_parse(second, &binding.keys[1]))) goto invalid;
    for (size_t i = 0; i < map->count; i++) {
        NavBinding *old = &map->bindings[i];
        if (old->context != context || !equal(old->keys[0], binding.keys[0])) continue;
        if (old->length != binding.length) goto conflict;
        if (binding.length == 2 && !equal(old->keys[1], binding.keys[1])) continue;
        if (old->configured) goto conflict;
        *old = binding;
        return 0;
    }
    if (map->count == NAV_BINDING_MAX) {
        snprintf(error, size, "too many key bindings"); return -1;
    }
    map->bindings[map->count++] = binding;
    return 0;
conflict:
    snprintf(error, size, "duplicate or prefix-conflicting binding: %s", sequence);
    return -1;
invalid:
    snprintf(error, size, "invalid binding: %s = %s", sequence, name);
    return -1;
}
static void add(NavKeymap *map, NavInputContext context, const char *key, NavCommand command)
{
    char ignored[128];
    if (!nav_keymap_bind(map, context, key, nav_command_name(command), ignored, sizeof ignored))
        map->bindings[map->count - 1].configured = false;
}
void nav_keymap_defaults(NavKeymap *map)
{
    memset(map, 0, sizeof *map);
#define B(context, key, cmd) add(map, NAV_CONTEXT_##context, key, NAV_CMD_##cmd)
    B(GLOBAL, "Ctrl+Q", QUIT); B(GLOBAL, "F10", QUIT);
    B(GLOBAL, "F1", HELP); B(GLOBAL, "Ctrl+H", HELP);
    B(GLOBAL, "Ctrl+\\", MENU); B(GLOBAL, "F2", MENU);
    B(PANEL, "F3", VIEW); B(PANEL, "F4", EDIT); B(PANEL, "F5", COPY);
    B(PANEL, "F6", MOVE); B(PANEL, "F7", MKDIR); B(PANEL, "F8", DELETE);
    B(PANEL, "Tab", PANEL_SWITCH); B(PANEL, "Enter", OPEN);
    B(PANEL, "Backspace", PANEL_PARENT); B(PANEL, "Ctrl+PgUp", PANEL_PARENT);
    B(PANEL, "Ctrl+PgDn", OPEN); B(PANEL, "Alt+Up", PANEL_PARENT);
    B(PANEL, "Alt+Left", HISTORY_BACK); B(PANEL, "Alt+Right", HISTORY_FORWARD);
    B(PANEL, "Ctrl+U", PANEL_SWAP); B(PANEL, "/", FILTER);
    B(PANEL, "Ctrl+F C", COPY); B(PANEL, "Ctrl+F M", MOVE);
    B(PANEL, "Ctrl+F R", RENAME); B(PANEL, "Ctrl+F D", DELETE);
    B(PANEL, "Ctrl+F K", MKDIR); B(PANEL, "Ctrl+F O", OPEN);
    B(PANEL, "Ctrl+N P", PANEL_PARENT); B(PANEL, "Ctrl+N O", OPEN_LOCATION);
    B(PANEL, "Ctrl+N R", REFRESH); B(PANEL, "Ctrl+N B", HISTORY_BACK);
    B(PANEL, "Ctrl+N F", HISTORY_FORWARD);
    B(PANEL, "Ctrl+V V", VIEW); B(PANEL, "Ctrl+S F", FILTER);
    B(PANEL, "Ctrl+R O", REPOSITORY_OPEN); B(PANEL, "Ctrl+R R", REFRESH);
    B(PANEL, "Ctrl+R V", VAULT); B(PANEL, "Ctrl+P S", PANEL_SWITCH);
    B(PANEL, "Ctrl+P W", PANEL_SWAP); B(PANEL, "Ctrl+P B", PANEL_BRIEF);
    B(PANEL, "Ctrl+P F", PANEL_FULL); B(PANEL, "Ctrl+T C", OPEN_CONFIG);
    B(PANEL, "Ctrl+T R", RELOAD_CONFIG); B(PANEL, "Ctrl+T T", THEME_INFO);
    for (NavInputContext c = NAV_CONTEXT_PANEL; c <= NAV_CONTEXT_VAULT; c++) {
        if (c == NAV_CONTEXT_CONFIRM) continue;
        add(map, c, "Up", NAV_CMD_UP); add(map, c, "Down", NAV_CMD_DOWN);
        add(map, c, "Left", NAV_CMD_LEFT); add(map, c, "Right", NAV_CMD_RIGHT);
        add(map, c, "Home", NAV_CMD_HOME); add(map, c, "End", NAV_CMD_END);
        add(map, c, "PgUp", NAV_CMD_PAGE_UP); add(map, c, "PgDn", NAV_CMD_PAGE_DOWN);
        if (c >= NAV_CONTEXT_MENU) {
            add(map, c, "Escape", NAV_CMD_CANCEL); add(map, c, "Ctrl+Q", NAV_CMD_CANCEL);
            if (c == NAV_CONTEXT_PICKER || c == NAV_CONTEXT_VAULT) add(map, c, "F10", NAV_CMD_CANCEL);
            add(map, c, "Enter", NAV_CMD_ACCEPT);
        }
    }
    B(MENU, "Ctrl+Left", LEFT); B(MENU, "Ctrl+Right", RIGHT);
    B(DIALOG, "Backspace", BACKSPACE); B(DIALOG, "Delete", TEXT_DELETE);
    B(CONFIRM, "Y", ACCEPT); B(CONFIRM, "Enter", ACCEPT);
    B(CONFIRM, "N", CANCEL); B(CONFIRM, "Escape", CANCEL);
    B(CONFIRM, "Ctrl+Q", CANCEL);
    B(VIEWER, "Escape", VIEWER_CLOSE); B(VIEWER, "Ctrl+Q", VIEWER_CLOSE);
    B(VIEWER, "F10", VIEWER_CLOSE); B(VIEWER, "Ctrl+S", FIND); B(VIEWER, "/", FIND);
    B(VIEWER, "F5", FIND_NEXT); B(VIEWER, "F6", FIND_PREVIOUS);
    B(VIEWER, "G", GOTO); B(VIEWER, "W", WRAP); B(VIEWER, "L", LINES);
    B(VIEWER, "Ctrl+Home", HOME); B(VIEWER, "Ctrl+End", END);
    B(VIEWER, "Ctrl+Left", LEFT_FAST); B(VIEWER, "Ctrl+Right", RIGHT_FAST);
    B(VAULT, "F7", VAULT_NEW); B(VAULT, "F4", VAULT_EDIT);
    B(VAULT, "F8", VAULT_DELETE); B(VAULT, "U", VAULT_UNLOCK); B(VAULT, "L", VAULT_LOCK);
#undef B
}
NavAction nav_input_resolve(NavInput *input, const NavKeymap *map,
                            NavInputContext context, const NavTermEvent *event)
{
    NavAction action = {.type = event->type, .width = event->width, .height = event->height};
    if (input->context != context || event->type == NAV_TERM_EVENT_RESIZE) input->pending = false;
    input->context = context;
    if (event->type != NAV_TERM_EVENT_KEY) return action;
    NavKeyStroke key = {event->key, event->modifiers};
    if (key.key >= 'A' && key.key <= 'Z') key.key = tolower(key.key);
    if (input->pending && event->key == NAV_KEY_ESCAPE) { input->pending = false; return action; }
    bool pending = input->pending;
    input->pending = false;
    NavInputContext scopes[] = {context, NAV_CONTEXT_GLOBAL};
    /* Modals capture all unmatched keys; they never activate the underlying view. */
    unsigned scope_count = context >= NAV_CONTEXT_MENU && context <= NAV_CONTEXT_VAULT ? 1 : 2;
    bool shifted_text = key.key >= 32 && key.key < 127 && (key.modifiers & NAV_MOD_SHIFT);
    for (unsigned scope = 0; scope < scope_count; scope++) {
        for (unsigned variant = 0; variant < (shifted_text ? 2u : 1u); variant++) {
            NavKeyStroke lookup = key;
            if (variant) lookup.modifiers &= ~NAV_MOD_SHIFT;
            for (size_t i = 0; i < map->count; i++) {
                const NavBinding *b = &map->bindings[i];
                if (b->context != scopes[scope]) continue;
                if (pending) {
                    if (b->length == 2 && equal(b->keys[0], input->prefix) && equal(b->keys[1], lookup)) {
                        action.command = b->command; return action;
                    }
                } else if (equal(b->keys[0], lookup)) {
                    if (b->length == 2) { input->pending = true; input->prefix = lookup; }
                    else action.command = b->command;
                    return action;
                }
            }
        }
    }
    if (!pending && !(event->modifiers & (NAV_MOD_CTRL | NAV_MOD_ALT)) &&
        event->key >= 32 && event->key <= 0x10ffff) {
        action.command = NAV_CMD_TEXT; action.text = (uint32_t)event->key;
    }
    return action;
}
int nav_keymap_label(const NavKeymap *map, NavInputContext context, NavCommand command,
                     char *buffer, size_t size)
{
    if (!buffer || !size) return 0;
    buffer[0] = 0;
    /* Prefer single-key bindings; verify visibility through the context resolver. */
    for (unsigned length = 1; length <= 2; length++) for (size_t i = 0; i < map->count; i++) {
        const NavBinding *b = &map->bindings[i];
        if (b->length != length || b->command != command ||
            (b->context != context && b->context != NAV_CONTEXT_GLOBAL)) continue;
        NavInput input = {0}; NavAction action = {0};
        for (unsigned k = 0; k < b->length; k++) {
            NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = b->keys[k].key, .modifiers = b->keys[k].modifiers};
            action = nav_input_resolve(&input, map, context, &event);
        }
        if (action.command != command) continue;
        char first[32], second[32];
        nav_key_format(b->keys[0], first, sizeof first);
        nav_key_format(b->keys[1], second, sizeof second);
        return snprintf(buffer, size, "%s%s%s", first, length == 2 ? " " : "", length == 2 ? second : "");
    }
    return 0;
}
