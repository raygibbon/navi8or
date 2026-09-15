/* One input boundary and command bar shared by all workspaces and modals. */
#include "nav_ui_core.h"
#include <stdio.h>
#include <string.h>

static NavInput input;
static bool quit_requested;
void nav_ui_request_quit(void) { quit_requested = true; }
bool nav_ui_quit_requested(void) { return quit_requested; }
static const NavKeymap *keymap;
static const NavConfig *live_config;
bool nav_ui_show_app_identity(void) { return !live_config || live_config->show_app_identity; }
static NavKeymap defaults;
static NavInputContext workspace = NAV_CONTEXT_PANEL;
void nav_ui_input_configure(const NavConfig *config)
{
    live_config = config;
    if (config) keymap = &config->keymap;
    else { nav_keymap_defaults(&defaults); keymap = &defaults; }
    input = (NavInput){0};
    quit_requested = false;
}
NavInputContext nav_ui_workspace(NavInputContext context)
{
    NavInputContext previous = workspace;
    workspace = context;
    input = (NavInput){0};
    return previous;
}
const NavKeymap *nav_ui_keymap(void)
{
    if (!keymap) nav_ui_input_configure(NULL);
    return keymap;
}
NavInputContext nav_ui_workspace_context(void) { return workspace; }
int nav_ui_input(NavInputContext context, NavAction *action)
{
    NavTermEvent event;
    int result = nav_term_poll_event(&event, -1);
    if (result > 0) *action = nav_input_resolve(&input, nav_ui_keymap(), context, &event);
    return result;
}
void nav_ui_command_bar(int row, int width, NavInputContext context,
                        bool (*available)(void *, NavCommand), void *data)
{
    if (row < 0) return;
    nav_ui_text(0, row, width, "", NAV_STYLE_KEYBAR);
    if (input.pending) {
        char prefix[64];
        nav_key_format(input.prefix, prefix, sizeof prefix);
        nav_ui_text(0, row, width, prefix, NAV_STYLE_KEYBAR_KEY);
        nav_ui_text((int)strlen(prefix), row, width - (int)strlen(prefix),
                    nav_ui_show_dialog_keys() ? " ...  Escape cancels" : " ...", NAV_STYLE_KEYBAR);
        return;
    }
    NavFunctionKeySegment segments[12];
    size_t count = nav_function_key_layout(width, nav_ui_keymap(), context, segments, 12);
    for (size_t i = 0; i < count; i++) {
        const NavFunctionKeySegment *segment = &segments[i];
        char key[8], label[64];
        snprintf(key, sizeof key, "F%d", segment->key);
        int label_width = segment->width - segment->key_width;
        bool enabled = !available || available(data, segment->command);
        snprintf(label, sizeof label, " %.*s", label_width > 1 ? label_width - 1 : 0, segment->label);
        nav_ui_text(segment->x, row, segment->key_width, key,
                    enabled ? NAV_STYLE_KEYBAR_KEY : NAV_STYLE_KEYBAR_DISABLED);
        if (label_width > 0) nav_ui_text(segment->x + segment->key_width, row, label_width, label,
                    enabled ? NAV_STYLE_KEYBAR : NAV_STYLE_KEYBAR_DISABLED);
    }
}


bool nav_ui_show_menu_keys(void) { return !live_config || live_config->show_menu_keys; }
bool nav_ui_show_dialog_keys(void) { return !live_config || live_config->show_dialog_keys; }
bool nav_ui_show_help_keys(void) { return !live_config || live_config->show_help_keys; }

int nav_ui_hint_key(NavInputContext context, NavCommand command, bool prefer_function, char *out, size_t size)
{
    const NavKeymap *map = nav_ui_keymap(); int best = 100;
    out[0] = 0;
    for (size_t i = 0; i < map->count; i++) {
        const NavBinding *b = &map->bindings[i];
        if (b->command != command || b->length != 1 || (b->context != context && b->context != NAV_CONTEXT_GLOBAL)) continue;
        NavInput state = {0}; NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = b->keys[0].key, .modifiers = b->keys[0].modifiers};
        if (nav_input_resolve(&state, map, context, &event).command != command) continue;
        bool function = b->keys[0].key >= NAV_KEY_F1 && b->keys[0].key <= NAV_KEY_F12;
        int rank = prefer_function && function ? 0 : !b->keys[0].modifiers && b->keys[0].key < 128 ? 1 : !b->keys[0].modifiers && !function ? 2 : 3;
        if (rank >= best) continue;
        best = rank; nav_binding_format(b, out, size);
        if (!strcmp(out, "Escape")) snprintf(out, size, "Esc");
        if (b->keys[0].key >= 'a' && b->keys[0].key <= 'z' && !b->keys[0].modifiers && out[0]) out[0] = (char)b->keys[0].key;
    }
    if (best == 100) return nav_keymap_label(map, context, command, out, size);
    return (int)strlen(out);
}

void nav_ui_hints(NavInputContext context, const NavCommand *commands, const char *const *labels,
                   size_t count, char *out, size_t size)
{
    out[0] = 0;
    if (!nav_ui_show_dialog_keys()) return;
    for (size_t i = 0; i < count; i++) {
        char key[80]; if (!nav_ui_hint_key(context, commands[i], true, key, sizeof key)) continue;
        size_t used = strlen(out);
        if (used < size) snprintf(out + used, size - used, "%s%s %s", used ? "   " : "", key,
                                  labels ? labels[i] : nav_command_description(commands[i]));
    }
}

void nav_ui_binding_help(NavInputContext context)
{
    char storage[NAV_CMD_COUNT][320]; const char *lines[NAV_CMD_COUNT]; size_t count = 0;
    for (NavCommand command = NAV_CMD_NONE + 1; command < NAV_CMD_COUNT; command++) {
        char bindings[256];
        if (!nav_keymap_labels(nav_ui_keymap(), context, command, bindings, sizeof bindings)) continue;
        snprintf(storage[count], sizeof storage[count], "%s%s%s", nav_command_description(command),
                 nav_ui_show_help_keys() ? "  " : "", nav_ui_show_help_keys() ? bindings : "");
        lines[count] = storage[count]; count++;
    }
    nav_ui_info(" Key Bindings ", lines, count);
}
