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
// gl_rsurf.c: surface-related refresh code

#include "console.h"
#include "gl_model.h"
#include "glquake.h"
#include "quakedef.h"
#include "sys.h"

#ifdef NQ_HACK
#include "host.h"
#endif

/*
 * ===================
 * R_AddDynamicLights
 * ===================
 * Check all dynamic lights against this surface
 */
static void
R_AddDynamicLights(const msurface_t *surf, unsigned *blocklights)
{
    int lnum;
    int sd, td;
    float dist, rad, minlight;
    vec3_t impact, local;
    int s, t;
    int i;
    int smax, tmax;
    mtexinfo_t *tex;
    dlight_t *dl;

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    tex = surf->texinfo;

    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
	if (!(surf->dlightbits & (1U << lnum)))
	    continue;		// not lit by this light

	dl = &cl_dlights[lnum];

	rad = dl->radius;
	dist = DotProduct(dl->origin, surf->plane->normal) - surf->plane->dist;
	rad -= fabs(dist);
	minlight = dl->minlight;
	if (rad < minlight)
	    continue;
	minlight = rad - minlight;

	for (i = 0; i < 3; i++)
	    impact[i] = dl->origin[i] -	surf->plane->normal[i] * dist;

	local[0] = DotProduct(impact, tex->vecs[0]) + tex->vecs[0][3];
	local[0] -= surf->texturemins[0];
	local[1] = DotProduct(impact, tex->vecs[1]) + tex->vecs[1][3];
	local[1] -= surf->texturemins[1];

	for (t = 0; t < tmax; t++) {
	    td = local[1] - t * 16;
	    if (td < 0)
		td = -td;
	    for (s = 0; s < smax; s++) {
		sd = local[0] - s * 16;
		if (sd < 0)
		    sd = -sd;
		if (sd > td)
		    dist = sd + (td >> 1);
		else
		    dist = td + (sd >> 1);
		if (dist < minlight) {
                    float scale = (rad - dist) * 256.0f;
                    unsigned *dest = &blocklights[(t * smax + s) * gl_lightmap_bytes];
		    *dest++ += dl->color[0] * scale;
		    *dest++ += dl->color[1] * scale;
		    *dest++ += dl->color[2] * scale;
                }
	    }
	}
    }
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the correct format in blocklights
===============
*/
static void
R_BuildLightMap(msurface_t *surf, byte *dest, int stride)
{
    int smax, tmax;
    int i, j, size;
    byte *lightmap;
    unsigned scale;
    int map;
    unsigned blocklights[18 * 18 * gl_lightmap_bytes];
    unsigned *block;

    surf->cached_dlight = (surf->dlightframe == r_framecount);

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    size = smax * tmax;

    /* set to full bright if no light data */
    if (!cl.worldmodel->lightdata) {
        memset(blocklights, 255, size * gl_lightmap_bytes * sizeof(blocklights[0]));
	goto store;
    }

    /* clear to no light */
    memset(blocklights, 0, size * gl_lightmap_bytes * sizeof(blocklights[0]));

    /* add all the lightmaps */
    lightmap = surf->samples;
    if (lightmap) {
	foreach_surf_lightstyle(surf, map) {
	    scale = d_lightstylevalue[surf->styles[map]];
	    surf->cached_light[map] = scale;	// 8.8 fraction
            block = blocklights;
	    for (i = 0; i < size; i++) {
                *block++ += *lightmap++ * scale;
                *block++ += *lightmap++ * scale;
                *block++ += *lightmap++ * scale;
            }
	}
    }

    /* add all the dynamic lights */
    if (surf->dlightframe == r_framecount)
	R_AddDynamicLights(surf, blocklights);

    /* bound, invert, and shift */
  store:
    block = blocklights;
    for (i = 0; i < tmax; i++, dest += stride - (smax * 3)) {
        for (j = 0; j < smax; j++) {
            *dest++ = qmin(*block++ >> 7, 255u);
            *dest++ = qmin(*block++ >> 7, 255u);
            *dest++ = qmin(*block++ >> 7, 255u);
        }
    }
}

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

lpMultiTexFUNC qglMultiTexCoord2fARB;
lpActiveTextureFUNC qglActiveTextureARB;
lpClientStateFUNC qglClientActiveTexture;

static qboolean mtexenabled = false;
static GLenum oldtarget = GL_TEXTURE0_ARB;
static int cnttextures[3] = { -1, -1, -1 };	// cached

/*
 * Makes the given texture unit active
 * FIXME: only aware of two texture units...
 */
void
GL_SelectTexture(GLenum target)
{
    if (!gl_mtexable || target == oldtarget)
	return;

    /*
     * Save the current texture unit's texture handle, select the new texture
     * unit and update currenttexture
     */
    qglActiveTextureARB(target);
    cnttextures[oldtarget - GL_TEXTURE0_ARB] = currenttexture;
    currenttexture = cnttextures[target - GL_TEXTURE0_ARB];
    oldtarget = target;
}

void
GL_DisableMultitexture(void)
{
    if (mtexenabled) {
	glDisable(GL_TEXTURE_2D);
	GL_SelectTexture(GL_TEXTURE0_ARB);
	mtexenabled = false;
    }
}

