/*
Copyright (C) 1996-1997 Id Software, Inc.

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

#ifndef GL_MODEL_H
#define GL_MODEL_H

#include <assert.h>

#include "model.h"

#define	BLOCK_WIDTH	128
#define	BLOCK_HEIGHT	128

typedef struct {
    unsigned char l, t, w, h;
} glRect_t;

typedef struct lm_block_s {
    glpoly_t *polys;
    qboolean modified;
    glRect_t rectchange;
    int allocated[BLOCK_WIDTH];
    byte data[4 * BLOCK_WIDTH * BLOCK_HEIGHT]; /* lightmaps */
    GLuint texture;
} lm_block_t;

typedef struct {
    int numblocks;
    lm_block_t *blocks;

    brushmodel_t brushmodel;
} glbrushmodel_t;

static inline glbrushmodel_t *
GLBrushModel(brushmodel_t *brushmodel)
{
    if (brushmodel->parent)
	return container_of(brushmodel->parent, glbrushmodel_t, brushmodel);

    return container_of(brushmodel, glbrushmodel_t, brushmodel);
}

static inline const glbrushmodel_t *
GLConstBrushModel(const brushmodel_t *brushmodel)
{
    if (brushmodel->parent)
	return const_container_of(brushmodel->parent, glbrushmodel_t, brushmodel);

    return const_container_of(brushmodel, glbrushmodel_t, brushmodel);
}

#endif /* GL_MODEL_H */
