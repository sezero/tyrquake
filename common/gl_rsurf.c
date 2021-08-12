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

#include <assert.h>
#include <float.h>
#include <math.h>

#include "console.h"
#include "developer.h"
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
	rad -= fabsf(dist);
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

lpActiveTextureFUNC qglActiveTextureARB;
lpClientStateFUNC qglClientActiveTexture;

static qboolean mtexenabled = false;
static GLenum oldtarget = GL_TEXTURE0_ARB;
static int cnttextures[3] = { -1, -1, -1 };	// cached

/*
 * Makes the given texture unit active
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
    if (surf->flags & SURF_DRAWTILED)
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
 * The triangle buffer is either allocated directly as an OpenGL
 * buffer or else on the hunk when buffers are not available.  We
 * can allow up to the full uint16_t max vertices per draw call.
 */
#define TRIBUF_MAX_VERTS MATERIALCHAIN_MAX_VERTS
#define TRIBUF_MAX_INDICES (TRIBUF_MAX_VERTS * 3)

/*
 * Define a reasonable minimum allocation to avoid lots of small
 * re-allocs to grow the buffers.
 */
#define TRIBUF_MIN_VERTS qmin(TRIBUF_MAX_VERTS, 2048)
#define TRIBUF_MIN_INDICES (TRIBUF_MIN_VERTS * 3)

enum tribuf_state {
    TRIBUF_STATE_UNINITIALISED,
    TRIBUF_STATE_PREPARED,
    TRIBUF_STATE_FINALISED,
};

const char *tribuf_state_names[] = {
    [TRIBUF_STATE_UNINITIALISED] = "uninitialised",
    [TRIBUF_STATE_PREPARED]      = "prepared",
    [TRIBUF_STATE_FINALISED]     = "finalised",
};

typedef struct {
    int32_t numverts;
    int32_t numindices;
    uint16_t *indices;
    byte *colors; // Only used for r_drawflat
    vec7_t *verts;
    vec7_t *final_verts;

    const surface_material_t *material;
    uint32_t numverts_allocated;
    uint32_t numindices_allocated;
    int hunk_mark;

    enum tribuf_state state;
#ifdef DEBUG
    const char *file;
    int line;
#endif
} triangle_buffer_t;

#ifdef DEBUG
#define TB_DEBUG_ARGS , const char *file, int line
#define TriBuf_SetState(_buffer, _state) do { \
    (_buffer)->state = (_state); \
    (_buffer)->file = file; \
    (_buffer)->line = line; \
} while (0)
#define TriBuf_Prepare(_buffer, _materialchain) TriBuf_Prepare_(_buffer, _materialchain, __FILE__, __LINE__)
#define TriBuf_Finalise(_buffer) TriBuf_Finalise_(_buffer, __FILE__, __LINE__)
#define TriBuf_Discard(_buffer) TriBuf_Discard_(_buffer, __FILE__, __LINE__)
#define TriBuf_Release(_buffer) TriBuf_Release_(_buffer, __FILE__, __LINE__)
#else
#define TB_DEBUG_ARGS
#define TriBuf_SetState(_buffer, _state) do { \
    (_buffer)->state = (_state); \
} while (0)
#define TriBuf_Prepare(_buffer, _materialchain) TriBuf_Prepare_(_buffer, _materialchain)
#define TriBuf_Finalise(_buffer) TriBuf_Finalise_(_buffer)
#define TriBuf_Discard(_buffer) TriBuf_Discard_(_buffer)
#define TriBuf_Release(_buffer) TriBuf_Release_(_buffer)
#endif

/*
 * Mark the buffer as finalised, indicating no further changes will be made.
 * Required to mark the 'unmap' point when using GL buffer objects.
 */
static void
TriBuf_Finalise_(triangle_buffer_t *buffer TB_DEBUG_ARGS)
{
    assert(buffer->state == TRIBUF_STATE_PREPARED);
    assert(buffer->numverts > 0); // Should discard if unused

    buffer->final_verts = buffer->material ? (vec7_t *)buffer->material->buffer : buffer->verts;

    TriBuf_SetState(buffer, TRIBUF_STATE_FINALISED);
}

/*
 * Reset the triangle buffer and ensure enough space has been
 * allocated for the incoming materialchain.
 */
static void
TriBuf_Prepare_(triangle_buffer_t *buffer, materialchain_t *materialchain TB_DEBUG_ARGS)
{
    assert(TRIBUF_MIN_VERTS <= TRIBUF_MAX_VERTS);

    if (buffer->state == TRIBUF_STATE_PREPARED) {
        Debug_Printf("Already prepared previously at %s:%d\n", buffer->file, buffer->line);
        assert(buffer->state != TRIBUF_STATE_PREPARED);
    }

    /* Reset buffer state */
    buffer->numverts = 0;
    buffer->numindices = 0;
    buffer->material = materialchain->material;
    TriBuf_SetState(buffer, TRIBUF_STATE_PREPARED);

    int32_t numindices = materialchain->numindices;
    int32_t numverts = materialchain->numverts;

    /* Common case should be no re-allocation */
    if (buffer->material && buffer->numindices_allocated >= numindices && !r_drawflat.value)
        return;
    if (buffer->numverts_allocated >= numverts && buffer->numindices_allocated >= numindices)
        return;

    if (buffer->hunk_mark)
        Hunk_FreeToLowMark(buffer->hunk_mark);

    buffer->hunk_mark = Hunk_LowMark();
    buffer->numverts_allocated = qmax(numverts, TRIBUF_MIN_VERTS);
    buffer->numindices_allocated = qmax(numindices, TRIBUF_MIN_INDICES);
    buffer->indices = Hunk_AllocName_Raw(buffer->numindices_allocated * sizeof(uint16_t), "indexbuf");
    buffer->verts = Hunk_AllocName_Raw(buffer->numverts_allocated * sizeof(vec7_t), "vertbuf");
    buffer->colors = r_drawflat.value
        ? Hunk_AllocName_Raw(buffer->numverts_allocated * 3 * sizeof(byte), "colorbuf")
        : NULL;
}

/*
 * Mark the contents of a prepared buffer as not required.  Avoids
 * sync with the GPU when using buffer objects, but allows the
 * allocated memory to be reused in the non-vbo case.
 */
static void
TriBuf_Discard_(triangle_buffer_t *buffer TB_DEBUG_ARGS)
{
    TriBuf_SetState(buffer, TRIBUF_STATE_UNINITIALISED);
}

/*
 * Final free of resources from the buffer.
 * Call this only once when the buffer will no longer be reused.
 */
static void
TriBuf_Release_(triangle_buffer_t *buffer TB_DEBUG_ARGS)
{
    if (buffer->state == TRIBUF_STATE_PREPARED) {
        Debug_Printf("Warning, releasing a prepared buffer! (from %s::%d)\n", file, line);
    }

    if (buffer->hunk_mark)
        Hunk_FreeToLowMark(buffer->hunk_mark);

    TriBuf_Discard(buffer);
}

/*
 * Thin wrappers for the gl*Pointer() functions
 */
static inline void
TriBuf_VertexPointer(triangle_buffer_t *buffer)
{
    assert(buffer->state == TRIBUF_STATE_FINALISED);
    glVertexPointer(3, GL_FLOAT, sizeof(buffer->final_verts[0]), buffer->final_verts);
}

