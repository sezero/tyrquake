/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2013 Kevin Shanahan and others

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef QPIC_H
#define QPIC_H

#include <stdint.h>

#include "qtypes.h"

typedef struct {
    int width;
    int height;
    int stride;
    const byte *pixels;
} qpic8_t;

typedef union {
    uint32_t rgba;
    struct {
	byte red;
	byte green;
	byte blue;
	byte alpha;
    };
} qpixel32_t;

typedef struct {
    int width;
    int height;
    qpixel32_t pixels[];
} qpic32_t;

typedef struct {
    qpixel32_t colors[256];
    qboolean alpha;
} qpalette32_t;

enum qpic_alpha_operation {
    QPIC_ALPHA_OP_NONE,
    QPIC_ALPHA_OP_EDGE_FIX,
    QPIC_ALPHA_OP_CLAMP_TO_ZERO,
};

/* Palettes for converting the base 8 bit texures to 32 bit RGBA */
extern qpalette32_t qpal_standard;
extern qpalette32_t qpal_fullbright;
extern qpalette32_t qpal_alpha_zero; /* Charset and sky foreground */
extern qpalette32_t qpal_alpha; /* HUD and sprites */

void QPic32_InitPalettes(const byte *palette);

/* Detect fullbright pixels in a source texture */
qboolean QPic_HasFullbrights(const qpic8_t *pic);

/* Allocate hunk space for a texture */
qpic32_t *QPic32_Alloc(int width, int height);

/* Create 32 bit texture from 8 bit source using the specified palette */
void QPic_8to32(const qpic8_t *in, qpic32_t *out, const qpalette32_t *palette, enum qpic_alpha_operation alpha_op);

/* Stretch from in size to out size */
void QPic32_Stretch(const qpic32_t *in, qpic32_t *out);

/* Copy smaller texture into top left of larger texture */
void QPic32_Expand(const qpic32_t *in, qpic32_t *out);

/* Shrink texture in place to next mipmap level */
void QPic32_MipMap(qpic32_t *pic, enum qpic_alpha_operation alpha_op);

#endif /* QPIC_H */
