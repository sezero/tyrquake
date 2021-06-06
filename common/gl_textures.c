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

#include "cmd.h"
#include "console.h"
#include "crc.h"
#include "cvar.h"
#include "list.h"
#include "glquake.h"
#include "qpic.h"
#include "sys.h"

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "client.h"
#endif

// FIXME - should I let this get larger, with view to enhancements?
static cvar_t gl_max_size = { "gl_max_size", "1024" };

static cvar_t gl_playermip = { "gl_playermip", "0" };
static cvar_t gl_nobind = { "gl_nobind", "0" };
static cvar_t gl_npot = { "gl_npot", "1" };
cvar_t gl_picmip = { "gl_picmip", "0" };

typedef struct {
    struct list_node list;
    const model_t *owner;
    GLuint texnum;
    int width, height;
    enum texture_type type;
    unsigned short crc;		// CRC for texture cache matching
    char name[MAX_QPATH];
} gltexture_t;

#define DEFAULT_MAX_TEXTURES 2048
static cvar_t gl_max_textures = {
    .name = "gl_max_textures",
    .string = stringify(DEFAULT_MAX_TEXTURES),
    .flags = CVAR_VIDEO,
};

struct {
    struct list_node free;
    struct list_node active;
    struct list_node inactive;
    gltexture_t *data;
    int num_textures;
    int hunk_highmark;
} manager = {
    .free = LIST_HEAD_INIT(manager.free),
    .active = LIST_HEAD_INIT(manager.active),
    .inactive = LIST_HEAD_INIT(manager.inactive),
};

static void
GL_InitTextureManager()
{
    if (manager.data) {
        Hunk_FreeToHighMark(manager.hunk_highmark);
        manager.data = NULL;
    }

    manager.num_textures = qmax((int)gl_max_textures.value, 512);
    manager.hunk_highmark = Hunk_HighMark();
    manager.data = Hunk_HighAllocName(manager.num_textures * sizeof(gltexture_t), "texmgr");

    list_head_init(&manager.free);
    list_head_init(&manager.active);
    list_head_init(&manager.inactive);

    /* Add all textures to the free list */
    for (int i = 0; i < manager.num_textures; i++)
        list_add_tail(&manager.data[i].list, &manager.free);
}

void
GL_FreeTextures()
{
    gltexture_t *texture;

    list_for_each_entry(texture, &manager.active, list)
        glDeleteTextures(1, &texture->texnum);
    list_for_each_entry(texture, &manager.inactive, list)
        glDeleteTextures(1, &texture->texnum);

    GL_InitTextureManager();
}

void
GL_Bind(int texnum)
{
    if (gl_nobind.value)
	texnum = charset_texture;
    if (currenttexture == texnum)
	return;
    currenttexture = texnum;

    glBindTexture(GL_TEXTURE_2D, texnum);
}


typedef struct {
    const char *name;
    GLenum min_filter;
    GLenum mag_filter;
} glmode_t;

static glmode_t *glmode;

static glmode_t gl_texturemodes[] = {
    { "gl_nearest", GL_NEAREST, GL_NEAREST },
    { "gl_linear", GL_LINEAR, GL_LINEAR },
    { "gl_nearest_mipmap_nearest", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
    { "gl_linear_mipmap_nearest", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
    { "gl_nearest_mipmap_linear", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
    { "gl_linear_mipmap_linear", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

static void
GL_UpdateTextureMode(const gltexture_t *texture, const glmode_t *mode)
{
    if (texture->type == TEXTURE_TYPE_LIGHTMAP)
        return; /* Lightmap filter is always GL_LINEAR */
    if (texture->type == TEXTURE_TYPE_NOTEXTURE)
        return; /* Notexture is always GL_NEAREST */
    GL_Bind(texture->texnum);
    if (texture_properties[texture->type].mipmap) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mode->min_filter);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mode->mag_filter);
    } else {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mode->mag_filter);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mode->mag_filter);
    }
}

/*
===============
Draw_TextureMode_f
===============
*/
static void
GL_TextureMode_f(void)
{
    int i;
    gltexture_t *texture;

    if (Cmd_Argc() == 1) {
	Con_Printf("%s\n", glmode->name);
	return;
    }

    for (i = 0; i < ARRAY_SIZE(gl_texturemodes); i++) {
	if (!strcasecmp(gl_texturemodes[i].name, Cmd_Argv(1))) {
	    glmode = &gl_texturemodes[i];
	    break;
	}
    }
    if (i == ARRAY_SIZE(gl_texturemodes)) {
	Con_Printf("bad filter name\n");
	return;
    }

    /* Change all the existing mipmap texture objects */
    list_for_each_entry(texture, &manager.active, list)
        GL_UpdateTextureMode(texture, glmode);
}