static inline void
TriBuf_TexCoordPointer(triangle_buffer_t *buffer, uint8_t texcoord_index)
{
    assert(buffer->state == TRIBUF_STATE_FINALISED);
    assert(texcoord_index < 2);
    glTexCoordPointer(2, GL_FLOAT, sizeof(buffer->final_verts[0]), &buffer->final_verts[0][3 + 2 * texcoord_index]);
}

static inline void
TriBuf_ColorPointer(triangle_buffer_t *buffer)
{
    assert(buffer->state == TRIBUF_STATE_FINALISED);
    glColorPointer(3, GL_UNSIGNED_BYTE, 0, buffer->colors);
}

/*
 * Vertex Buffer State - Encapsulate all we need to make sure the
 * correct array buffers are active and pointing at the correct
 * offset.  Probably want a better name for this.  Later.
 */
#define VBO_MAX_TEXCOORDS 3
struct buffer_state {
    qboolean color; // true if active
    struct {
        qboolean active; // True or false.  Always interleaved with vertices.
        uint8_t slot;    // 0 or 1 for the two sets of texcoords interleaved in the vbo
    } texcoords[VBO_MAX_TEXCOORDS];
};

/*
 * Set array object pointers for vertices, texcoords and colors
 * according to a given set of desired VBO bindings.
 *
 * TODO: avoid API calls if state hasn't changed
 */
static void
TriBuf_SetBufferState(triangle_buffer_t *buffer, const struct buffer_state *state)
{
    assert(buffer->state == TRIBUF_STATE_FINALISED);

    /* Always set the vertex pointer */
    TriBuf_VertexPointer(buffer);

    /* Set the color pointer, if enabled */
    if (state->color) {
        glEnableClientState(GL_COLOR_ARRAY);
        TriBuf_ColorPointer(buffer);
    } else {
        glDisableClientState(GL_COLOR_ARRAY);
    }

    /* Set texcoord state for each tmu */
    if (gl_mtexable) {
        for (int i = 0; i < VBO_MAX_TEXCOORDS; i++) {
            qglClientActiveTexture(GL_TEXTURE0 + i);
            if (!state->texcoords[i].active) {
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                continue;
            }
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            TriBuf_TexCoordPointer(buffer, state->texcoords[i].slot);
        }
    } else {
        if (!state->texcoords[0].active) {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        } else {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            TriBuf_TexCoordPointer(buffer, state->texcoords[0].slot);
        }
    }
}

int gl_draw_calls;
int gl_verts_submitted;
int gl_indices_submitted;
int gl_full_buffers;

static void
TriBuf_DrawElements(triangle_buffer_t *buffer, const struct buffer_state *state)
{
    assert(buffer->state == TRIBUF_STATE_FINALISED);
    assert(buffer->numindices > 0);

    TriBuf_SetBufferState(buffer, state);
    glDrawElements(GL_TRIANGLES, buffer->numindices, GL_UNSIGNED_SHORT, buffer->indices);

    gl_draw_calls++;
    gl_verts_submitted += buffer->numverts;
    gl_indices_submitted += buffer->numindices;
}

static inline qboolean
TriBuf_CheckSpacePoly(const triangle_buffer_t *buffer, const glpoly_t *poly)
{
    if (buffer->numverts + poly->numverts > buffer->numverts_allocated) {
	gl_full_buffers++;
	return false;
    }
    if (buffer->numindices + poly->numverts > buffer->numindices_allocated) {
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

    vert_bytes = sizeof(poly->verts[0]) * poly->numverts;
    memcpy(&buffer->verts[buffer->numverts], &poly->verts[0], vert_bytes);

    TriBuf_AddPolyIndices(buffer, poly);
}

static inline void
TriBuf_AddSurf(triangle_buffer_t *buffer, const msurface_t *surf)
{
    assert(buffer->material);

    uint16_t *index = &buffer->indices[buffer->numindices];
    const int offset = surf->buffer_offset;
    /* TODO: strip order instead of fan? */
    for (int i = 1; i < surf->numedges - 1; i++) {
        *index++ = offset;
        *index++ = offset + i;
        *index++ = offset + i + 1;
    }

    buffer->numverts += surf->numedges;
    buffer->numindices += (surf->numedges - 2) * 3;
}

static void
TriBuf_AddFlatSurf(triangle_buffer_t *buffer, const msurface_t *surf)
{
    GLbyte color[3];
    int i;

    assert(buffer->material);

    srand((intptr_t)surf);
    color[0] = (byte)(rand() & 0xff);
    color[1] = (byte)(rand() & 0xff);
    color[2] = (byte)(rand() & 0xff);

    byte *dst = &buffer->colors[surf->buffer_offset * 3];
    for (i = 0; i < surf->numedges; i++) {
        *dst++ = color[0];
        *dst++ = color[1];
        *dst++ = color[2];
    }
    TriBuf_AddSurf(buffer, surf);
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
	GL_SelectTexture(GL_TEXTURE0_ARB);
    }

    if (alpha < 1.0f) {
	glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, qmax(alpha, 0.0f));
    }

    GL_Bind(texture->gl_warpimage);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    const struct buffer_state state = {
        .texcoords = { [0] = { .active = true, .slot = 0 } },
    };
    TriBuf_DrawElements(buffer, &state);

    if (alpha < 1.0f) {
	glColor4f(1, 1, 1, 1);
	glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    if (gl_mtexable) {
	GL_EnableMultitexture();
    }
}

static void
TriBuf_DrawFlat(triangle_buffer_t *buffer)
{
    if (gl_mtexable) {
	GL_DisableMultitexture();
    }

    glDisable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    const struct buffer_state state = { .color = true };
    TriBuf_DrawElements(buffer, &state);

    glEnable(GL_TEXTURE_2D);

    if (gl_mtexable) {
	GL_EnableMultitexture();
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

        const struct buffer_state state = {
            .texcoords = {
                [0] = { .active = true, .slot = 0 },
                [1] = { .active = true, .slot = 1 },
            }
        };
	TriBuf_DrawElements(buffer, &state);
    } else {
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	GL_Bind(texture->gl_texturenum);

        const struct buffer_state state = {
            .texcoords = { [0] = { .active = true, .slot = 1 } }
        };
	TriBuf_DrawElements(buffer, &state);

	glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);

        GL_Bind(texture->gl_texturenum_alpha);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        TriBuf_DrawElements(buffer, &state);

        glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
    }

    // TODO: Use single pass with a skyfog texture of the right color/alpha
    if (Fog_GetDensity() > 0 && map_skyfog > 0) {
        GL_DisableMultitexture();
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        const float *color = Fog_GetColor();
        glColor4f(color[0], color[1], color[2], qclamp(map_skyfog, 0.0f, 1.0f));

        const struct buffer_state state = {0};
	TriBuf_DrawElements(buffer, &state);

        glColor4f(1, 1, 1, 1);
        glDisable(GL_BLEND);
        glEnable(GL_TEXTURE_2D);
        GL_EnableMultitexture();
    }
}

