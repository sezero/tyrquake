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
GL_AllocLightmapBlock(glbrushmodel_resource_t *resources, msurface_t *surf)
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
    blocknum = qmax(0, resources->numblocks - 4);
    for ( ;; blocknum++) {
	if (blocknum == resources->numblocks) {
	    Hunk_AllocExtend(resources->blocks, sizeof(lm_block_t));
	    resources->numblocks++;
	}
	allocated = resources->blocks[blocknum].allocated;

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

    surf->lightmapblock = blocknum;
}

static void
GL_BrushModelPostProcess(brushmodel_t *brushmodel)
{
}

static enum material_class
GL_GetTextureMaterialClass(texture_t *texture)
{
    if (texture->name[0] == '*')
        return MATERIAL_LIQUID;
    if (!strncmp(texture->name, "sky", 3))
        return MATERIAL_SKY;
    if (texture->gl_texturenum_fullbright)
        return MATERIAL_FULLBRIGHT;

    return MATERIAL_BASE;
}

#ifdef DEBUG
static const char *material_class_names[] = {
    "MATERIAL_BASE",
    "MATERIAL_FULLBRIGHT",
    "MATERIAL_SKY",
    "MATERIAL_LIQUID",
    "MATERIAL_END",
};

static void
Debug_PrintMaterials(glbrushmodel_t *glbrushmodel)
{
    brushmodel_t *brushmodel = &glbrushmodel->brushmodel;
    surface_material_t *material;
    enum material_class class;
    int i;

    Sys_Printf("====== %s ======\n", glbrushmodel->brushmodel.model.name);

    for (class = MATERIAL_BASE; class <= MATERIAL_END; class++) {
        Sys_Printf("%25s: %d\n", material_class_names[class], glbrushmodel->material_index[class]);
    }

    class = MATERIAL_BASE;
    material = &glbrushmodel->materials[0];
    for (i = 0; i < glbrushmodel->nummaterials; i++, material++) {
        texture_t *texture = brushmodel->textures[material->texturenum];
        Sys_Printf("Material %3d: %-16s (%3d) :: lightmap block %3d",
                   i, texture->name, material->texturenum, material->lightmapblock);
        if (i == glbrushmodel->material_index[class]) {
            while (i == glbrushmodel->material_index[class + 1])
                class++;
            Sys_Printf("  <---- %s\n", material_class_names[class]);
            class++;
        } else {
            Sys_Printf("\n");
        }
    }
}
#else
static inline void Debug_PrintMaterials(glbrushmodel_t *glbrushmodel) { }
#endif

