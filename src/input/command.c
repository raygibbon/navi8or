#include "nav_input.h"
#include <string.h>
static const struct { const char *name, *label; } commands[] = {
#define NAV_COMMAND(id, name, label) {name, label},
#include "nav_commands.def"
#undef NAV_COMMAND
};
const char *nav_command_name(NavCommand command)
{ return (unsigned)command < NAV_CMD_COUNT ? commands[command].name : "unknown"; }
const char *nav_command_label(NavCommand command)
{ return (unsigned)command < NAV_CMD_COUNT ? commands[command].label : ""; }
NavCommand nav_command_parse(const char *name)
{
    if (!name) return NAV_CMD_COUNT;
    for (unsigned i = 0; i < NAV_CMD_COUNT; i++)
        if (!strcmp(name, commands[i].name)) return (NavCommand)i;
    return NAV_CMD_COUNT;
}
const char *nav_context_name(NavInputContext context)
{
    static const char *names[] = {"global", "panel", "viewer", "menu", "dialog",
        "confirm", "info", "picker", "vault", "editor", "terminal"};
    return context < NAV_CONTEXT_COUNT ? names[context] : "unknown";
}