void
GL_EnableMultitexture(void)
{
    if (gl_mtexable) {
	GL_SelectTexture(GL_TEXTURE1_ARB);
	glEnable(GL_TEXTURE_2D);
	mtexenabled = true;
    }
}

/*
 * R_UploadLightmapUpdate
 * Re-uploads the modified region of the given lightmap number
 */
static void
R_UploadLMBlockUpdate(lm_block_t *block)
{
    glRect_t *rect;
    byte *pixels;
    unsigned offset;

    rect = &block->rectchange;
    offset = (BLOCK_WIDTH * rect->t + rect->l) * gl_lightmap_bytes;
    pixels = block->data + offset;

    /* set unpacking width to BLOCK_WIDTH, reset after */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, BLOCK_WIDTH);
    glTexSubImage2D(GL_TEXTURE_2D,
		    0,
		    rect->l, /* x-offset */
		    rect->t, /* y-offset */
		    rect->w,
		    rect->h,
		    gl_lightmap_format,
		    GL_UNSIGNED_BYTE,
		    pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    rect->l = BLOCK_WIDTH;
    rect->t = BLOCK_HEIGHT;
    rect->h = 0;
    rect->w = 0;
    block->modified = false;

    c_lightmaps_uploaded++;
}

/*
 * R_UpdateLightmapBlockRect
 */
static void
R_UpdateLightmapBlockRect(glbrushmodel_resource_t *resources, msurface_t *surf)
{
    int map;
    byte *base;
    int smax, tmax;
    lm_block_t *block;
    glRect_t *rect;

    if (!r_dynamic.value)
	return;

    /* Check if any of this surface's lightmaps changed */
    foreach_surf_lightstyle(surf, map)
	if (d_lightstylevalue[surf->styles[map]] != surf->cached_light[map])
	    goto dynamic;

    /*
     * 	surf->dlightframe == r_framecount	=> dynamic this frame
     *  surf->cached_dlight		=> dynamic previously
     */
    if (surf->dlightframe == r_framecount || surf->cached_dlight) {
    dynamic:
	/*
	 * Record that the lightmap block for this surface has been
	 * modified. If necessary, increase the modified rectangle to include
	 * this surface's allocatied sub-area.
	 */
	block = &resources->blocks[surf->lightmapblock];
	rect = &block->rectchange;
	block->modified = true;
	if (surf->light_t < rect->t) {
	    if (rect->h)
		rect->h += rect->t - surf->light_t;
	    rect->t = surf->light_t;
	}
	if (surf->light_s < rect->l) {
	    if (rect->w)
		rect->w += rect->l - surf->light_s;
	    rect->l = surf->light_s;
	}
	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	if ((rect->w + rect->l) < (surf->light_s + smax))
	    rect->w = (surf->light_s - rect->l) + smax;
	if ((rect->h + rect->t) < (surf->light_t + tmax))
	    rect->h = (surf->light_t - rect->t) + tmax;
	base = block->data;
	base += surf->light_t * BLOCK_WIDTH * gl_lightmap_bytes;
	base += surf->light_s * gl_lightmap_bytes;
	R_BuildLightMap(surf, base, BLOCK_WIDTH * gl_lightmap_bytes);
    }
}

