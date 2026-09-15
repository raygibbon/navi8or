#include "nav_symbols.h"

const NavSymbols *nav_symbols_classic_dos(void)
{
    static const NavSymbols symbols = {'>', '^', '>', '^', 'v'};
    return &symbols;
}

static const NavSymbols *active_symbols;

const NavSymbols *nav_symbols_active(void)
{
    return active_symbols ? active_symbols : nav_symbols_classic_dos();
}

void nav_symbols_set_active(const NavSymbols *symbols)
{
    active_symbols = symbols;
}
