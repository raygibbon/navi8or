#ifndef NAV_THEME_H
#define NAV_THEME_H
#include <stdint.h>
#include "nav_terminal.h"
typedef struct NavTheme { const char *name; uint8_t foreground[NAV_STYLE_COUNT]; uint8_t background[NAV_STYLE_COUNT]; } NavTheme;
const NavTheme *nav_theme_classic_dos(void);
#endif
