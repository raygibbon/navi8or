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
    NavCommand conflict_command = NAV_CMD_NONE;
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
        conflict_command = old->command;
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
    snprintf(error, size, "duplicate or prefix-conflicting binding %s: %s vs %s", sequence,
             nav_command_description(conflict_command), nav_command_description(binding.command));
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
    B(GLOBAL, "Ctrl+T P", PREFERENCES);
    B(GLOBAL, "Ctrl+\\", MENU); B(GLOBAL, "F2", MENU);
    B(PANEL, "F3", VIEW); B(PANEL, "F4", EDIT); B(PANEL, "F5", COPY);
    B(PANEL, "F6", MOVE); B(PANEL, "F7", MKDIR); B(PANEL, "F8", DELETE);
    B(PANEL, "Ctrl+L", OPEN_LOCATION);
    B(GLOBAL, "Tab", PANEL_SWITCH); B(PANEL, "Enter", OPEN);
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
    B(PANEL, "Ctrl+R V", VAULT); B(GLOBAL, "Ctrl+P S", PANEL_SWITCH);
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
    B(DIALOG, "Tab", DOWN);
    B(DIALOG, "Ctrl+V", TEXT_PASTE); B(DIALOG, "Shift+Insert", TEXT_PASTE);
    B(DIALOG, "Ctrl+C", TEXT_COPY); B(DIALOG, "Ctrl+X", TEXT_CUT);
    B(DIALOG, "Ctrl+A", TEXT_SELECT_ALL);
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
    B(PREFERENCES, "Up", UP); B(PREFERENCES, "Down", DOWN);
    B(PREFERENCES, "Left", LEFT); B(PREFERENCES, "Right", RIGHT);
    B(PREFERENCES, "Home", HOME); B(PREFERENCES, "End", END);
    B(PREFERENCES, "PageUp", PAGE_UP); B(PREFERENCES, "PageDown", PAGE_DOWN);
    B(PREFERENCES, "Enter", ACCEPT); B(PREFERENCES, "Escape", CANCEL);
    B(PREFERENCES, "Ctrl+Q", CANCEL); B(PREFERENCES, "F10", CANCEL);
    B(PREFERENCES, "Delete", TEXT_DELETE); B(PREFERENCES, "F2", PROFILE_SAVE);
    B(PREFERENCES, "F3", PROFILE_SAVE_AS); B(PREFERENCES, "F4", PROFILE_APPLY);
    B(PREFERENCES, "F5", KEY_APPEND); B(PREFERENCES, "F6", KEY_SEQUENCE);
    B(PREFERENCES, "F1", HELP);