static void
GL_TextureMode_Arg_f(struct stree_root *root, int argnum)
{
    int i, arg_len;

    if (argnum != 1)
        return;

    const char *arg = Cmd_Argv(1);
    arg_len = arg ? strlen(arg) : 0;
    for (i = 0; i < ARRAY_SIZE(gl_texturemodes); i++) {
        if (!arg || !strncasecmp(gl_texturemodes[i].name, arg, arg_len))
            STree_InsertAlloc(root, gl_texturemodes[i].name, false);
    }
}

/*
 * Uploads a 32-bit RGBA texture.  The original pixels are destroyed during
 * the scaling/mipmap process.  When done, the width and height the source pic
 * are set to the values used to upload the (miplevel 0) texture.
 *
 * This matters in the case of non-power-of-two HUD or skin textures
 * getting expanded, since the texture coordinates need to be adjusted
 * accordingly when drawing.
 */
void
GL_Upload32(qpic32_t *pic, enum texture_type type)
{
    GLint internal_format;
    qpic32_t *scaled;
    int width, height, mark, miplevel, picmip;

    /* This is not written for lightmaps! */
    assert(type != TEXTURE_TYPE_LIGHTMAP);

    if (!gl_npotable || !gl_npot.value) {
	/* find the next power-of-two size up */
	width = 1;
	while (width < pic->width)
	    width <<= 1;
	height = 1;
	while (height < pic->height)
	    height <<= 1;
    } else {
	width = pic->width;
	height = pic->height;
    }

    mark = Hunk_LowMark();

    /* Begin by expanding the texture if needed */
    if (width != pic->width || height != pic->height) {
	scaled = QPic32_Alloc(width, height);
        switch (type) {
            case TEXTURE_TYPE_HUD:
            case TEXTURE_TYPE_ALIAS_SKIN:
            case TEXTURE_TYPE_ALIAS_SKIN_FULLBRIGHT:
            case TEXTURE_TYPE_PLAYER_SKIN:
            case TEXTURE_TYPE_PLAYER_SKIN_FULLBRIGHT:
		QPic32_Expand(pic, scaled);
                break;
            default:
                QPic32_Stretch(pic, scaled);
                break;
	}
    } else {
	scaled = pic;
    }

    /* Allow some textures to be crunched down by player preference */
    if (texture_properties[type].playermip) {
        picmip = qmax(0, (int)gl_playermip.value);
    } else if (texture_properties[type].picmip) {
        picmip = qmax(0, (int)gl_picmip.value);
    } else {
        picmip = 0;
    }
    while (picmip) {
	if (width == 1 && height == 1)
	    break;
	width = qmax(1, width >> 1);
	height = qmax(1, height >> 1);
	QPic32_MipMap(scaled, texture_properties[type].alpha_op);
	picmip--;
    }

    /* Set the internal format */
    if (texture_properties[type].palette->alpha) {
        internal_format = gl_alpha_format;
    } else {
        internal_format = gl_solid_format;
    }

    /* Upload with or without mipmaps, depending on type */
    if (texture_properties[type].mipmap) {
        miplevel = 0;
        while (1) {
            glTexImage2D(GL_TEXTURE_2D, miplevel, internal_format,
                         scaled->width, scaled->height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, scaled->pixels);
            if (scaled->width == 1 && scaled->height == 1)
                break;

            QPic32_MipMap(scaled, texture_properties[type].alpha_op);
            miplevel++;
        }
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmode->min_filter);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmode->mag_filter);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format,
                     scaled->width, scaled->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, scaled->pixels);
        if (type == TEXTURE_TYPE_NOTEXTURE) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmode->mag_filter);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmode->mag_filter);
        }
    }

    /* Set texture wrap mode */
    GLenum wrap = texture_properties[type].repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    Hunk_FreeToLowMark(mark);

    /* Pass back the width and height used on the scaled texture that was uploaded */
    pic->width = width;
    pic->height = height;
}

/**
 * Special rules for determining the required size for the warp target
 * texture (warp texture is assumed to be square).
 *
 * Must be less or equal to the backbuffer size since we need to
 * render into that first then copy the pixels out.  If npot textures
 * are not supported, make sure we select the next *smaller* power of
 * two here to avoid the generic upload function from stretching it
 * up.
 */
