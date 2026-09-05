#ifndef NAV_SYMBOLS_H
#define NAV_SYMBOLS_H
#include <stdint.h>
typedef struct { uint8_t upper_left, upper_right, lower_left, lower_right;
                 uint8_t horizontal, vertical, separator; } NavSymbols;
const NavSymbols *nav_symbols_classic_dos(void);
#endif
