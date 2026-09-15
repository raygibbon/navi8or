#ifndef NAV_TERMBOX_INPUT_H
#define NAV_TERMBOX_INPUT_H

#include "nav_terminal.h"

struct tb_event;

/* Translate a raw termbox event without polling or touching terminal state. */
int nav_term_translate_tb_event(const struct tb_event *, NavTermEvent *);

#endif
