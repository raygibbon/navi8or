/*
 * TDX
 * Filename: cp437.h
 * Version: 0.0.2
 * Author: Ray Gibbon
 * Date: August 13, 2026
 * Copyright (c) 2026 Ray Gibbon
 * Dedicated to the public domain under CC0 1.0.
 *
 * Origin: New TDX file; no TDE 5.1 or TDE 7 source ancestor.
 *
 * Changes: CP437 conversion declarations shared by terminal backends and tests.
 */

#ifndef TDX_CP437_H
#define TDX_CP437_H

#include <stdint.h>

uint32_t tdx_cp437_to_unicode( unsigned char );
int tdx_unicode_to_cp437( uint32_t, unsigned char * );

#endif