void
GL_BuildMaterials()
{
    glbrushmodel_resource_t *resources;
    glbrushmodel_t *glbrushmodel;
    brushmodel_t *brushmodel;
    msurface_t **texturechains;
    msurface_t *surf;
    enum material_class material_class;
    surface_material_t *material;
    texture_t *texture;
    int surfnum, texturenum, materialnum, material_start;

    /* Allocate the shared resource structure for the bmodels */
    resources = Hunk_AllocName(sizeof(glbrushmodel_resource_t), "resources");
    resources->blocks = Hunk_AllocName(sizeof(lm_block_t), "lightmaps");
    resources->numblocks = 1;

    /*
     * Allocate lightmaps for all brush models first, so we get contiguous
     * memory for the lightmap block allocations.
     */
    for (brushmodel = loaded_brushmodels; brushmodel; brushmodel = brushmodel->next) {
        if (brushmodel->parent)
            continue;

        /*
         * Setup (temporary) texture chains so we can allocate lightmaps in
         * order of texture.  We borrow the materialchains pointer to store
         * the texturechain until every brushmodel has had it's lightmaps
         * allocated.  We'll also initialise the surface material to -1 here
         * to signify no material yet allocated.
         */
        texturechains = Hunk_TempAllocExtend(brushmodel->numtextures * sizeof(msurface_t *));
        surf = brushmodel->surfaces;
        for (surfnum = 0; surfnum < brushmodel->numsurfaces; surfnum++, surf++) {
            surf->material = -1;
            texture = surf->texinfo->texture;
            surf->chain = texturechains[texture->texturenum];
            texturechains[texture->texturenum] = surf;
        }

        /* Allocate lightmap blocks in texture order */
        for (texturenum = 0; texturenum < brushmodel->numtextures; texturenum++) {
            surf = texturechains[texturenum];
            if (!surf)
                continue;
            if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
                continue;
            for ( ; surf; surf = surf->chain) {
                GL_AllocLightmapBlock(resources, surf);
            }
        }

        /* Save the texturechains for material allocation below */
        glbrushmodel = GLBrushModel(brushmodel);
        glbrushmodel->materialchains = texturechains;
    }

    /*
     * Next we allocate materials for each brushmodel
     */
    for (brushmodel = loaded_brushmodels; brushmodel; brushmodel = brushmodel->next) {
        if (brushmodel->parent)
            continue;

        glbrushmodel = GLBrushModel(brushmodel);
        glbrushmodel->nummaterials = 0;
        glbrushmodel->materials = Hunk_AllocName(0, "material");
        texturechains = glbrushmodel->materialchains; /* saved earlier */

        /* To group the materials by class we'll do multiple passes over the texture list */
        material_class = MATERIAL_BASE;
        for ( ; material_class < MATERIAL_END; material_class++) {
            glbrushmodel->material_index[material_class] = glbrushmodel->nummaterials;

            for (texturenum = 0; texturenum < brushmodel->numtextures; texturenum++) {
                texture = brushmodel->textures[texturenum];
                if (!texture)
                    continue;

                /* Skip past textures not in the current material class */
                if (GL_GetTextureMaterialClass(texture) != material_class)
                    continue;

                /*
                 * For each new texture we know we won't match materials
                 * generated for the previous textures, so use this index to
                 * skip ahead in the materials to be matched.
                 */
                material_start = glbrushmodel->material_index[material_class];
                for (surf = texturechains[texturenum]; surf; surf = surf->chain) {
                    int lightmapblock = surf->lightmapblock;
                    qboolean found = false;
                    for (materialnum = material_start; materialnum < glbrushmodel->nummaterials; materialnum++) {
                        material = &glbrushmodel->materials[materialnum];
                        if (material->texturenum != texturenum)
                            continue;
                        if (material->lightmapblock != lightmapblock)
                            continue;
                        surf->material = materialnum;
                        found = true;
                        break;
                    }
                    if (found)
                        continue;

                    /*
                     * TODO: The hunk extension will be rounded up to the nearest
                     * 16 bytes, which might waste some space over the entire
                     * material list.  Make a better alloc helper for this case?
                     */
                    surf->material = glbrushmodel->nummaterials;
                    Hunk_AllocExtend(glbrushmodel->materials, sizeof(surface_material_t));
                    material = &glbrushmodel->materials[glbrushmodel->nummaterials++];
                    material->texturenum = texturenum;
                    material->lightmapblock = surf->lightmapblock;
                }
            }
        }
        glbrushmodel->material_index[MATERIAL_END] = glbrushmodel->nummaterials;
    }

    /*
     * Finally, we allocate the materialchains for every brushmodel (including
     * submodels).  Submodels share the material list with their parent.  All
     * share the common resources struct.
     */
    void *hunkbase = Hunk_AllocName(0, "material");
    for (brushmodel = loaded_brushmodels; brushmodel; brushmodel = brushmodel->next) {
        glbrushmodel = GLBrushModel(brushmodel);
        if (brushmodel->parent) {
            glbrushmodel_t *parent = GLBrushModel(brushmodel->parent);
            glbrushmodel->nummaterials = parent->nummaterials;
            glbrushmodel->materials = parent->materials;
            memcpy(glbrushmodel->material_index, parent->material_index, sizeof(glbrushmodel->material_index));
        }
        glbrushmodel->materialchains = Hunk_AllocExtend(hunkbase, glbrushmodel->nummaterials * sizeof(msurface_t *));
        glbrushmodel->resources = resources;
    }

    Debug_PrintMaterials(glbrushmodel);
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

