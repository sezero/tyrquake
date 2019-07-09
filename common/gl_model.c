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

#include "glquake.h"
#include "gl_model.h"
#include "sys.h"

/*
 * Model Loader Functions
 */
static int GL_AliashdrPadding() { return offsetof(gl_aliashdr_t, ahdr); }
static int GL_BrushModelPadding() { return offsetof(glbrushmodel_t, brushmodel); }

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct {
    short x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

static void
GL_FloodFillSkin(byte *skin, int skinwidth, int skinheight)
{
    byte fillcolor = *skin;	// assume this is the pixel to fill
    floodfill_t fifo[FLOODFILL_FIFO_SIZE];
    int inpt = 0, outpt = 0;
    int filledcolor = -1;
    int i;

    if (filledcolor == -1) {
	filledcolor = 0;
	// attempt to find opaque black (FIXME - precompute!)
        const qpixel32_t black = { .c.red = 0, .c.green = 0, .c.blue = 0, .c.alpha = 255 };
	for (i = 0; i < 256; ++i)
	    if (qpal_standard.colors[i].rgba == black.rgba)
	    {
		filledcolor = i;
		break;
	    }
    }
    // can't fill to filled color or to transparent color (used as visited marker)
    if ((fillcolor == filledcolor) || (fillcolor == 255)) {
	//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
	return;
    }

    fifo[inpt].x = 0, fifo[inpt].y = 0;
    inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

    while (outpt != inpt) {
	int x = fifo[outpt].x, y = fifo[outpt].y;
	int fdc = filledcolor;
	byte *pos = &skin[x + skinwidth * y];

	outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

	if (x > 0)
	    FLOODFILL_STEP(-1, -1, 0);
	if (x < skinwidth - 1)
	    FLOODFILL_STEP(1, 1, 0);
	if (y > 0)
	    FLOODFILL_STEP(-skinwidth, 0, -1);
	if (y < skinheight - 1)
	    FLOODFILL_STEP(skinwidth, 0, 1);
	skin[x + skinwidth * y] = fdc;
    }
}

static void
GL_LoadAliasSkinData(model_t *model, aliashdr_t *aliashdr, const alias_skindata_t *skindata)
{
    int i, skinsize;
    qgltexture_t *textures;
    byte *pixels;

    skinsize = aliashdr->skinwidth * aliashdr->skinheight;
    pixels = Mod_AllocName(skindata->numskins * skinsize, model->name);
    aliashdr->skindata = (byte *)pixels - (byte *)aliashdr;
    textures = Mod_AllocName(skindata->numskins * sizeof(qgltexture_t), model->name);
    GL_Aliashdr(aliashdr)->textures = (byte *)textures - (byte *)aliashdr;

    for (i = 0; i < skindata->numskins; i++) {
	GL_FloodFillSkin(skindata->data[i], aliashdr->skinwidth, aliashdr->skinheight);
	memcpy(pixels, skindata->data[i], skinsize);
        pixels += skinsize;
    }

    GL_LoadAliasSkinTextures(model, aliashdr);
}

static alias_loader_t GL_AliasModelLoader = {
    .Padding = GL_AliashdrPadding,
    .LoadSkinData = GL_LoadAliasSkinData,
    .LoadMeshData = GL_LoadAliasMeshData,
    .CacheDestructor = NULL,
};

const alias_loader_t *
R_AliasModelLoader(void)
{
    return &GL_AliasModelLoader;
}

/*
 * Allocates space in the lightmap blocks for the surface lightmap
 */
static void
GL_AllocLightmapBlock(glbrushmodel_t *glmodel, msurface_t *surf)
{
    int i, j;
    int best, best2;
    int blocknum;
    int *allocated;
    int width, height;

    assert(!(surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB)));

    width = (surf->extents[0] >> 4) + 1;
    height = (surf->extents[1] >> 4) + 1;

    /*
     * Only scan over the last four blocks. Only negligable effects on the
     * packing efficiency, but much faster for maps with a lot of lightmaps.
     */
    blocknum = qmax(0, glmodel->numblocks - 4);
    for ( ;; blocknum++) {
	if (blocknum == glmodel->numblocks) {
	    Hunk_AllocExtend(glmodel->blocks, sizeof(lm_block_t));
	    glmodel->numblocks++;
	}
	allocated = glmodel->blocks[blocknum].allocated;

	best = BLOCK_HEIGHT - height + 1;
	for (i = 0; i < BLOCK_WIDTH - width; i++) {
	    best2 = 0;
	    for (j = 0; j < width; j++) {
		/* If it's not going to fit, don't check again... */
		if (allocated[i + j] + height > BLOCK_HEIGHT) {
		    i += j + 1;
		    break;
		}
		if (allocated[i + j] >= best)
		    break;
		if (allocated[i + j] > best2)
		    best2 = allocated[i + j];
	    }
	    if (j == width) {	// this is a valid spot
		surf->light_s = i;
		surf->light_t = best = best2;
	    }
	}
	if (best + height <= BLOCK_HEIGHT)
	    break;
    }

    /* Mark the allocation as used */
    for (i = 0; i < width; i++) {
	allocated[surf->light_s + i] = best + height;
    }

    surf->lightmaptexturenum = blocknum;
}

static void
GL_BrushModelPostProcess(brushmodel_t *brushmodel)
{
    glbrushmodel_t *glbrushmodel;
    msurface_t **texturechains;
    msurface_t *surf;
    int i;

    /* Setup texture chains so we can allocate lightmaps in order of texture */
    texturechains = Hunk_TempAllocExtend(brushmodel->numtextures * sizeof(msurface_t *));
    surf = brushmodel->surfaces;
    for (i = 0; i < brushmodel->numsurfaces; i++, surf++) {
	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
	    continue;
	unsigned texturenum = surf->texinfo->texture->texturenum;
	surf->texturechain = texturechains[texturenum];
	texturechains[texturenum] = surf;
    }

    /* Allocate lightmap blocks */
    glbrushmodel = GLBrushModel(brushmodel);
    glbrushmodel->blocks = Mod_AllocName(sizeof(lm_block_t), brushmodel->model.name);
    for (i = 0; i < brushmodel->numtextures; i++) {
	for (surf = texturechains[i]; surf; surf = surf->texturechain) {
	    GL_AllocLightmapBlock(glbrushmodel, surf);
	}
    }
}

static brush_loader_t GL_BrushModelLoader = {
    .Padding = GL_BrushModelPadding,
    .PostProcess = GL_BrushModelPostProcess,
};

const brush_loader_t *
R_BrushModelLoader()
{
    return &GL_BrushModelLoader;
}