static void
TriBuf_DrawFullbrightSolid(triangle_buffer_t *buffer, const texture_t *texture)
{
    if (gl_mtexable) {
        GL_DisableMultitexture();
    }

    GL_Bind(texture->gl_texturenum);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    const struct buffer_state state = {
        .texcoords = { [0] = { .active = true, .slot = 0 } },
    };
    TriBuf_DrawElements(buffer, &state);

    if (gl_mtexable) {
        qglClientActiveTexture(GL_TEXTURE1);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        GL_EnableMultitexture();
    }
}

static void
TriBuf_DrawSolid(triangle_buffer_t *buffer, const texture_t *texture, lm_block_t *block, int flags, float alpha)
{
    if (alpha < 1.0f) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glColor4f(1, 1, 1, alpha);
    }

    if (flags & SURF_DRAWFENCE) {
        glEnable(GL_ALPHA_TEST);
        if (alpha < 1.0f)
            glAlphaFunc(GL_GREATER, 0.666 * alpha);
    }

    if (gl_mtexable) {
	GL_SelectTexture(GL_TEXTURE0_ARB);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	GL_Bind(texture->gl_texturenum);
	GL_SelectTexture(GL_TEXTURE1_ARB);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        GL_Bind(block->texture);
        if (block->modified)
            R_UploadLMBlockUpdate(block);

        struct buffer_state state = {
            .texcoords = {
                [0] = { .active = true, .slot = 0 },
                [1] = { .active = true, .slot = 1 },
            }
        };
        if (gl_num_texture_units > 2 && texture->gl_texturenum_fullbright && gl_fullbrights.value) {
            GL_SelectTexture(GL_TEXTURE2);
            glEnable(GL_TEXTURE_2D);
            glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
            GL_Bind(texture->gl_texturenum_fullbright);
            state.texcoords[2].active = true;
            state.texcoords[2].slot = 0;
        }

	TriBuf_DrawElements(buffer, &state);

        if (gl_num_texture_units > 2 && texture->gl_texturenum_fullbright && gl_fullbrights.value) {
            glDisable(GL_TEXTURE_2D);
            GL_SelectTexture(GL_TEXTURE1);
        }
    } else {
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	GL_Bind(texture->gl_texturenum);

        struct buffer_state state = {
            .texcoords = { [0] = { .active = true, .slot = 0 } }
        };
	TriBuf_DrawElements(buffer, &state);

        /*
         * By using the depth equal test, we automatically mask the
         * lightmap correctly on fence textures
         */
        glDepthFunc(GL_EQUAL);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        Fog_StartBlend();
        GL_Bind(block->texture);
        if (block->modified)
            R_UploadLMBlockUpdate(block);

        state.texcoords[0].slot = 1;
        TriBuf_DrawElements(buffer, &state);

        Fog_StopBlend();

        if (Fog_GetDensity() > 0) {
            /* Extra pass for fog with geometry color set to black */
            glBlendFunc(GL_ONE, GL_ONE);
            glColor3f(0, 0, 0);
            GL_Bind(texture->gl_texturenum);

            state.texcoords[0].slot = 0;
            TriBuf_DrawElements(buffer, &state);

            glColor3f(1, 1, 1);
        }

        glDisable(GL_BLEND);
        glDepthFunc(GL_LEQUAL);
    }

    /* Extra pass for fullbrights */
    if ((!gl_mtexable || gl_num_texture_units < 3) && texture->gl_texturenum_fullbright && gl_fullbrights.value) {
        if (gl_mtexable) {
            GL_DisableMultitexture();
            GL_SelectTexture(GL_TEXTURE0_ARB);
        }

        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GL_Bind(texture->gl_texturenum_fullbright);

        const struct buffer_state state = {
            .texcoords[0] = { .active = true, .slot = 0 },
        };
        TriBuf_DrawElements(buffer, &state);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        if (gl_mtexable) {
            GL_EnableMultitexture();
        }
    }

    if (flags & SURF_DRAWFENCE) {
        if (alpha < 1.0f)
            glAlphaFunc(GL_GREATER, 0.666);
        glDisable(GL_ALPHA_TEST);
    }

    if (alpha < 1.0f) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glColor4f(1, 1, 1, 1);
    }
}

// ---------------------------------------------------------------------
// NEW SKY
// ---------------------------------------------------------------------

/* Sky face corners, in drawing order */
static const vec3_t skyfaces[6][4] = {
    { {  1,  1, -1, }, {  1,  1,  1 }, {  1, -1,  1 }, {  1, -1, -1 } }, // Right (x =>  1)
    { { -1, -1, -1, }, { -1, -1,  1 }, { -1,  1,  1 }, { -1,  1, -1 } }, // Left  (x => -1)
    { { -1,  1, -1, }, { -1,  1,  1 }, {  1,  1,  1 }, {  1,  1, -1 } }, // Back  (y =>  1)
    { {  1, -1, -1, }, {  1, -1,  1 }, { -1, -1,  1 }, { -1, -1, -1 } }, // Front (y => -1)
    { {  1,  1,  1, }, { -1,  1,  1 }, { -1, -1,  1 }, {  1, -1,  1 } }, // Up    (z =>  1)
    { { -1,  1, -1, }, {  1,  1, -1 }, {  1, -1, -1 }, { -1, -1, -1 } }, // Down  (z => -1)
};

/* Indicate which vertices of the sky faces are mins/maxes for bounds checking */
static const int skyfacebounds[6][2] = {
    { 3, 1 },
    { 0, 2 },
    { 0, 2 },
    { 3, 1 },
    { 2, 0 },
    { 3, 1 },
};

typedef struct {
    vec3_t origin;
    vec3_t forward;
    vec3_t right;
    vec3_t up;
    qboolean rotated;
} transform_t;

static void
SetupEntityTransform(const entity_t *entity, transform_t *xfrm)
{
    vec3_t origin;

    if (R_EntityIsRotated(entity)) {
        VectorSubtract(r_refdef.vieworg, entity->origin, origin);
        AngleVectors(entity->angles, xfrm->forward, xfrm->right, xfrm->up);
        xfrm->origin[0] = DotProduct(origin, xfrm->forward);
        xfrm->origin[1] = DotProduct(origin, xfrm->right);
        xfrm->origin[2] = DotProduct(origin, xfrm->up);
        xfrm->rotated = true;
    } else {
        VectorSubtract(r_refdef.vieworg, entity->origin, xfrm->origin);
        xfrm->rotated = false;
    }
}

static inline void
BoundsInit(vec3_t mins, vec3_t maxs)
{
    mins[0] = mins[1] = mins[2] = FLT_MAX;
    maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
}

static inline void
BoundsAddPoint(const vec3_t point, vec3_t mins, vec3_t maxs)
{
    int i;

    for (i = 0; i < 3; i++) {
        if (point[i] < mins[i])
            mins[i] = point[i];
        if (point[i] > maxs[i])
            maxs[i] = point[i];
    }
}

/*
 * Transform the polygon and return bounds
 */