// TODO: re-implement mirror rendering
#if 0
/*
================
R_MirrorChain
================
*/
static void
R_MirrorChain(msurface_t *surf)
{
    if (mirror)
	return;
    mirror = true;
    mirror_plane = surf->plane;
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * The triangle buffer is on the stack, so we want to keep the size
 * reasonable.  We allow for up to 3 indices per vertex, which means
 * we can never run out of indices and only need to check space for
 * vertices.
 */
#define TRIBUF_MAX_VERTS 4096
#define TRIBUF_MAX_INDICES (TRIBUF_MAX_VERTS * 3)

typedef struct {
    int numverts;
    int numindices;
    float verts[TRIBUF_MAX_VERTS][VERTEXSIZE];
    byte colors[TRIBUF_MAX_VERTS * 3];
    uint16_t indices[TRIBUF_MAX_INDICES];
} triangle_buffer_t;

int gl_draw_calls;
int gl_verts_submitted;
int gl_indices_submitted;
int gl_full_buffers;

static qboolean
TriBuf_CheckSpacePoly(const triangle_buffer_t *buffer, const glpoly_t *poly)
{
    if (buffer->numverts + poly->numverts > TRIBUF_MAX_VERTS) {
	gl_full_buffers++;
	return false;
    }

    return true;
}

/*
 * NOTE: Call without incrementing buffer->numverts so we have the
 *       correct place to start the indices.  This function will do
 *       the numverts increment when done.
 */
static inline void
TriBuf_AddPolyIndices(triangle_buffer_t *buffer, const glpoly_t *poly)
{
    int i;
    uint16_t *index = &buffer->indices[buffer->numindices];
    for (i = 1; i < poly->numverts - 1; i++) {
	*index++ = buffer->numverts;
	*index++ = buffer->numverts + i;
	*index++ = buffer->numverts + i + 1;
    }
    buffer->numverts += poly->numverts;
    buffer->numindices += (poly->numverts - 2) * 3;

    c_brush_polys++;
}

static void
TriBuf_AddPoly(triangle_buffer_t *buffer, const glpoly_t *poly)
{
    int vert_bytes;

    assert(TriBuf_CheckSpacePoly(buffer, poly));

    vert_bytes = sizeof(poly->verts[0]) * poly->numverts;
    memcpy(&buffer->verts[buffer->numverts], &poly->verts[0], vert_bytes);

    TriBuf_AddPolyIndices(buffer, poly);
}

static void
TriBuf_AddFlatPoly(triangle_buffer_t *buffer, const glpoly_t *poly)
{
    GLbyte color[3];
    int i;

    srand((intptr_t)poly);
    color[0] = (byte)(rand() & 0xff);
    color[1] = (byte)(rand() & 0xff);
    color[2] = (byte)(rand() & 0xff);

    byte *dst = &buffer->colors[buffer->numverts * 3];
    for (i = 0; i < poly->numverts; i++) {
        *dst++ = color[0];
        *dst++ = color[1];
        *dst++ = color[2];
    }
    TriBuf_AddPoly(buffer, poly);
}

#define TURBSCALE (256.0 / (2 * M_PI))
static float turbsin[256] = {
#include "gl_warp_sin.h"
};

static void
TriBuf_AddTurbPoly(triangle_buffer_t *buffer, const glpoly_t *poly)
{
    int i;

    assert(TriBuf_CheckSpacePoly(buffer, poly));

    const float *src = &poly->verts[0][0];
    float *dst = &buffer->verts[buffer->numverts][0];
    for (i = 0; i < poly->numverts; i++) {
	VectorCopy(src, dst);
	float turb_s = turbsin[(int)((src[4] * 0.125f + realtime) * TURBSCALE) & 255];
	float turb_t = turbsin[(int)((src[3] * 0.125f + realtime) * TURBSCALE) & 255];
	dst[3] = (src[3] + turb_s) * (1.0f / 64);
	dst[4] = (src[4] + turb_t) * (1.0f / 64);
	src += VERTEXSIZE;
	dst += VERTEXSIZE;
    }
    TriBuf_AddPolyIndices(buffer, poly);
}

static void
TriBuf_AddSkyPoly(triangle_buffer_t *buffer, const glpoly_t *poly, float speed1, float speed2)
{
    int i;
    vec3_t dir;
    float length;

    assert(TriBuf_CheckSpacePoly(buffer, poly));

    const float *src = &poly->verts[0][0];
    float *dst = &buffer->verts[buffer->numverts][0];
    for (i = 0; i < poly->numverts; i++) {
	VectorCopy(src, dst);

	VectorSubtract(src, r_origin, dir);
	dir[2] *= 3;	// flatten the sphere
	length = DotProduct(dir, dir);
	length = sqrtf(length);
	length = 6 * 63 / length;
	dir[0] *= length;
	dir[1] *= length;

	dst[3] = (speed1 + dir[0]) * (1.0 / 128);
	dst[4] = (speed1 + dir[1]) * (1.0 / 128);
	dst[5] = (speed2 + dir[0]) * (1.0 / 128);
	dst[6] = (speed2 + dir[1]) * (1.0 / 128);

	src += VERTEXSIZE;
	dst += VERTEXSIZE;
    }
    TriBuf_AddPolyIndices(buffer, poly);
}

/*
 * Water/Slime/Lava/Tele are fullbright (no lightmap)
 * May be blended, depending on r_wateralpha setting
 */
static void
TriBuf_DrawTurb(triangle_buffer_t *buffer, const texture_t *texture, float alpha)
{
    if (gl_mtexable) {
	GL_DisableMultitexture();
	qglClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	qglClientActiveTexture(GL_TEXTURE0);
	GL_SelectTexture(GL_TEXTURE0_ARB);
    }

    if (alpha < 1.0f) {
	glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, qmax(alpha, 0.0f));
    }

    GL_Bind(texture->gl_texturenum);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
    glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
    glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);
    gl_draw_calls++;
    gl_verts_submitted += buffer->numverts;
    gl_indices_submitted += buffer->numindices;

    if (alpha < 1.0f) {
	glColor4f(1, 1, 1, 1);
	glDisable(GL_BLEND);
    }

    if (gl_mtexable) {
	GL_EnableMultitexture();
	qglClientActiveTexture(GL_TEXTURE1);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    }
}

static void
TriBuf_DrawFlat(triangle_buffer_t *buffer)
{
    Sys_Printf("Drawing %d flat triangles\n", buffer->numindices / 3);

    if (gl_mtexable) {
	GL_DisableMultitexture();
	qglClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	qglClientActiveTexture(GL_TEXTURE0);
	GL_SelectTexture(GL_TEXTURE0_ARB);
    }

    glEnableClientState(GL_COLOR_ARRAY);
    glDisable(GL_TEXTURE_2D);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
    glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
    glColorPointer(3, GL_UNSIGNED_BYTE, 0, buffer->colors);
    glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);
    gl_draw_calls++;
    gl_verts_submitted += buffer->numverts;
    gl_indices_submitted += buffer->numindices;

    glDisableClientState(GL_COLOR_ARRAY);
    glEnable(GL_TEXTURE_2D);

    if (gl_mtexable) {
	GL_EnableMultitexture();
	qglClientActiveTexture(GL_TEXTURE1);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    }
}

