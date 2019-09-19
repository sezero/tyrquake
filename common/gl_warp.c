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
// gl_warp.c -- sky and water polygons

#include <float.h>

#include "console.h"
#include "glquake.h"
#include "model.h"
#include "qpic.h"
#include "quakedef.h"
#include "sbar.h"
#include "sys.h"

#ifdef NQ_HACK
#include "host.h"
#endif

#define TURBSCALE (256.0 / (2 * M_PI))
static float turbsin[256] = {
#include "gl_warp_sin.h"
};

cvar_t r_waterquality = { "r_waterquality", "8", CVAR_CONFIG };

static inline void
AddWarpVert(vec4_t vert, float x, float y)
{
    float turb_s = turbsin[(int)((y * 2.0f) + (realtime * TURBSCALE)) & 255];
    float turb_t = turbsin[(int)((x * 2.0f) + (realtime * TURBSCALE)) & 255];

    vert[0] = x;
    vert[1] = y;
    vert[2] = (x + turb_s) * (1.0f / 64.0f);
    vert[3] = (y + turb_t) * (1.0f / 64.0f);
}

void
R_UpdateWarpTextures()
{
    texture_t *texture;
    int i, s, t, subdivisions, numindices, numverts, gl_warpimagesize;
    float x, y, step, *vertices, *vertex;
    uint16_t *indices, *index;

    if (cl.paused || r_drawflat.value || r_lightmap.value)
        return;

    subdivisions = qclamp(floorf(r_waterquality.value), 3.0f, 64.0f);
    step = (float)WARP_IMAGE_SIZE / (float)subdivisions;

    /* Draw the whole thing at once with drawelements */
    vertices = alloca((subdivisions + 1) * (subdivisions + 1) * sizeof(float) * 4);
    indices = alloca(subdivisions * subdivisions * 6 * sizeof(uint16_t));

    /* Add the first row of vertices */
    vertex = vertices;
    index = indices;
    x = 0.0f;
    for (s = 0; s <= subdivisions; s++, x += step) {
        AddWarpVert(vertex, x, 0.0f);
        vertex += 4;
    }

    /* Add the remaining rows */
    y = step;
    for (t = 1; t <= subdivisions; t++, y += step) {

        /* Add the first vertex separately, no complete quads yet */
        AddWarpVert(vertex, 0.0f, y);
        vertex += 4;

        x = step;
        for (s = 1; s <= subdivisions; s++, x += step) {
            AddWarpVert(vertex, x, y);
            vertex += 4;
            numverts = (vertex - vertices) >> 2;

            /* Add size indices for the two triangles in this quad */
            *index++ = numverts - subdivisions - 3;
            *index++ = numverts - 2;
            *index++ = numverts - subdivisions - 2;
            *index++ = numverts - subdivisions - 2;
            *index++ = numverts - 2;
            *index++ = numverts - 1;
        }
    }

    numverts = (vertex - vertices) >> 2;
    numindices = index - indices;
    gl_warpimagesize = 0;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WARP_IMAGE_SIZE, 0, WARP_IMAGE_SIZE, -99999, 99999);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GL_DisableMultitexture();
    glEnable(GL_VERTEX_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), vertices);
    qglClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), vertices + 2);

    for (i = 0; i < cl.worldmodel->numtextures; i++) {
        texture = cl.worldmodel->textures[i];
        if (!texture || !texture->mark)
            continue;
        if (texture->name[0] != '*')
            continue;

        /* Set the viewport appropriately for the warp target texture size */
        if (gl_warpimagesize != texture->gl_warpimagesize) {
            gl_warpimagesize = texture->gl_warpimagesize;
            glViewport(glx, gly + glheight - gl_warpimagesize, gl_warpimagesize, gl_warpimagesize);
        }

        // Render warp
        GL_Bind(texture->gl_texturenum);
        glDrawElements(GL_TRIANGLES, numindices, GL_UNSIGNED_SHORT, indices);
        GL_Bind(texture->gl_warpimage);

        // Enable legacy generate mipmap parameter if available
        qglTexParameterGenerateMipmap(GL_TRUE);

        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, glx, gly + glheight - gl_warpimagesize, gl_warpimagesize, gl_warpimagesize);

        // Regenerate mipmaps if extension is available
        qglGenerateMipmap(GL_TEXTURE_2D);

        texture->mark = 0;
    }

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_VERTEX_ARRAY);
}


//=========================================================

