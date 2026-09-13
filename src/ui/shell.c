/* One input boundary and command bar shared by all workspaces and modals. */
#include "nav_ui_core.h"
#include <stdio.h>
#include <string.h>

static NavInput input;
static bool quit_requested;
void nav_ui_request_quit(void) { quit_requested = true; }
bool nav_ui_quit_requested(void) { return quit_requested; }
static const NavKeymap *keymap;
static NavKeymap defaults;
static NavInputContext workspace = NAV_CONTEXT_PANEL;
void nav_ui_input_configure(const NavConfig *config)
{
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
    nav_ui_text(0, row, width, "", NAV_STYLE_KEYBAR);
    if (input.pending) {
        char prefix[64];
        nav_key_format(input.prefix, prefix, sizeof prefix);
        nav_ui_text(0, row, width, prefix, NAV_STYLE_KEYBAR_KEY);
        nav_ui_text((int)strlen(prefix), row, width - (int)strlen(prefix),
                    " ...  Escape cancels", NAV_STYLE_KEYBAR);
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

void nav_ui_binding_help(NavInputContext context)
{
    char storage[NAV_BINDING_MAX][160]; const char *lines[NAV_BINDING_MAX]; size_t count = 0;
    const NavKeymap *map = nav_ui_keymap();
    for (size_t i = 0; i < map->count; i++) {
        const NavBinding *b = &map->bindings[i];
        if (b->context != context && b->context != NAV_CONTEXT_GLOBAL) continue;
        NavInput state = {0}; NavAction action = {0};
        for (unsigned k = 0; k < b->length; k++) {
            NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = b->keys[k].key, .modifiers = b->keys[k].modifiers};
            action = nav_input_resolve(&state, map, context, &event);
        }
        if (!action.command || action.command != b->command) continue;
        char first[32], second[32];
        nav_key_format(b->keys[0], first, sizeof first); nav_key_format(b->keys[1], second, sizeof second);
        snprintf(storage[count], sizeof storage[count], "%s%s%s  %s", first,
                 b->length == 2 ? " " : "", b->length == 2 ? second : "", nav_command_name(b->command));
        lines[count] = storage[count]; count++;
    }
    nav_ui_info(" Key Bindings ", lines, count);
}