static void
TriBuf_DrawSky(triangle_buffer_t *buffer, const texture_t *texture)
{
    if (gl_mtexable) {
	GL_SelectTexture(GL_TEXTURE0_ARB);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	GL_Bind(texture->gl_texturenum);
	GL_SelectTexture(GL_TEXTURE1_ARB);
        GL_Bind(texture->gl_texturenum_alpha);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);

	glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
	qglClientActiveTexture(GL_TEXTURE0);
	glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
	qglClientActiveTexture(GL_TEXTURE1);
	glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][5]);

	glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);
	gl_draw_calls++;
	gl_verts_submitted += buffer->numverts;
	gl_indices_submitted += buffer->numindices;
    } else {
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	GL_Bind(texture->gl_texturenum);

	glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
	glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
	glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);

	glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);

        GL_Bind(texture->gl_texturenum_alpha);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
        glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][5]);
        glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);

        glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	gl_draw_calls += 2;
	gl_verts_submitted += buffer->numverts * 2;
	gl_indices_submitted += buffer->numindices * 2;
    }
}

static void
TriBuf_Draw(triangle_buffer_t *buffer, const texture_t *texture, lm_block_t *block, int flags)
{
    if (gl_mtexable) {
	GL_SelectTexture(GL_TEXTURE0_ARB);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	GL_Bind(texture->gl_texturenum);
	GL_SelectTexture(GL_TEXTURE1_ARB);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        GL_Bind(block->texture);
        if (block->modified)
            R_UploadLMBlockUpdate(block);

	glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
	qglClientActiveTexture(GL_TEXTURE0);
	glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
	qglClientActiveTexture(GL_TEXTURE1);
	glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][5]);

        if (gl_num_texture_units > 2 && texture->gl_texturenum_fullbright && gl_fullbrights.value) {
            GL_SelectTexture(GL_TEXTURE2);
            glEnable(GL_TEXTURE_2D);
            glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
            GL_Bind(texture->gl_texturenum_fullbright);
            qglClientActiveTexture(GL_TEXTURE2);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
        }

	glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);
	gl_draw_calls++;
	gl_verts_submitted += buffer->numverts;
	gl_indices_submitted += buffer->numindices;

        if (gl_num_texture_units > 2 && texture->gl_texturenum_fullbright && gl_fullbrights.value) {
            glDisable(GL_TEXTURE_2D);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            GL_SelectTexture(GL_TEXTURE1);
            qglClientActiveTexture(GL_TEXTURE1);
        }
    } else {
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	GL_Bind(texture->gl_texturenum);

	glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
	glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
	glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);

	glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        Fog_StartBlend();
        GL_Bind(block->texture);
        if (block->modified)
        R_UploadLMBlockUpdate(block);

        glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
        glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][5]);
        glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);

        Fog_StopBlend();

        if (Fog_GetDensity() > 0) {
            /* Extra pass for fog with geometry color set to black */
            glBlendFunc(GL_ONE, GL_ONE);
            glColor3f(0, 0, 0);
            GL_Bind(texture->gl_texturenum);
            glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][0]);
            glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
            glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);
            glColor3f(1, 1, 1);
        }

        glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	gl_draw_calls += 2;
	gl_verts_submitted += buffer->numverts * 2;
	gl_indices_submitted += buffer->numindices * 2;
    }

    if ((!gl_mtexable || gl_num_texture_units < 3) && texture->gl_texturenum_fullbright && gl_fullbrights.value) {
        if (gl_mtexable) {
            GL_DisableMultitexture();
            qglClientActiveTexture(GL_TEXTURE1);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            GL_SelectTexture(GL_TEXTURE0_ARB);
        }

        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GL_Bind(texture->gl_texturenum_fullbright);
        glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(float), &buffer->verts[0][3]);
        glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);

        gl_draw_calls++;
        gl_verts_submitted += buffer->numverts;
        gl_indices_submitted += buffer->numindices;

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        if (gl_mtexable) {
            GL_EnableMultitexture();
            qglClientActiveTexture(GL_TEXTURE1);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        }
    }
}

static void
DrawSkyChain(triangle_buffer_t *buffer, msurface_t *surf, texture_t *texture)
{
    float speed1, speed2;
    glpoly_t *poly = NULL;

    speed1 = realtime * 8;
    speed1 -= (int)speed1 & ~127;
    speed2 = realtime * 16;
    speed2 -= (int)speed2 & ~127;

    for ( ; surf; surf = surf->chain) {
	for (poly = surf->polys ; poly; poly = poly->next) {
	    if (!TriBuf_CheckSpacePoly(buffer, poly))
		goto drawBuffer;
	addPoly:
	    TriBuf_AddSkyPoly(buffer, poly, speed1, speed2);
	}
    }
 drawBuffer:
    TriBuf_DrawSky(buffer, texture);
    buffer->numverts = 0;
    buffer->numindices = 0;
    if (poly)
	goto addPoly;
}