static void
TransformPoly(const entity_t *entity, const transform_t *xfrm,
              const glpoly_t *inpoly, glpoly_t *outpoly, vec3_t mins, vec3_t maxs)
{
    const float *in = inpoly->verts[0];
    float *out = outpoly->verts[0];
    int i;

    BoundsInit(mins, maxs);
    outpoly->numverts = inpoly->numverts;
    if (xfrm->rotated) {
        for (i = 0; i < inpoly->numverts; i++, in += VERTEXSIZE, out += VERTEXSIZE) {
            out[0] = entity->origin[0] + in[0] * xfrm->forward[0] - in[1] * xfrm->right[0] + in[2] * xfrm->up[0];
            out[1] = entity->origin[1] + in[0] * xfrm->forward[1] - in[1] * xfrm->right[1] + in[2] * xfrm->up[1];
            out[2] = entity->origin[2] + in[0] * xfrm->forward[2] - in[1] * xfrm->right[2] + in[2] * xfrm->up[2];
            BoundsAddPoint(out, mins, maxs);
        }
    } else {
        for (i = 0; i < inpoly->numverts; i++, in += VERTEXSIZE, out += VERTEXSIZE) {
            VectorAdd(in, entity->origin, out);
            BoundsAddPoint(out, mins, maxs);
        }
    }
}

/*
 * Check for a sky material on the brushmodel.
 * Return the material number, or -1 if none exists.
 */
static inline int
SkyMaterialNum(const glbrushmodel_t *glbrushmodel)
{
    int materialnum = glbrushmodel->material_index[MATERIAL_SKY];
    if (materialnum == glbrushmodel->material_index[MATERIAL_SKY + 1])
        materialnum = -1;
    return materialnum;
}

static void
DrawSkyChain_RenderSkyBrushPolys(triangle_buffer_t *buffer, materialchain_t *materialchain, const struct buffer_state *state, float mins[6][2], float maxs[6][2])
{
    int i, maxverts, skymaterial;
    glpoly_t *poly;
    transform_t xfrm;
    vec3_t polymins, polymaxs;
    glbrushmodel_t *glbrushmodel;

    ForEach_MaterialChain(materialchain) {
        TriBuf_Prepare(buffer, materialchain);
        if (mins) {
            for (msurface_t *surf = materialchain->surf ; surf; surf = surf->chain) {
                Sky_AddPolyToSkyboxBounds(surf->poly, mins, maxs);
                TriBuf_AddSurf(buffer, surf);
            }
        } else {
            for (msurface_t *surf = materialchain->surf ; surf; surf = surf->chain)
                TriBuf_AddSurf(buffer, surf);
        }
        TriBuf_Finalise(buffer);
        TriBuf_DrawElements(buffer, state);
    }

    /*
     * If there are (dynamic) submodels with sky surfaces, draw them now
     * TODO: Speed up accounting frustum culled visedicts and so forth in one place
     */
    maxverts = GLBrushModel(cl.worldmodel)->resources->max_submodel_skypoly_verts;
    if (maxverts && r_drawentities.value) {
        materialchain_t dummy = {
            .numverts = TRIBUF_MIN_VERTS,
            .numindices = TRIBUF_MIN_INDICES,
        };
        TriBuf_Prepare(buffer, &dummy);

        poly = alloca(sizeof(*poly) + maxverts * sizeof(poly->verts[0]));
        for (i = 0; i < cl_numvisedicts; i++) {
            entity_t *entity = cl_visedicts[i];
            if (entity->model->type != mod_brush)
                continue;
            if (!BrushModel(entity->model)->parent)
                continue;
            glbrushmodel = GLBrushModel(BrushModel(entity->model));
            if (!glbrushmodel->drawsky)
                continue;
            if (!R_EntityIsTranslated(entity) && !R_EntityIsRotated(entity))
                continue;

            skymaterial = SkyMaterialNum(glbrushmodel);
            if (skymaterial < 0)
                continue;
            msurface_t *surf = glbrushmodel->materialchains[skymaterial].surf;
            if (!surf)
                continue;

            SetupEntityTransform(entity, &xfrm);
            for ( ; surf; surf = surf->chain) {
                float dot = DotProduct(xfrm.origin, surf->plane->normal) - surf->plane->dist;
                if ((surf->flags & SURF_PLANEBACK) && dot > BACKFACE_EPSILON)
                    continue;
                if (!(surf->flags & SURF_PLANEBACK) && dot < -BACKFACE_EPSILON)
                    continue;
                TransformPoly(entity, &xfrm, surf->poly, poly, polymins, polymaxs);
                if (R_CullBox(polymins, polymaxs))
                    continue;
                if (mins)
                    Sky_AddPolyToSkyboxBounds(poly, mins, maxs);
                if (!TriBuf_CheckSpacePoly(buffer, poly)) {
                    TriBuf_DrawElements(buffer, state);
                    TriBuf_Prepare(buffer, &dummy);
                }
                TriBuf_AddPoly(buffer, poly);
            }
        }
    }
    if (buffer->numverts)
        TriBuf_DrawElements(buffer, state);
    else
        TriBuf_Discard(buffer);
}