#undef B
}
NavAction nav_input_resolve(NavInput *input, const NavKeymap *map,
                            NavInputContext context, const NavTermEvent *event)
{
    NavAction action = {.type = event->type, .width = event->width, .height = event->height};
    if (event->type == NAV_TERM_EVENT_PASTE_START) { input->pasting = true; input->pending = false; return action; }
    if (event->type == NAV_TERM_EVENT_PASTE_END) { input->pasting = false; return action; }
    if (input->pasting && event->type == NAV_TERM_EVENT_KEY) {
        if (context == NAV_CONTEXT_DIALOG || context == NAV_CONTEXT_EDITOR) {
            action.command = NAV_CMD_TEXT;
            action.text = event->modifiers & NAV_MOD_CTRL ? 0 : (uint32_t)event->key;
        }
        return action;
    }
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
    unsigned scope_count = ((context >= NAV_CONTEXT_MENU && context <= NAV_CONTEXT_VAULT) || context == NAV_CONTEXT_PREFERENCES) ? 1 : 2;
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

static bool binding_scopes_overlap(NavInputContext a, NavInputContext b)
{
    /* Context lookup is deterministic (the active context wins, then global),
     * so equal physical keys in separate contexts are not configuration
     * conflicts. Only bindings competing inside one declared context collide. */
    return a == b;
}
bool nav_binding_overlaps(const NavBinding *a, const NavBinding *b)
{
    if (!binding_scopes_overlap(a->context, b->context) ||
        a->keys[0].key != b->keys[0].key || a->keys[0].modifiers != b->keys[0].modifiers) return false;
    return a->length == 1 || b->length == 1 ||
           (a->keys[1].key == b->keys[1].key && a->keys[1].modifiers == b->keys[1].modifiers);
}

int nav_binding_format(const NavBinding *binding, char *out, size_t size)
{
    char first[32], second[32];
    nav_key_format(binding->keys[0], first, sizeof first);
    nav_key_format(binding->keys[1], second, sizeof second);
    return snprintf(out, size, "%s%s%s", first, binding->length == 2 ? " " : "",
                    binding->length == 2 ? second : "");
}

int nav_key_capture_normalize(const NavTermEvent *event, NavKeyStroke *stroke)
{
    if (event->type != NAV_TERM_EVENT_KEY) return -1;
    NavKeyStroke raw = {.key = event->key, .modifiers = event->modifiers};
    char text[64];
    nav_key_format(raw, text, sizeof text);
    return nav_key_parse(text, stroke);
}

int nav_keymap_labels(const NavKeymap *map, NavInputContext context, NavCommand command,
                      char *out, size_t size)
{
    if (!size) return 0;
    out[0] = 0;
    for (size_t i = 0; i < map->count; i++) {
        const NavBinding *b = &map->bindings[i];
        if (b->command != command || (b->context != context && b->context != NAV_CONTEXT_GLOBAL)) continue;
        NavInput state = {0}; NavAction action = {0};
        for (unsigned k = 0; k < b->length; k++) {
            NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = b->keys[k].key, .modifiers = b->keys[k].modifiers};
            action = nav_input_resolve(&state, map, context, &event);
        }
        if (action.command != command) continue;
        char sequence[80]; nav_binding_format(b, sequence, sizeof sequence);
        size_t used = strlen(out);
        if (used + strlen(sequence) + (used ? 2 : 0) >= size) {
            if (used + 4 < size) snprintf(out + used, size - used, " ...");
            break;
        }
        snprintf(out + used, size - used, "%s%s", used ? ", " : "", sequence);
    }
    return (int)strlen(out);
}

int nav_keymap_replace(NavKeymap *map, NavInputContext context, NavCommand command,
                       const char *const *sequences, size_t count, bool replace,
                       char *error, size_t size)
{
    NavKeymap additions = {0}, candidate = *map;
    if (command <= NAV_CMD_NONE || command >= NAV_CMD_COUNT || command == NAV_CMD_TEXT || context >= NAV_CONTEXT_COUNT) {
        snprintf(error, size, "invalid command or context"); return -1;
    }
    for (size_t i = 0; i < count; i++)
        if (nav_keymap_bind(&additions, context, sequences[i], nav_command_name(command), error, size)) return -1;
    size_t keep = 0;
    for (size_t i = 0; i < candidate.count; i++) {
        NavBinding old = candidate.bindings[i];
        if (old.context == context && old.command == command) continue;
        bool remove = false;
        for (size_t j = 0; j < additions.count; j++) if (nav_binding_overlaps(&old, &additions.bindings[j])) {
            if (!replace) {
                char key[80]; nav_binding_format(&additions.bindings[j], key, sizeof key);
                snprintf(error, size, "%s is assigned to %s (%s); replace with %s?", key,
                         nav_command_description(old.command), nav_context_name(old.context), nav_command_description(command));
                return 1;
            }
            remove = true;
        }
        if (!remove) candidate.bindings[keep++] = old;
    }
    candidate.count = keep;
    if (candidate.count + additions.count > NAV_BINDING_MAX) { snprintf(error, size, "too many bindings"); return -1; }
    for (size_t i = 0; i < additions.count; i++) candidate.bindings[candidate.count++] = additions.bindings[i];
    *map = candidate; return 0;
}