static void
DrawTurbChain(triangle_buffer_t *buffer, msurface_t *surf, texture_t *texture)
{
    glpoly_t *poly = NULL;

    for ( ; surf; surf = surf->chain) {
	for (poly = surf->polys ; poly; poly = poly->next) {
	    if (!TriBuf_CheckSpacePoly(buffer, poly))
		goto drawBuffer;
	addPoly:
	    TriBuf_AddTurbPoly(buffer, poly);
	}
    }
 drawBuffer:
    TriBuf_DrawTurb(buffer, texture, r_wateralpha.value);
    buffer->numverts = 0;
    buffer->numindices = 0;
    if (poly)
	goto addPoly;
}

static void
DrawFlatChain(triangle_buffer_t *buffer, msurface_t *surf)
{
    glpoly_t *poly = NULL;

    for ( ; surf; surf = surf->chain) {
	for (poly = surf->polys ; poly; poly = poly->next) {
	    if (!TriBuf_CheckSpacePoly(buffer, poly))
		goto drawBuffer;
	addPoly:
	    TriBuf_AddFlatPoly(buffer, poly);
	}
    }
 drawBuffer:
    TriBuf_DrawFlat(buffer);
    buffer->numverts = 0;
    buffer->numindices = 0;
    if (poly)
	goto addPoly;
}

