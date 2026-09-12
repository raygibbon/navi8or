#ifndef NAV_THEME_H
#define NAV_THEME_H
#include <stdbool.h>
#include <stdint.h>
#include "nav.h"
#include "nav_terminal.h"
#include "nav_symbols.h"
typedef enum
{
    NAV_FRAME_ASCII,
    NAV_FRAME_SINGLE,
    NAV_FRAME_DOUBLE,
    NAV_FRAME_COMBINE,
    NAV_FRAME_COMBINE_REVERSE,
    NAV_FRAME_BLOCK
} NavFrameStyle;
typedef struct NavTheme
{
    char name[64];
    char display_name[64];
    int format;
    NavUiStyle style;
    uint8_t foreground[NAV_STYLE_COUNT];
    uint8_t background[NAV_STYLE_COUNT];
    NavSymbols symbols;
    NavFrameStyle frame_style;
    bool frame_space;
    bool shadow;
    int shadow_width;
} NavTheme;

typedef struct
{
    NavTheme theme;
    bool loaded_from_file;
    bool fallback;
    char path[NAV_PATH_MAX];
    char error[256];
    char warning[256];
} NavThemeResult;

const NavTheme *nav_theme_classic_dos(void);
int nav_theme_load_file(const char *, const char *, NavThemeResult *);
int nav_theme_load_result(const char *name, NavThemeResult *result);
const NavTheme *nav_theme_load(const char *name);
#endif