static void
DrawSkyChain_MarkDepthAndBounds(triangle_buffer_t *buffer, materialchain_t *materialchain, float mins[6][2], float maxs[6][2])
{
    Sky_InitBounds(mins, maxs);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    if (gl_mtexable) {
        GL_DisableMultitexture();
    }
    glDisable(GL_TEXTURE_2D);

    /* Render to the depth buffer and accumulate bounds */
    const struct buffer_state state = {0};
    DrawSkyChain_RenderSkyBrushPolys(buffer, materialchain, &state, mins, maxs);

    glEnable(GL_TEXTURE_2D);
    if (gl_mtexable) {
        GL_EnableMultitexture();
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

static void
DrawSkyFast(triangle_buffer_t *buffer, materialchain_t *materialchain)
{
    /* Render simple flat shaded polys */
    if (gl_mtexable) {
        GL_DisableMultitexture();
    }
    glDisable(GL_TEXTURE_2D);

    glColor3fv(skyflatcolor);

    /* Simply render the sky brush polys, no bounds checking needed */
    const struct buffer_state state = {0};
    DrawSkyChain_RenderSkyBrushPolys(buffer, materialchain, &state, NULL, NULL);

    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    if (gl_mtexable) {
        GL_EnableMultitexture();
    }
}

static void
DrawSkyBox(triangle_buffer_t *buffer, materialchain_t *materialchain)
{
    int facenum;
    float mins[6][2];
    float maxs[6][2];

    /*
     * Write depth values for the actual sky polys.
     * Calculate bounds information at the same time.
     */
    DrawSkyChain_MarkDepthAndBounds(buffer, materialchain, mins, maxs);

    /* Render the sky box only where we have written depth values */
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GEQUAL);

    if (gl_mtexable) {
        GL_DisableMultitexture();
    }

    const float scale = gl_farclip.value / sqrtf(3.0f);
    const struct buffer_state state = {
        .texcoords = { [0] = { .active = true, .slot = 0 } }
    };

    vec7_t verts[4];
    for (facenum = 0; facenum < 6; facenum++) {
        /* Cull skybox faces covered by geometry */
        if (mins[facenum][0] >= maxs[facenum][0] || mins[facenum][1] >= maxs[facenum][1])
            continue;

        float *vert = verts[0];

        VectorMA(r_origin, scale, skyfaces[facenum][0], vert);
        vert[3] = 0;
        vert[4] = 1;
        vert += VERTEXSIZE;

        VectorMA(r_origin, scale, skyfaces[facenum][1], vert);
        vert[3] = 0;
        vert[4] = 0;
        vert += VERTEXSIZE;

        VectorMA(r_origin, scale, skyfaces[facenum][2], vert);
        vert[3] = 1;
        vert[4] = 0;
        vert += VERTEXSIZE;

        VectorMA(r_origin, scale, skyfaces[facenum][3], vert);
        vert[3] = 1;
        vert[4] = 1;
        vert += VERTEXSIZE;

        /* Cull skybox faces outside the view frustum */
        if (R_CullBox(verts[skyfacebounds[facenum][0]], verts[skyfacebounds[facenum][1]]))
            continue;

        materialchain_t dummy = {
            .numverts = 4,
            .numindices = 6,
        };
        TriBuf_Prepare(buffer, &dummy);
        memcpy(buffer->verts, verts, sizeof(verts));

        unsigned short *indices = &buffer->indices[0];
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        indices[3] = 0;
        indices[4] = 2;
        indices[5] = 3;

        buffer->numverts   = 4;
        buffer->numindices = 6;

        TriBuf_Finalise(buffer);

        GL_Bind(skytextures[facenum].gl_texturenum);
        TriBuf_DrawElements(buffer, &state);

        // TODO: Use single pass with a skyfog texture of the right color/alpha
        if (Fog_GetDensity() > 0 && map_skyfog > 0) {
            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            const float *color = Fog_GetColor();
            glColor4f(color[0], color[1], color[2], qclamp(map_skyfog, 0.0f, 1.0f));

            TriBuf_DrawElements(buffer, &state);

            glColor4f(1, 1, 1, 1);
            glDisable(GL_BLEND);
            glEnable(GL_TEXTURE_2D);
        }
    }

    if (gl_mtexable) {
        GL_EnableMultitexture();
    }

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
}

static inline void
TriBuf_AddSkyVert(triangle_buffer_t *buffer, const vec3_t vert, float speed1, float speed2)
{
    vec3_t dir;
    float length;

    VectorSubtract(vert, r_origin, dir);
    dir[2] *= 3; // flatten the sphere
    length = DotProduct(dir, dir);
    length = sqrtf(length);
    length = 6 * 63 / length;
    dir[0] *= length;
    dir[1] *= length;

    float *dst = buffer->verts[buffer->numverts++];
    VectorCopy(vert, dst);
    dst[3] = (speed1 + dir[0]) * (1.0f / 128.0f);
    dst[4] = (speed1 + dir[1]) * (1.0f / 128.0f);
    dst[5] = (speed2 + dir[0]) * (1.0f / 128.0f);
    dst[6] = (speed2 + dir[1]) * (1.0f / 128.0f);
}

/* DEBUG_SKY is helpful to see how well the skybox culling is working */
#define DEBUG_SKY 0
#define SKYCLIP_EPSILON 0.0001f

/*
 * Because we set up the vertices in rows rather than one triangle at
 * a time, we need to be able to fix at least one row of sky triangles
 * into the buffer.  This should never be a problem, unless
 * artificially restricting the size for debugging purposes.
 */
#define MAX_SKY_QUALITY (TRIBUF_MAX_INDICES / 6 < 64 ? TRIBUF_MAX_INDICES / 6 : 64)

static void
DrawSkyLayers(triangle_buffer_t *buffer, materialchain_t *materialchain, texture_t *texture)
{
    int facenum, subdivisions;
    int s, s_start, s_end, t, t_start, t_end;
    float ds, dt, t_scale;
    float speed1, speed2;
    float facescale = gl_farclip.value / sqrtf(3.0f);

    vec3_t faceverts[4];
    vec3_t baseline_edge_verts[2];
    vec3_t svec, tvec;
    vec3_t vert;
    vec3_t t_offset;
    float mins[6][2];
    float maxs[6][2];

    /*
     * Write depth values for the actual sky polys.
     * Calculate bounds information at the same time.
     */
    DrawSkyChain_MarkDepthAndBounds(buffer, materialchain, mins, maxs);

    /* Render the sky box only where we have written depth values */
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GEQUAL);

    speed1 = realtime * 8;
    speed1 -= (int)speed1 & ~127;
    speed2 = realtime * 16;
    speed2 -= (int)speed2 & ~127;

    /* Subdivide the skybox face so we can simulate the sky curvature. */
    subdivisions = qclamp((int)r_sky_quality.value, 1, MAX_SKY_QUALITY);
    ds = 1.0f / subdivisions;

    vec3_t *baseverts = alloca((subdivisions + 1) * sizeof(vec3_t));

    materialchain_t dummy = {
        .numverts = TRIBUF_MIN_VERTS,
        .numindices = TRIBUF_MIN_INDICES,
    };
    TriBuf_Prepare(buffer, &dummy);

    for (facenum = 0; facenum < 6; facenum++) {
    nextFace:
        /* Construct the skybox face vertices */
        VectorMA(r_origin, facescale, skyfaces[facenum][0], faceverts[0]);
        VectorMA(r_origin, facescale, skyfaces[facenum][1], faceverts[1]);
        VectorMA(r_origin, facescale, skyfaces[facenum][2], faceverts[2]);
        VectorMA(r_origin, facescale, skyfaces[facenum][3], faceverts[3]);

        /* Cull skybox faces outside viewable areas */
        if (R_CullBox(faceverts[skyfacebounds[facenum][0]], faceverts[skyfacebounds[facenum][1]]))
            continue;
        if (mins[facenum][0] >= maxs[facenum][0] || mins[facenum][1] >= maxs[facenum][1])
            continue;

        /* Find which divisions are visible in s, if none early out */
        s_end = subdivisions + 1;
        for (s = 0; s < s_end; s++)
            if (ds * (s + 1) > mins[facenum][0] / 2.0f + 0.5f - SKYCLIP_EPSILON)
                break;
        s_start = s;
        for (s = s_start + 1; s < s_end; s++)
            if (ds * (s - 1) > maxs[facenum][0] / 2.0f + 0.5f + SKYCLIP_EPSILON)
                break;
        s_end = s;
        if (s_start >= s_end)
            continue;

        /* Construct S/T vectors for the face */
        VectorSubtract(faceverts[2], faceverts[1], svec);
        VectorSubtract(faceverts[2], faceverts[3], tvec);

        /*
         * Setup subdivisions in T.  For the vertical faces, we make
         * extra sub-divisions and focus the detail at the horizon
         * where more is needed.
         *
         * The spacings for the vertical subdivisions use the sequence
         * of 'triangular' numbers (sum of first 'n' integers, e.g. 1,
         * 3, 6, 10, 15, ...), which worked out better than square
         * numbers due to diverging less quickly.
         *
         * Baseverts verticies are the ones we use to project out each
         * row by adding a multiple of dt to the t component.  For
         * vertical faces the baseverts are in the centre of the face
         * vertically, but for the top/bottom faces they are on the
         * zero edge.
         */
        if (facenum < 4) {
            dt = 0.5f / (subdivisions * (subdivisions - 1) / 2);
            t_start = -subdivisions;
            t_end = subdivisions + 1;
            VectorMA(faceverts[0], 0.5f, tvec, baseline_edge_verts[0]);
            VectorMA(faceverts[3], 0.5f, tvec, baseline_edge_verts[1]);
        } else {
            dt = 1.0f / subdivisions;
            t_start = 0;
            t_end = subdivisions + 1;
            VectorCopy(faceverts[0], baseline_edge_verts[0]);
            VectorCopy(faceverts[3], baseline_edge_verts[1]);
        }

        /* Find which divisions are visible in t, if none early out */
        for (t = t_start; t < t_end; t++) {
            float next = (facenum < 4) ? dt * ((t + 1) * (abs(t + 1) - 1) / 2) : dt * (t + 1) - 0.5f;
            if (next > mins[facenum][1] / 2.0f - SKYCLIP_EPSILON)
                break;
        }
        t_start = t;
        for (t = t_start + 1; t < t_end; t++) {
            float prev = (facenum < 4) ? dt * ((t - 1) * (abs(t - 1) - 1) / 2) : dt * (t - 1) - 0.5f;
            if (prev > maxs[facenum][1] / 2.0f + SKYCLIP_EPSILON)
                break;
        }
        t_end = t;
        if (t_start >= t_end)
            continue;

        /* Set up the base verticies - each row will offset from these */
        for (s = s_start; s < s_end; s++)
            VectorMA(baseline_edge_verts[0], ds * s, svec, baseverts[s]);
        VectorCopy(baseline_edge_verts[1], baseverts[s]);

    rowStart:
        /* Add the first row of vertices to the buffer */
        t_scale = (facenum < 4) ? dt * (t_start * (abs(t_start) - 1) / 2) : dt * t_start;
        t_start++;

        /* Add the first row of vertices */
        VectorScale(tvec, t_scale, t_offset);
        for (s = s_start; s < s_end; s++) {
            VectorAdd(baseverts[s], t_offset, vert);
            TriBuf_AddSkyVert(buffer, vert, speed1, speed2);
        }

        uint16_t *index = buffer->indices + buffer->numindices;
        for (t = t_start; t < t_end; t++) {
            t_scale = (facenum < 4) ? dt * (t * (abs(t) - 1) / 2) : dt * t;
            VectorScale(tvec, t_scale, t_offset);

            /* Add the first vertex in the row separately, no complete quads yet... */
            VectorAdd(baseverts[s_start], t_offset, vert);
            TriBuf_AddSkyVert(buffer, vert, speed1, speed2);

            for (s = s_start + 1; s < s_end; s++) {
                VectorAdd(baseverts[s], t_offset, vert);
                TriBuf_AddSkyVert(buffer, vert, speed1, speed2);

                /*
                 * Now, add the six indicies for the two triangles in
                 * the quad that this vertex completed.
                 */
                *index++ = buffer->numverts - 2 - (s_end - s_start); // previous row, left
                *index++ = buffer->numverts - 2;                     // this row, left
                *index++ = buffer->numverts - 1 - (s_end - s_start); // previous row, right
#if !DEBUG_SKY
                *index++ = buffer->numverts - 1 - (s_end - s_start); // previous row, right
                *index++ = buffer->numverts - 2;                     // this row, left
                *index++ = buffer->numverts - 1;                     // this row, right
#endif
            }
            buffer->numindices = index - buffer->indices;
            if (buffer->numindices + (s_end - s_start) * 6 > buffer->numverts_allocated) {
                gl_full_buffers++;
                goto drawBuffer;
            }
        }
    }
    if (buffer->numindices) {
 drawBuffer:
        TriBuf_Finalise(buffer);
        TriBuf_DrawSky(buffer, texture);

        if (t < t_end - 1) {
            t_start = t;
            TriBuf_Prepare(buffer, &dummy);
            goto rowStart;
        }
        if (facenum < 5) {
            facenum++;
            TriBuf_Prepare(buffer, &dummy);
            goto nextFace;
        }
    } else {
        TriBuf_Discard(buffer);
    }

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
}