static void
DrawMaterialChains(const entity_t *e)
{
    int i;
    msurface_t *surf, *materialchain;
    brushmodel_t *brushmodel;
    glbrushmodel_t *glbrushmodel;
    surface_material_t *material;
    triangle_buffer_t buffer = {0};

    glEnable(GL_VERTEX_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    if (gl_mtexable) {
	qglClientActiveTexture(GL_TEXTURE0);
    }
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    if (gl_mtexable) {
	qglClientActiveTexture(GL_TEXTURE1);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	GL_EnableMultitexture();
    }

    // MATERIAL CHAIN
    brushmodel = BrushModel(e->model);
    glbrushmodel = GLBrushModel(brushmodel);
    material = glbrushmodel->materials;
    for (i = 0; i < glbrushmodel->nummaterials; i++, material++) {
	materialchain = glbrushmodel->materialchains[i];
	if (!materialchain)
	    continue;
        if (r_drawflat.value) {
            DrawFlatChain(&buffer, materialchain);
            continue;
        }
	int flags = materialchain->flags;
	if ((flags & SURF_DRAWTURB) && r_wateralpha.value < 1.0f)
	    continue; // Transparent surfaces last

	surf = materialchain;
	texture_t *texture = brushmodel->textures[material->texturenum];
	if (flags & SURF_DRAWSKY) {
	    DrawSkyChain(&buffer, surf, texture);
	    continue;
	}
	if (flags & SURF_DRAWTURB) {
	    DrawTurbChain(&buffer, surf, texture);
	    continue;
	}

	lm_block_t *block = &glbrushmodel->resources->blocks[material->lightmapblock];
	for ( ; surf; surf = surf->chain) {
	    if (!surf->polys)
		continue;
	    if (!TriBuf_CheckSpacePoly(&buffer, surf->polys))
		goto drawBuffer;
	addPoly:
	    R_UpdateLightmapBlockRect(glbrushmodel->resources, surf);
	    TriBuf_AddPoly(&buffer, surf->polys);
	}
    drawBuffer:
	TriBuf_Draw(&buffer, texture, block, materialchain->flags);
	buffer.numverts = 0;
	buffer.numindices = 0;
	if (surf && surf->polys)
	    goto addPoly;
    }

    if (gl_mtexable) {
	GL_DisableMultitexture();
	qglClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	qglClientActiveTexture(GL_TEXTURE0);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_VERTEX_ARRAY);
}

/*
 * TODO: probably rename / rework this (DrawTransparentSurfaces) some more as it's only the world surfaces right now...
 */
void
R_DrawTransparentSurfaces(void)
{
    int i;
    msurface_t *materialchain;
    texture_t *texture;
    triangle_buffer_t buffer = {0};

    if (r_wateralpha.value >= 1.0f)
	return;

    //
    // go back to the world matrix
    //
    glLoadMatrixf(r_world_matrix);

    GL_DisableMultitexture();
    glEnable(GL_VERTEX_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    if (gl_mtexable) {
	qglClientActiveTexture(GL_TEXTURE0);
    }
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDepthMask(GL_FALSE);

    brushmodel_t *brushmodel = cl.worldmodel;
    glbrushmodel_t *glbrushmodel = GLBrushModel(brushmodel);
    surface_material_t *material = glbrushmodel->materials;
    for (i = 0; i < glbrushmodel->nummaterials; i++, material++) {
	materialchain = glbrushmodel->materialchains[i];
	if (!materialchain)
	    continue;
	int flags = materialchain->flags;
	if (!(flags & SURF_DRAWTURB))
	    continue;

	texture = brushmodel->textures[material->texturenum];
	DrawTurbChain(&buffer, materialchain, texture);
    }

    glDepthMask(GL_TRUE);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_VERTEX_ARRAY);
}

/*
 * Adds the surfaces from the brushmodel to the given material chains
 * Allows us to add submodels to the world material chains if they are static.
 */
static void
R_AddBrushModelToMaterialChains(const brushmodel_t *brushmodel, const vec3_t modelorg, msurface_t **materialchains)
{
    int i;
    msurface_t *surf;

    /* Gather visible surfaces */
    surf = &brushmodel->surfaces[brushmodel->firstmodelsurface];
    for (i = 0; i < brushmodel->nummodelsurfaces; i++, surf++) {
        mplane_t *plane = surf->plane;
        float dot = DotProduct(modelorg, plane->normal) - plane->dist;
        if ((surf->flags & SURF_PLANEBACK) && dot >= -BACKFACE_EPSILON)
            continue;
        if (!(surf->flags & SURF_PLANEBACK) && dot <= BACKFACE_EPSILON)
            continue;
        surf->chain = materialchains[surf->material];
        materialchains[surf->material] = surf;
    }
}

static inline void
SwapChains(int material1, int material2, msurface_t **materialchains)
{
    if (material1 != material2) {
        msurface_t *tmp = materialchains[material1];
        materialchains[material1] = materialchains[material2];
        materialchains[material2] = tmp;
    }
}

/*
 * Simple swap of animation materials when no swapping between alt frames is involved
 */
static inline void
R_SwapAnimationChains(const glbrushmodel_t *glbrushmodel, msurface_t **materialchains)
{
    const material_animation_t *animation;
    int i, basematerial, altmaterial;
    int frametick = (int)(cl.time * 5.0f);

    animation = glbrushmodel->animations;
    for (i = 0; i < glbrushmodel->numanimations; i++, animation++) {
        basematerial = animation->frames[frametick % animation->numframes];
        SwapChains(basematerial, animation->frames[0], materialchains);
        if (animation->numalt) {
            altmaterial = animation->alt[frametick % animation->numalt];
            SwapChains(altmaterial, animation->alt[0], materialchains);
        }
    }
}

/*
 * Swap entity alt animations, if entity alt-frame is active
 */
static inline void
R_SwapAltAnimationChains(const entity_t *entity, msurface_t **materialchains)
{
    const glbrushmodel_t *glbrushmodel;
    const material_animation_t *animation;
    int i;

    if (!entity->frame)
        return;

    glbrushmodel = GLBrushModel(BrushModel(entity->model));
    animation = glbrushmodel->animations;
    for (i = 0; i < glbrushmodel->numanimations; i++, animation++) {
        if (animation->numalt)
            SwapChains(animation->frames[0], animation->alt[0], materialchains);
    }
}

/*
 * This is for all non-world brushmodels that have a transform
 */
static void
R_SetupDynamicBrushModelMaterialChains(const entity_t *entity, const vec3_t modelorg)
{
    brushmodel_t *brushmodel = BrushModel(entity->model);
    glbrushmodel_t *glbrushmodel = GLBrushModel(brushmodel);
    msurface_t **materialchains = glbrushmodel->materialchains;

    memset(materialchains, 0, glbrushmodel->nummaterials * sizeof(msurface_t *));

    R_AddBrushModelToMaterialChains(brushmodel, modelorg, materialchains);
    R_SwapAltAnimationChains(entity, materialchains);
    R_SwapAnimationChains(glbrushmodel, materialchains);
}

static void
R_AddStaticBrushModelToWorldMaterialChains(const entity_t *entity)
{
    brushmodel_t *brushmodel = BrushModel(entity->model);
    msurface_t **materialchains = GLBrushModel(brushmodel->parent)->materialchains;

    R_SwapAltAnimationChains(entity, materialchains);
    R_AddBrushModelToMaterialChains(brushmodel, r_refdef.vieworg, materialchains);
    R_SwapAltAnimationChains(entity, materialchains);
}

/*
=================
R_DrawDynamicBrushModel
=================
*/
void
R_DrawDynamicBrushModel(const entity_t *entity)
{
    int i;
    vec3_t mins, maxs, angles_bug, modelorg;
    model_t *model;
    brushmodel_t *brushmodel;
    qboolean rotated = false;

    model = entity->model;
    brushmodel = BrushModel(model);

    /*
     * Static (non-rotated/translated) models are drawn with the world,
     * so we skip them here
     */
    if (entity->angles[0] || entity->angles[1] || entity->angles[2])
	rotated = true;
    else if (brushmodel->parent && !entity->origin[0] && !entity->origin[1] && !entity->origin[2])
        return;

    if (rotated) {
	for (i = 0; i < 3; i++) {
	    mins[i] = entity->origin[i] - model->radius;
	    maxs[i] = entity->origin[i] + model->radius;
	}
    } else {
	VectorAdd(entity->origin, model->mins, mins);
	VectorAdd(entity->origin, model->maxs, maxs);
    }

    if (R_CullBox(mins, maxs))
	return;

    currenttexture = -1;

    VectorSubtract(r_refdef.vieworg, entity->origin, modelorg);
    if (rotated) {
	vec3_t temp;
	vec3_t forward, right, up;

	VectorCopy(modelorg, temp);
	AngleVectors(entity->angles, forward, right, up);
	modelorg[0] = DotProduct(temp, forward);
	modelorg[1] = -DotProduct(temp, right);
	modelorg[2] = DotProduct(temp, up);
    }

    if (gl_zfix.value)
	glEnable(GL_POLYGON_OFFSET_FILL);

    glPushMatrix();

    /* Stupid bug means pitch is reversed for entities */
    VectorCopy(entity->angles, angles_bug);
    angles_bug[PITCH] = -angles_bug[PITCH];
    R_RotateForEntity(entity->origin, angles_bug);

    /* Setup material chains and draw */
    R_SetupDynamicBrushModelMaterialChains(entity, modelorg);
    DrawMaterialChains(entity);

    glPopMatrix();

    if (gl_zfix.value)
	glDisable(GL_POLYGON_OFFSET_FILL);
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
static void
R_RecursiveWorldNode(const vec3_t modelorg, mnode_t *node)
{
    int numsurfaces, side;
    mplane_t *plane;
    msurface_t *surf, **mark;
    mleaf_t *pleaf;
    double dot;

    if (node->contents == CONTENTS_SOLID)
	return; // solid
    if (node->visframe != r_visframecount)
	return; // Not in PVS
    if (R_CullBox(node->mins, node->maxs))
	return; // Outside frustum

    // if a leaf node, mark the surfaces with the current visframe
    if (node->contents < 0) {
	pleaf = (mleaf_t *)node;
	mark = pleaf->firstmarksurface;
	numsurfaces = pleaf->nummarksurfaces;
	if (numsurfaces) {
	    do {
		(*mark)->visframe = r_framecount;
		mark++;
	    } while (--numsurfaces);
	}

	// deal with model fragments in this leaf
	if (pleaf->efrags)
	    R_StoreEfrags(&pleaf->efrags);

	return;
    }

    // node is just a decision point, so go down the apropriate sides
    // find which side of the node we are on
    plane = node->plane;

    switch (plane->type) {
    case PLANE_X:
    case PLANE_Y:
    case PLANE_Z:
	dot = modelorg[plane->type - PLANE_X] - plane->dist;
	break;
    default:
	dot = DotProduct(modelorg, plane->normal) - plane->dist;
	break;
    }

    side = (dot >= 0) ? 0 : 1;

    /* recurse down the children, front side first */
    R_RecursiveWorldNode(modelorg, node->children[side]);

    /* Gather surfaces for drawing */
    numsurfaces = node->numsurfaces;
    if (numsurfaces) {
        msurface_t **materialchains = GLBrushModel(cl.worldmodel)->materialchains;
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (; numsurfaces; numsurfaces--, surf++) {
	    if (surf->visframe != r_framecount)
		continue;

	    // backface cull
	    if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK))
		continue;

            // Frustum cull
            if (R_CullBox(surf->mins, surf->maxs))
                continue;

	    // skip mirror texture if mirror enabled
	    if (mirror && surf->texinfo->texture == cl.worldmodel->textures[mirrortexturenum])
		continue;

            // Add to the material chain for drawing
            surf->chain = materialchains[surf->material];
            materialchains[surf->material] = surf;
	}
    }

    /* recurse down the back side */
    R_RecursiveWorldNode(modelorg, node->children[side ? 0 : 1]);
}


