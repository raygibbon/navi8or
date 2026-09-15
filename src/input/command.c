#include "nav_input.h"
#include <string.h>
static const struct { const char *name, *label, *description; } commands[] = {
#define NAV_COMMAND(id, name, label, description) {name, label, description},
#include "nav_commands.def"
#undef NAV_COMMAND
};
const char *nav_command_name(NavCommand command)
{ return (unsigned)command < NAV_CMD_COUNT ? commands[command].name : "unknown"; }
const char *nav_command_label(NavCommand command)
{ return (unsigned)command < NAV_CMD_COUNT ? commands[command].label : ""; }
const char *nav_command_description(NavCommand command)
{ return (unsigned)command < NAV_CMD_COUNT ? commands[command].description : ""; }
static const struct { const char *name; NavCommand command; } aliases[] = {
        {"download", NAV_CMD_DOWNLOAD}, {"help", NAV_CMD_HELP}, {"view", NAV_CMD_VIEW}, {"edit", NAV_CMD_EDIT},
        {"copy", NAV_CMD_COPY}, {"move", NAV_CMD_MOVE}, {"mkdir", NAV_CMD_MKDIR},
        {"delete", NAV_CMD_DELETE}, {"quit", NAV_CMD_QUIT}, {"menu", NAV_CMD_MENU},
        {"pane_switch", NAV_CMD_PANEL_SWITCH}, {"parent", NAV_CMD_PANEL_PARENT},
        {"cancel", NAV_CMD_CANCEL}, {"preferences", NAV_CMD_PREFERENCES}, {"refresh", NAV_CMD_REFRESH}, {"location", NAV_CMD_OPEN_LOCATION}
    };
const char *nav_command_config_name(NavCommand command)
{
    for (size_t i = 0; i < sizeof aliases / sizeof *aliases; i++) if (aliases[i].command == command) return aliases[i].name;
    return nav_command_name(command);
}
NavCommand nav_command_parse(const char *name)
{
    if (!name) return NAV_CMD_COUNT;
    for (size_t i = 0; i < sizeof aliases / sizeof *aliases; i++)
        if (!strcmp(name, aliases[i].name)) return aliases[i].command;
    for (unsigned i = 0; i < NAV_CMD_COUNT; i++)
        if (!strcmp(name, commands[i].name)) return (NavCommand)i;
    return NAV_CMD_COUNT;
}
const char *nav_context_name(NavInputContext context)
{
    static const char *names[] = {"global", "panel", "viewer", "menu", "dialog",
        "confirm", "info", "picker", "vault", "editor", "terminal", "preferences"};
    return context < NAV_CONTEXT_COUNT ? names[context] : "unknown";
}