static int
GL_GetWarpImageSize(qpic8_t *pic)
{
    int size = qmin(pic->width * 4, WARP_RENDER_TEXTURE_SIZE);

    if (!gl_npotable || !gl_npot.value) {
        int original_size = size;
        size = 1;
        while (size < original_size)
            size <<= 1;
        while (size > vid.width || size > vid.height)
            size >>= 1;
    } else {
        size = qmin(size, vid.width);
        size = qmin(size, vid.height);
    }

    return size;
}

/*
===============
GL_Upload8

Sets the actual uploaded width/height in the original pic, which matters for skin
textures that get expanded up to the nearest power-of-two size.

The alpha version will scale the texture alpha channel by a factor of (alpha / 255).
===============
*/
void
GL_Upload8_Alpha(qpic8_t *pic, enum texture_type type, byte alpha)
{
    const qpalette32_t *palette = texture_properties[type].palette;
    enum qpic_alpha_operation alpha_op = texture_properties[type].alpha_op;
    qpic32_t *pic32;
    int mark;

    mark = Hunk_LowMark();

    if (type == TEXTURE_TYPE_WARP_TARGET) {
        int size = GL_GetWarpImageSize(pic);
        pic32 = QPic32_Alloc(size, size);
    } else {
        pic32 = QPic32_Alloc(pic->width, pic->height);
        QPic_8to32(pic, pic32, palette, alpha_op);
        if (alpha != 255)
            QPic32_ScaleAlpha(pic32, alpha);
    }
    GL_Upload32(pic32, type);

    pic->width = pic32->width;
    pic->height = pic32->height;

    Hunk_FreeToLowMark(mark);
}

void
GL_Upload8(qpic8_t *pic, enum texture_type type)
{
    GL_Upload8_Alpha(pic, type, 255);
}

void
GL_Upload8_Translate(qpic8_t *pic, enum texture_type type, const byte *translation)
{
    const byte *source;
    byte *pixels, *dest;
    qpic8_t translated_pic;
    int mark, i, j;

    mark = Hunk_LowMark();

    source = pic->pixels;
    pixels = dest = Hunk_AllocName(pic->width * pic->height, "transpic");
    for (i = 0; i < pic->height; i++) {
        for (j = 0; j < pic->width; j++) {
            *dest++ = translation[*source++];
        }
        source += pic->stride - pic->width;
    }

    translated_pic.width = pic->width;
    translated_pic.height = pic->height;
    translated_pic.stride = pic->width;
    translated_pic.pixels = pixels;

    GL_Upload8(&translated_pic, type);

    Hunk_FreeToLowMark(mark);
}

void
GL_Upload8_GLPic(glpic_t *glpic)
{
    const qpalette32_t *palette = texture_properties[TEXTURE_TYPE_HUD].palette;
    enum qpic_alpha_operation alpha_op = texture_properties[TEXTURE_TYPE_HUD].alpha_op;
    qpic32_t *pic32;
    int mark;

    mark = Hunk_LowMark();

    pic32 = QPic32_Alloc(glpic->pic.width, glpic->pic.height);
    QPic_8to32(&glpic->pic, pic32, palette, alpha_op);
    GL_Upload32(pic32, TEXTURE_TYPE_HUD);

    glpic->sl = 0;
    glpic->sh = qmin(1.0f, (float)glpic->pic.width / (float)pic32->width);
    glpic->tl = 0;
    glpic->th = qmin(1.0f, (float)glpic->pic.height / (float)pic32->height);

    Hunk_FreeToLowMark(mark);
}

/*
================
GL_AllocTexture

Return an existing texture reference if we already have one for this
name (and the CRC matches).  Otherwise allocate and configure a new
one.
================
*/
struct alloc_texture_result {
    qboolean exists;
    int texnum;
};