/*
=============
R_DrawWorld
=============
*/
void
R_DrawWorld(void)
{
    int i;
    entity_t worldentity;
    glbrushmodel_t *glbrushmodel;

    memset(&worldentity, 0, sizeof(worldentity));
    worldentity.model = &cl.worldmodel->model;

    currenttexture = -1;
    glColor3f(1, 1, 1);
    glbrushmodel = GLBrushModel(cl.worldmodel);

    if (_gl_drawhull.value) {
	GL_DisableMultitexture();
	glDisable(GL_TEXTURE_2D);
    }

    if (_gl_drawhull.value) {
	switch ((int)_gl_drawhull.value) {
	case 1:
	case 2:
	    /* all preparation done when variable is set */
	    R_DrawWorldHull();
	    break;
	default:
	    /* FIXME: Error? should never happen... */
	    break;
	}
	glEnable(GL_TEXTURE_2D);
	glColor3f(1.0, 1.0, 1.0);
	return;
    }

    /* Build material chains */
    memset(glbrushmodel->materialchains, 0, glbrushmodel->nummaterials * sizeof(msurface_t *));
    R_RecursiveWorldNode(r_refdef.vieworg, cl.worldmodel->nodes);

    /* Add static submodels to the material chains */
    if (r_drawentities.value) {
        for (i = 0; i < cl_numvisedicts; i++) {
            entity_t *entity = &cl_visedicts[i];
            if (entity->model->type != mod_brush)
                continue;
            if (!BrushModel(entity->model)->parent)
                continue;
            if (entity->angles[0] || entity->angles[1] || entity->angles[2])
                continue;
            if (entity->origin[0] || entity->origin[1] || entity->origin[2])
                continue;
            if (R_CullBox(entity->model->mins, entity->model->maxs))
                continue;
            R_AddStaticBrushModelToWorldMaterialChains(entity);
        }
    }
    R_SwapAnimationChains(glbrushmodel, glbrushmodel->materialchains);

    /* Draw! */
    DrawMaterialChains(&worldentity);
}

