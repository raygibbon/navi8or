#ifndef NAV_SYMBOLS_H
#define NAV_SYMBOLS_H
#include <stdint.h>
typedef struct
{
    uint32_t directory, parent, selected, upload, download;
} NavSymbols;
const NavSymbols *nav_symbols_classic_dos(void);
const NavSymbols *nav_symbols_active(void);
void nav_symbols_set_active(const NavSymbols *);
#endif