static struct alloc_texture_result
GL_AllocTexture(const model_t *owner, const char *name, unsigned short crc, int width, int height, enum texture_type type)
{
    struct alloc_texture_result result = {0};
    gltexture_t *texture;

    /* Check the active list for a match */
    list_for_each_entry(texture, &manager.active, list) {
        if (owner != texture->owner)
            continue;
        if (!strcmp(name, texture->name)) {
            if (type != texture->type || (crc != texture->crc && type < TEXTURE_TYPE_LIGHTMAP))
                goto GL_AllocTexture_setup;
            if (width != texture->width || height != texture->height)
                goto GL_AllocTexture_setup;

            result.exists = true;
            goto GL_AllocTexture_out;
        }
    }

    /* Check the inactive list for a match, these are unowned */
    list_for_each_entry(texture, &manager.inactive, list) {
        if (!strcmp(name, texture->name)) {
            if (type != texture->type || (crc != texture->crc && type < TEXTURE_TYPE_LIGHTMAP))
                goto GL_AllocTexture_setup;
            if (width != texture->width || height != texture->height)
                goto GL_AllocTexture_setup;

            /* Move it back to the active list and return */
            list_del(&texture->list);
            list_add(&texture->list, &manager.active);
            result.exists = true;
            goto GL_AllocTexture_out;
        }
    }

    if (!list_empty(&manager.free)) {
        /* Grab a texture from the free list */
        texture = container_of(manager.free.next, gltexture_t, list);
        list_del(&texture->list);
    } else if (!list_empty(&manager.inactive)) {
        /* Repurpose the least recently used inactive texture (from the list tail) */
        texture = container_of(manager.inactive.prev, gltexture_t, list);
        list_del(&texture->list);
    } else {
        // TODO: use NOTEXTURE instead?
	Sys_Error("numgltextures == MAX_GLTEXTURES");
    }

    qstrncpy(texture->name, name, sizeof(texture->name));
    glGenTextures(1, &texture->texnum);

    list_add(&texture->list, &manager.active);

 GL_AllocTexture_setup:
    texture->crc = crc;
    texture->width = width;
    texture->height = height;
    texture->type = type;

 GL_AllocTexture_out:
    texture->owner = owner;
    result.texnum = texture->texnum;

    return result;
}

static struct alloc_texture_result
GL_AllocTexture8_Result(const model_t *owner, const char *name, const qpic8_t *pic, enum texture_type type)
{
    unsigned short crc = CRC_Block(pic->pixels, pic->width * pic->height * sizeof(pic->pixels[0]));
    return GL_AllocTexture(owner, name, crc, pic->width, pic->height, type);
}

int
GL_AllocTexture8(const model_t *owner, const char *name, const qpic8_t *pic, enum texture_type type)
{
    return GL_AllocTexture8_Result(owner, name, pic, type).texnum;
}

static struct alloc_texture_result
GL_AllocTexture32_Result(const model_t *owner, const char *name, const qpic32_t *pic, enum texture_type type)
{
    unsigned short crc = CRC_Block(pic->pixels, pic->width * pic->height * sizeof(pic->pixels[0]));
    return GL_AllocTexture(owner, name, crc, pic->width, pic->height, type);
}

int
GL_AllocTexture32(const model_t *owner, const char *name, const qpic32_t *pic, enum texture_type type)
{
    return GL_AllocTexture32_Result(owner, name, pic, type).texnum;
}

int
GL_LoadTexture8_Alpha(const model_t *owner, const char *name, qpic8_t *pic, enum texture_type type, byte alpha)
{
    struct alloc_texture_result result = GL_AllocTexture8_Result(owner, name, pic, type);

    if (!isDedicated) {
        if (!result.exists) {
            GL_Bind(result.texnum);
            GL_Upload8_Alpha(pic, type, alpha);
        } else if (type == TEXTURE_TYPE_WARP_TARGET) {
            /*
             * TODO: Kind of a temporary fix for the terrible interface to handle textures that end
             * up a different size after upload due to picmip, npot stretching and all that.  When
             * fixing that, fix this too!
             *
             * Probably should decide the desired upload size for the input pic in advance and then
             * check the cache for those factors matching as well as the input image size (or at
             * least CRC).
             */
            pic->width = pic->height = GL_GetWarpImageSize(pic);
        }
    }

    return result.texnum;
}

int
GL_LoadTexture8(const model_t *owner, const char *name, qpic8_t *pic, enum texture_type type)
{
    return GL_LoadTexture8_Alpha(owner, name, pic, type, 255);
}

int
GL_LoadTexture8_GLPic(const model_t *owner, const char *name, glpic_t *glpic)
{
    struct alloc_texture_result result = GL_AllocTexture8_Result(owner, name, &glpic->pic, TEXTURE_TYPE_HUD);

    if (!isDedicated && !result.exists) {
	GL_Bind(result.texnum);
	GL_Upload8_GLPic(glpic);
    }

    return result.texnum;
}