static void
DrawSkyChain(triangle_buffer_t *buffer, materialchain_t *materialchain, texture_t *texture)
{
    Fog_DisableGlobalFog();

    if (r_fastsky.value)
        DrawSkyFast(buffer, materialchain);
    else if (map_skyboxname[0])
        DrawSkyBox(buffer, materialchain);
    else
        DrawSkyLayers(buffer, materialchain, texture);

    Fog_EnableGlobalFog();
}

static void
DrawTurbChain(triangle_buffer_t *buffer, materialchain_t *materialchain, texture_t *texture, float alpha)
{
    /* Init the correct alpha value */
    msurface_t *surf = materialchain->surf;
    if (surf->flags & SURF_DRAWWATER)
        alpha *= map_wateralpha;
    else if (surf->flags & SURF_DRAWSLIME)
        alpha *= map_slimealpha;
    else if (surf->flags & SURF_DRAWLAVA)
        alpha *= map_lavaalpha;
    else if (surf->flags & SURF_DRAWTELE)
        alpha *= map_telealpha;

    ForEach_MaterialChain(materialchain) {
        TriBuf_Prepare(buffer, materialchain);
        for (surf = materialchain->surf; surf; surf = surf->chain)
            TriBuf_AddSurf(buffer, surf);
        TriBuf_Finalise(buffer);
        TriBuf_DrawTurb(buffer, texture, alpha);
    }
}

static void
DrawFlatChain(triangle_buffer_t *buffer, materialchain_t *materialchain)
{
    ForEach_MaterialChain(materialchain) {
        TriBuf_Prepare(buffer, materialchain);
        for (msurface_t *surf = materialchain->surf ; surf; surf = surf->chain)
            TriBuf_AddFlatSurf(buffer, surf);
        TriBuf_Finalise(buffer);
        TriBuf_DrawFlat(buffer);
    }
}

static void
DrawSolidChain(triangle_buffer_t *buffer, materialchain_t *materialchain, glbrushmodel_t *glbrushmodel, float alpha)
{
    texture_t *texture = glbrushmodel->brushmodel.textures[materialchain->material->texturenum];
    lm_block_t *block = &glbrushmodel->resources->blocks[materialchain->material->lightmapblock];
    int flags = materialchain->surf->flags;

    ForEach_MaterialChain(materialchain) {
        TriBuf_Prepare(buffer, materialchain);
        for (msurface_t *surf = materialchain->surf; surf; surf = surf->chain) {
            R_UpdateLightmapBlockRect(glbrushmodel->resources, surf);
            TriBuf_AddSurf(buffer, surf);
        }
        TriBuf_Finalise(buffer);
        if (flags & SURF_DRAWTILED) {
            TriBuf_DrawFullbrightSolid(buffer, texture);
        } else {
            TriBuf_DrawSolid(buffer, texture, block, flags, alpha);
        }
    }
}

static void
GL_BeginMaterialChains()
{
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
}

