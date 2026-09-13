#ifndef NAV_INPUT_H
#define NAV_INPUT_H
#include "nav_terminal.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
#define NAV_COMMAND(id, name, label) NAV_CMD_##id,
#include "nav_commands.def"
#undef NAV_COMMAND
    NAV_CMD_COUNT
} NavCommand;
typedef enum {
    NAV_CONTEXT_GLOBAL, NAV_CONTEXT_PANEL, NAV_CONTEXT_VIEWER,
    NAV_CONTEXT_MENU, NAV_CONTEXT_DIALOG, NAV_CONTEXT_CONFIRM,
    NAV_CONTEXT_INFO, NAV_CONTEXT_PICKER, NAV_CONTEXT_VAULT,
    NAV_CONTEXT_EDITOR, NAV_CONTEXT_TERMINAL, NAV_CONTEXT_COUNT
} NavInputContext;
typedef struct { int key; unsigned modifiers; } NavKeyStroke;
typedef struct {
    NavInputContext context;
    NavKeyStroke keys[2];
    unsigned length;
    NavCommand command;
    bool configured;
} NavBinding;
#define NAV_BINDING_MAX 256
typedef struct { NavBinding bindings[NAV_BINDING_MAX]; size_t count; } NavKeymap;
typedef struct { NavKeyStroke prefix; bool pending; NavInputContext context; } NavInput;
typedef struct {
    NavTermEventType type;
    NavCommand command;
    uint32_t text;
    int width, height;
} NavAction;
const char *nav_command_name(NavCommand);
const char *nav_command_label(NavCommand);
NavCommand nav_command_parse(const char *);
int nav_key_parse(const char *, NavKeyStroke *);
int nav_key_format(NavKeyStroke, char *, size_t);
void nav_keymap_defaults(NavKeymap *);
int nav_keymap_bind(NavKeymap *, NavInputContext, const char *, const char *, char *, size_t);
NavAction nav_input_resolve(NavInput *, const NavKeymap *, NavInputContext, const NavTermEvent *);
int nav_keymap_label(const NavKeymap *, NavInputContext, NavCommand, char *, size_t);
const char *nav_context_name(NavInputContext);
#endif