void
GL_DisownTextures(const model_t *owner)
{
    gltexture_t *texture, *next;

    /*
     * Disowned textures are added to the head of the list, so more
     * recently discarded ones are quicker to find.  We then harvest
     * from the tail if we need a new texture so the least recently
     * used ones will be purged.
     */
    list_for_each_entry_safe(texture, next, &manager.active, list) {
        if (texture->owner == owner) {
            list_del(&texture->list);
            list_add(&texture->list, &manager.inactive);
        }
    }
}

static void
GL_AddTexturesToTree(struct stree_root *root, struct list_node *list_head)
{
    gltexture_t *texture;

    /*
     * The same texture name with a different owner could be loaded
     * more than once, so if the insert fails (due to non-uniqueness)
     * add an increasing counter to the string until we can
     * successfully insert.  There are typically very few name
     * clashes, but they do occur.
     */
    *root = STREE_ROOT;
    list_for_each_entry(texture, list_head, list) {
        qboolean result = STree_InsertAlloc(root, texture->name, false);
        if (!result) {
            int counter = 1;
            while (!result) {
                result = STree_InsertAlloc(root, va("%s(%d)", texture->name, counter++), true);
            }
        }
    }
}

static void
GL_PrintTextures_f(void)
{
    struct stree_root root;
    gltexture_t *texture;
    qboolean print_active = true;
    qboolean print_inactive = false;
    qboolean print_free = false;

    if (Cmd_Argc() > 1) {
        if (!strcasecmp(Cmd_Argv(1), "active")) {
            print_active = true;
        } else if (!strcasecmp(Cmd_Argv(1), "inactive")) {
            print_active = false;
            print_inactive = true;
        } else if (!strcasecmp(Cmd_Argv(1), "free")) {
            print_active = false;
            print_free = true;
        } else if (!strcasecmp(Cmd_Argv(1), "all")) {
            print_active = true;
            print_inactive = true;
            print_free = true;
        } else {
            Con_Printf("Usage: %s [active|inactive|free|all]\n", Cmd_Argv(0));
            return;
        }
    }

    STree_AllocInit();

    if (print_active) {
        GL_AddTexturesToTree(&root, &manager.active);
        Con_ShowTree(&root);
        Con_Printf("======== %d active textures ========\n", root.entries);
    }
    if (print_inactive) {
        GL_AddTexturesToTree(&root, &manager.inactive);
        Con_ShowTree(&root);
        Con_Printf("======== %d inactive textures in cache ========\n", root.entries);
    }
    if (print_free) {
        int count = 0;
        list_for_each_entry(texture, &manager.free, list)
            count++;
        Con_Printf("======== %d free textures slots ========\n", count);
    }

    Con_Printf("NO OWNER:\n");
    list_for_each_entry(texture, &manager.active, list) {
        if (texture->owner)
            continue;
        Con_Printf("%s\n", texture->name);
    }
}

static void
GL_PrintTextures_Arg_f(struct stree_root *root, int argnum)
{
    if (argnum != 1)
        return;

    const char *arg = Cmd_Argv(1);
    const char *args[] = { "active", "inactive", "free", "all" };
    int i;
    int arg_len = arg ? strlen(arg) : 0;

    for (i = 0; i < ARRAY_SIZE(args); i++) {
        if (!arg || !strncasecmp(args[i], arg, arg_len))
            STree_InsertAlloc(root, args[i], false);
    }
}

void
GL_Textures_RegisterVariables()
{
    Cvar_RegisterVariable(&gl_nobind);
    Cvar_RegisterVariable(&gl_max_size);
    Cvar_RegisterVariable(&gl_picmip);
    Cvar_RegisterVariable(&gl_playermip);
    Cvar_RegisterVariable(&gl_max_textures);
    Cvar_RegisterVariable(&gl_npot);
}

void
GL_Textures_AddCommands()
{
    Cmd_AddCommand("gl_texturemode", GL_TextureMode_f);
    Cmd_SetCompletion("gl_texturemode", GL_TextureMode_Arg_f);
    Cmd_AddCommand("gl_printtextures", GL_PrintTextures_f);
    Cmd_SetCompletion("gl_printtextures", GL_PrintTextures_Arg_f);
}

void
GL_Textures_Init(void)
{
    GLint max_size;

    glmode = gl_texturemodes;

    // FIXME - could do better to check on each texture upload with
    //         GL_PROXY_TEXTURE_2D
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (gl_max_size.value > max_size) {
	Con_DPrintf("Reducing gl_max_size from %d to %d\n",
		    (int)gl_max_size.value, max_size);
	Cvar_Set("gl_max_size", va("%d", max_size));
    }

    GL_InitTextureManager();
    GL_LoadNoTexture();
}
