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

#ifndef TEXTURES_H
#define TEXTURES_H

#include "qtypes.h"

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
} qtexture32_t;

/* Allocate hunk space for a texture */
qtexture32_t *QTexture32_Alloc(int width, int height);

/* Create 32 bit texture from 8 bit source */
void QTexture32_8to32(const byte *in, int width, int height, int stride,
		    qboolean alpha, qtexture32_t *out);

/* Stretch from in size to out size */
void QTexture32_Stretch(const qtexture32_t *in, qtexture32_t *out);

/* Shrink texture in place to next mipmap level */
void QTexture32_MipMap(qtexture32_t *in);

#endif /* TEXTURES_H */