static void
GL_EndMaterialChains()
{
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

static void
DrawMaterialChain(triangle_buffer_t *buffer, glbrushmodel_t *glbrushmodel, materialchain_t *materialchain, float alpha)
{
    int flags = materialchain->surf->flags;

    // If drawflat enabled, all surfs go through the same path
    if (r_drawflat.value) {
        DrawFlatChain(buffer, materialchain);
        return;
    }

#if DEBUG_SKY
    // DEBUG: SKY LAST
    if (flags & SURF_DRAWSKY)
        return;
#endif
    if (flags & SURF_DRAWSKY) {
        texture_t *texture = glbrushmodel->brushmodel.textures[materialchain->material->texturenum];
        DrawSkyChain(buffer, materialchain, texture);
    } else if (flags & SURF_DRAWTURB) {
        texture_t *texture = glbrushmodel->brushmodel.textures[materialchain->material->texturenum];
        DrawTurbChain(buffer, materialchain, texture, alpha);
    } else {
        DrawSolidChain(buffer, materialchain, glbrushmodel, alpha);
    }
}

static void
DrawMaterialChains(const entity_t *entity)
{
    int i;
    float alpha;
    materialchain_t *materialchain;
    brushmodel_t *brushmodel;
    glbrushmodel_t *glbrushmodel;
    surface_material_t *material;
    triangle_buffer_t buffer = {0};

    GL_BeginMaterialChains();

    // MATERIAL CHAIN
    brushmodel = BrushModel(entity->model);
    glbrushmodel = GLBrushModel(brushmodel);
    material = glbrushmodel->materials;
    alpha = ENTALPHA_DECODE(entity->alpha);
    for (i = 0; i < glbrushmodel->nummaterials; i++, material++) {
	materialchain = &glbrushmodel->materialchains[i];
	if (!materialchain->surf)
	    continue;

        // Skip transparent liquid surfaces for drawing last
        int flags = materialchain->surf->flags;
        if (flags & r_surfalpha_flags) {
            TriBuf_Release(&buffer);
            return;
        }

        DrawMaterialChain(&buffer, glbrushmodel, materialchain, alpha);
    }

#if DEBUG_SKY
    // DEBUG: SKY LAST
    material = glbrushmodel->materials;
    for (i = 0; i < glbrushmodel->nummaterials; i++, material++) {
	materialchain = &glbrushmodel->materialchains[i];
	if (!materialchain->surf)
	    continue;
	int flags = materialchain->surf->flags;
        if (!(flags & SURF_DRAWSKY))
            continue;
	texture_t *texture = brushmodel->textures[material->texturenum];
        TriBuf_Prepare(&buffer, materialchain);
        DrawSkyChain(&buffer, materialchain, texture);
        break;
    }
#endif

    TriBuf_Release(&buffer);
    GL_EndMaterialChains();
}

void
R_DrawTranslucentChain(entity_t *entity, materialchain_t *materialchain, float alpha)
{
    triangle_buffer_t buffer = {0};
    brushmodel_t *brushmodel = BrushModel(entity->model);
    glbrushmodel_t *glbrushmodel = GLBrushModel(brushmodel);

    GL_BeginMaterialChains();

    // TODO: lift the buffer out so we don't re-alloc for each translucent render
    DrawMaterialChain(&buffer, glbrushmodel, materialchain, alpha);
    TriBuf_Release(&buffer);

    GL_EndMaterialChains();
}

/*
 * Adds the surfaces from the brushmodel to the given material chains
 * Allows us to add submodels to the world material chains if they are static.
 */
static void
R_AddBrushModelToMaterialChains(entity_t *entity, const vec3_t modelorg, materialchain_t *materialchains, depthchain_type_t type)
{
    int i;
    msurface_t *surf;
    brushmodel_t *brushmodel = BrushModel(entity->model);

    /* Gather visible surfaces */
    surf = &brushmodel->surfaces[brushmodel->firstmodelsurface];
    for (i = 0; i < brushmodel->nummodelsurfaces; i++, surf++) {
        mplane_t *plane = surf->plane;
        float dot = DotProduct(modelorg, plane->normal) - plane->dist;
        if ((surf->flags & SURF_PLANEBACK) && dot >= -BACKFACE_EPSILON)
            continue;
        if (!(surf->flags & SURF_PLANEBACK) && dot <= BACKFACE_EPSILON)
            continue;
        if (surf->flags & r_surfalpha_flags) {
            DepthChain_AddSurf(&r_depthchain, entity, surf, type);
        } else {
            MaterialChain_AddSurf(&materialchains[surf->material], surf);
        }
    }
}

static void
R_AddBrushModelToDepthChain(depthchain_t *depthchain, entity_t *entity, vec3_t modelorg, depthchain_type_t type)
{
    int i;
    msurface_t *surf;
    brushmodel_t *brushmodel = BrushModel(entity->model);

    surf = &brushmodel->surfaces[brushmodel->firstmodelsurface];
    for (i = 0; i < brushmodel->nummodelsurfaces; i++, surf++) {
        mplane_t *plane = surf->plane;
        float dot = DotProduct(modelorg, plane->normal) - plane->dist;
        if ((surf->flags & SURF_PLANEBACK) && dot >= -BACKFACE_EPSILON)
            continue;
        if (!(surf->flags & SURF_PLANEBACK) && dot <= BACKFACE_EPSILON)
            continue;
        DepthChain_AddSurf(&r_depthchain, entity, surf, type);
    }
}

static inline void
SwapChains(int material1, int material2, materialchain_t *materialchains)
{
    if (material1 != material2) {
        materialchain_t tmp = materialchains[material1];
        materialchains[material1] = materialchains[material2];
        materialchains[material2] = tmp;
    }
}

/*
 * Simple swap of animation materials when no swapping between alt frames is involved
 */
static inline void
R_SwapAnimationChains(const glbrushmodel_t *glbrushmodel, materialchain_t *materialchains)
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
R_SwapAltAnimationChains(const entity_t *entity, materialchain_t *materialchains)
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
R_SetupDynamicBrushModelMaterialChains(entity_t *entity, const vec3_t modelorg)
{
    brushmodel_t *brushmodel = BrushModel(entity->model);
    glbrushmodel_t *glbrushmodel = GLBrushModel(brushmodel);
    materialchain_t *materialchains = glbrushmodel->materialchains;

    MaterialChains_Init(materialchains, glbrushmodel->materials, glbrushmodel->nummaterials);

    R_AddBrushModelToMaterialChains(entity, modelorg, materialchains, depthchain_bmodel_transformed);
    R_SwapAltAnimationChains(entity, materialchains);
    R_SwapAnimationChains(glbrushmodel, materialchains);

    /* Sky is handled separately */
    int skymaterial = SkyMaterialNum(glbrushmodel);
    if (skymaterial >= 0)
        materialchains[skymaterial].surf = NULL;
}

static void
R_AddStaticBrushModelToWorldMaterialChains(entity_t *entity)
{
    brushmodel_t *brushmodel = BrushModel(entity->model);
    materialchain_t *materialchains = GLBrushModel(brushmodel->parent)->materialchains;

    R_SwapAltAnimationChains(entity, materialchains);
    R_AddBrushModelToMaterialChains(entity, r_refdef.vieworg, materialchains, depthchain_bmodel_static);
    R_SwapAltAnimationChains(entity, materialchains);
}

/*
=================
R_DrawDynamicBrushModel
=================
*/
void
R_DrawDynamicBrushModel(entity_t *entity)
{
    int i;
    vec3_t mins, maxs, angles_bug, modelorg;
    model_t *model;
    brushmodel_t *brushmodel;
    qboolean rotated = false;
    float alpha;

    model = entity->model;
    brushmodel = BrushModel(model);
    alpha = ENTALPHA_DECODE(entity->alpha);

    if (_debug_models.value)
        DEBUG_DrawModelInfo(entity, entity->origin);

    /*
     * Static (non-rotated/translated/translucent) models are drawn
     * with the world, so we skip them here.
     */
    if (R_EntityIsRotated(entity))
	rotated = true;
    else if (brushmodel->parent && !R_EntityIsTranslated(entity)) {
        if (alpha < 1.0f) {
            VectorSubtract(r_refdef.vieworg, entity->origin, modelorg);
            R_AddBrushModelToDepthChain(&r_depthchain, entity, modelorg, depthchain_bmodel_static);
        }
        return;
    }

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

    if (!brushmodel->parent && alpha < 1.0f) {
        /* No parent so it's an instanced bmodel */
        DepthChain_AddEntity(&r_depthchain, entity, depthchain_bmodel_instanced);
        return;
    }

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

    if (alpha < 1.0f) {
        if (brushmodel->parent)
            R_AddBrushModelToDepthChain(&r_depthchain, entity, modelorg, depthchain_bmodel_transformed);
        return;
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
 * We need to handle instanced bmodels individually, since we can't
 * put the surfaces directly into the depth chain.
 */
void
R_DrawInstancedTranslucentBmodel(entity_t *entity)
{
    vec3_t modelorg, angles_bug;

    VectorSubtract(r_refdef.vieworg, entity->origin, modelorg);
    if (R_EntityIsRotated(entity)) {
        vec3_t temp, forward, right, up;

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
		(*mark)->visframe = r_visframecount;
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
        materialchain_t *materialchains = GLBrushModel(cl.worldmodel)->materialchains;
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (; numsurfaces; numsurfaces--, surf++) {
	    if (surf->visframe != r_visframecount)
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

            /* Flag turb textures that need to be updated */
            if (surf->flags & SURF_DRAWTURB)
                surf->texinfo->texture->mark = 1;

            if (surf->flags & r_surfalpha_flags) {
                // Sort alpha surfaces into the depth chain for blending
                DepthChain_AddSurf(&r_depthchain, &r_worldentity, surf, depthchain_bmodel_static);
            } else {
                // Add to the material chain for batch drawing
                MaterialChain_AddSurf(&materialchains[surf->material], surf);
            }
        }
    }

    /* recurse down the back side */
    R_RecursiveWorldNode(modelorg, node->children[side ? 0 : 1]);
}

/*
 * Pre-mark all the world surfaces that need to be drawn
 */
void
R_PrepareWorldMaterialChains()
{
    glbrushmodel_t *glbrushmodel;

    glbrushmodel = GLBrushModel(cl.worldmodel);
    MaterialChains_Init(glbrushmodel->materialchains, glbrushmodel->materials, glbrushmodel->nummaterials);
    R_RecursiveWorldNode(r_refdef.vieworg, cl.worldmodel->nodes);
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

    /*
     * Add static submodels to the material chains.
     * Setup sky chains on dynamic models.
     *
     * TODO: Reorder RenderScene so we only walk visedicts once for
     *       both this and for R_DrawEntitiesOnList
     */
    if (r_drawentities.value) {
        for (i = 0; i < cl_numvisedicts; i++) {
            entity_t *entity = cl_visedicts[i];
            if (entity->model->type != mod_brush)
                continue;
            if (!BrushModel(entity->model)->parent)
                continue;

            /* Setup sky materialchains on transformed bmodels */
            if (R_EntityIsTranslated(entity) || R_EntityIsRotated(entity)) {
                brushmodel_t *brushmodel = BrushModel(entity->model);
                glbrushmodel_t *glbrushmodel = GLBrushModel(brushmodel);
                if (!glbrushmodel->drawsky)
                    continue;
                int skymaterial = SkyMaterialNum(glbrushmodel);
                if (skymaterial < 0)
                    continue;
                materialchain_t *materialchains = glbrushmodel->materialchains;
                msurface_t *surf = &brushmodel->surfaces[brushmodel->firstmodelsurface];
                int i;
                materialchains[skymaterial].surf = NULL;
                for (i = 0; i < brushmodel->nummodelsurfaces; i++, surf++) {
                    if (!(surf->flags & SURF_DRAWSKY))
                        continue;
                    MaterialChain_AddSurf(&materialchains[skymaterial], surf);

                    /* Flag turb textures that need to be updated */
                    if (surf->flags & SURF_DRAWTURB)
                        surf->texinfo->texture->mark = 1;
                }
                continue;
            }

            /* Skip alpha entities on this pass (but still draw sky, above) */
            float alpha = ENTALPHA_DECODE(entity->alpha);
            if (alpha < 1.0f)
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
================
BuildSurfaceGLPoly
================
*/
static void
BuildSurfaceGLPoly(brushmodel_t *brushmodel, msurface_t *surf, void *hunkbase)
{
    const mtexinfo_t *const texinfo = surf->texinfo;
    const float *vertex;
    glpoly_t *poly;
    float s, t;
    int i, memsize;

    /* reconstruct the polygon */
    memsize = sizeof(*poly) + surf->numedges * sizeof(poly->verts[0]);
    poly = Hunk_AllocExtend(hunkbase, memsize);
    surf->poly = poly;
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
        if (surf->flags & SURF_DRAWSKY)
            continue;

	/* Texture coordinates */
	s = DotProduct(vertex, texinfo->vecs[0]) + texinfo->vecs[0][3];
	t = DotProduct(vertex, texinfo->vecs[1]) + texinfo->vecs[1][3];
        if (surf->flags & SURF_DRAWTURB) {
            s /= 128.0f;
            t /= 128.0f;
        } else {
            s /= texinfo->texture->width;
            t /= texinfo->texture->height;
        }

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
void
GL_UploadLightmaps(const model_t *model, const glbrushmodel_resource_t *resources)
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
            block->texture = GL_AllocTexture8(model, va("@lightmap_%03d", i), &pic, TEXTURE_TYPE_LIGHTMAP);
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
            if (!(surf->flags & SURF_DRAWTILED)) {
                byte *base = resources->blocks[surf->lightmapblock].data;
                base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * gl_lightmap_bytes;
                R_BuildLightMap(surf, base, BLOCK_WIDTH * gl_lightmap_bytes);
            }
	    BuildSurfaceGLPoly(brushmodel, surf, hunkbase);
	}

	/* upload all lightmaps that were filled */
	GL_UploadLightmaps(model, resources);
    }

    /* TODO - rename or separate out the things that are not building lightmaps */

    /* Count the number of verts on submodel surfs with a sky texture */
    brushmodel = loaded_brushmodels;
    for ( ; brushmodel ; brushmodel = brushmodel->next) {
        if (!brushmodel->parent)
            continue;
        surf = brushmodel->surfaces + brushmodel->firstmodelsurface;
        for (i = 0; i < brushmodel->nummodelsurfaces; i++, surf++) {
            if (!(surf->flags & SURF_DRAWSKY) || !surf->numedges)
                continue;
            GLBrushModel(brushmodel)->drawsky = true;
            glbrushmodel_resource_t *resources = GLBrushModel(brushmodel)->resources;
            if (surf->numedges > resources->max_submodel_skypoly_verts)
                resources->max_submodel_skypoly_verts = surf->numedges;
        }
    }
}