/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

/*
================
BuildSurfaceDisplayList
================
*/
static void
BuildSurfaceDisplayList(brushmodel_t *brushmodel, msurface_t *surf, void *hunkbase)
{
    const mtexinfo_t *const texinfo = surf->texinfo;
    const float *vertex;
    glpoly_t *poly;
    float s, t;
    int i, memsize;

    /* reconstruct the polygon */
    memsize = sizeof(*poly) + surf->numedges * sizeof(poly->verts[0]);
    poly = Hunk_AllocExtend(hunkbase, memsize);
    poly->next = surf->polys;
    surf->polys = poly;
    poly->numverts = surf->numedges;

    for (i = 0; i < surf->numedges; i++) {
	const int edgenum = brushmodel->surfedges[surf->firstedge + i];
	if (edgenum >= 0) {
	    const medge_t *const edge = &brushmodel->edges[edgenum];
	    vertex = brushmodel->vertexes[edge->v[0]].position;
	} else {
	    const medge_t *const edge = &brushmodel->edges[-edgenum];
	    vertex = brushmodel->vertexes[edge->v[1]].position;
	}
	VectorCopy(vertex, poly->verts[i]);

	/* Texture coordinates */
	s = DotProduct(vertex, texinfo->vecs[0]) + texinfo->vecs[0][3];
	s /= texinfo->texture->width;

	t = DotProduct(vertex, texinfo->vecs[1]) + texinfo->vecs[1][3];
	t /= texinfo->texture->height;

	poly->verts[i][3] = s;
	poly->verts[i][4] = t;

	/* Lightmap texture coordinates */
	s = DotProduct(vertex, texinfo->vecs[0]) + texinfo->vecs[0][3];
	s -= surf->texturemins[0];
	s += surf->light_s * 16;
	s += 8;
	s /= BLOCK_WIDTH * 16;	/* texinfo->texture->width */

	t = DotProduct(vertex, texinfo->vecs[1]) + texinfo->vecs[1][3];
	t -= surf->texturemins[1];
	t += surf->light_t * 16;
	t += 8;
	t /= BLOCK_HEIGHT * 16;	/* texinfo->texture->height */

	poly->verts[i][5] = s;
	poly->verts[i][6] = t;
    }
}

/*
 * Upload modified lightmap blocks
 * Return the number of uploads that were required/done
 */
static void
GL_UploadLightmaps(const glbrushmodel_resource_t *resources)
{
    int i;
    lm_block_t *block;
    qpic8_t pic;

    for (i = 0; i < resources->numblocks; i++) {
	block = &resources->blocks[i];

	block->modified = false;
	block->rectchange.l = BLOCK_WIDTH;
	block->rectchange.t = BLOCK_HEIGHT;
	block->rectchange.w = 0;
	block->rectchange.h = 0;

        if (!block->texture) {
            pic.width = pic.stride = BLOCK_WIDTH;
            pic.height = BLOCK_HEIGHT;
            pic.pixels = block->data;
            block->texture = GL_AllocateTexture(va("@lightmap_%03d", i), &pic, TEXTURE_TYPE_LIGHTMAP);
        }

	GL_Bind(block->texture);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, gl_lightmap_bytes, BLOCK_WIDTH,
		     BLOCK_HEIGHT, 0, gl_lightmap_format, GL_UNSIGNED_BYTE,
		     block->data);
    }
}


/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void
GL_BuildLightmaps()
{
    int i, j;
    model_t *model;
    brushmodel_t *brushmodel;
    glbrushmodel_resource_t *resources;
    msurface_t *surf;
    void *hunkbase = Hunk_AllocName(0, "glpolys");

    for (i = 1; i < MAX_MODELS; i++) {
	model = cl.model_precache[i];
	if (!model)
	    break;
	if (model->type != mod_brush)
	    continue;
	if (model->name[0] == '*')
	    continue;

	brushmodel = BrushModel(model);
	resources = GLBrushModel(brushmodel)->resources;
	surf = brushmodel->surfaces;
	for (j = 0; j < brushmodel->numsurfaces; j++, surf++) {
	    if (surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		continue;

	    byte *base = resources->blocks[surf->lightmapblock].data;
	    base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * gl_lightmap_bytes;
	    R_BuildLightMap(surf, base, BLOCK_WIDTH * gl_lightmap_bytes);
	    BuildSurfaceDisplayList(brushmodel, surf, hunkbase);
	}

	/* upload all lightmaps that were filled */
	GL_UploadLightmaps(resources);
    }
}

void
GL_ReloadLightmapTextures(const glbrushmodel_resource_t *resources)
{
    int i;

    for (i = 0; i < resources->numblocks; i++)
        resources->blocks[i].texture = 0;

    GL_UploadLightmaps(resources);
}
