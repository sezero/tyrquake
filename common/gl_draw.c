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

// draw.c -- this is the only file outside the refresh that touches the
// vid buffer

#include "cmd.h"
#include "console.h"
#include "crc.h"
#include "glquake.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sys.h"
#include "textures.h"
#include "view.h"
#include "wad.h"

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "vid.h"
#endif

static cvar_t gl_nobind = { "gl_nobind", "0" };
static cvar_t gl_picmip = { "gl_picmip", "0" };
static cvar_t gl_constretch = { "gl_constretch", "0", true };

// FIXME - should I let this get larger, with view to enhancements?
cvar_t gl_max_size = { "gl_max_size", "1024" };

byte *draw_chars;		/* 8*8 graphic characters */
const qpic_t *draw_disc;
static const qpic_t *draw_backtile;

static GLuint translate_texture;
static GLuint char_texture;
static GLuint cs_texture;		// crosshair texture

static byte cs_data[64] = {
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

typedef struct {
    int texnum;
    float sl, tl, sh, th;
    qpic_t pic;
} glpic_t;

static glpic_t *conback;

int gl_lightmap_format = GL_RGBA;	// 4
int gl_solid_format = GL_RGB;	// 3
int gl_alpha_format = GL_RGBA;	// 4

static int gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
static int gl_filter_max = GL_LINEAR;

typedef struct {
    GLuint texnum;
    char identifier[MAX_QPATH];
    int width, height;
    qboolean mipmap;
    unsigned short crc;		// CRC for texture cache matching
} gltexture_t;

#define	MAX_GLTEXTURES	4096
static gltexture_t gltextures[MAX_GLTEXTURES];
static int numgltextures;

void
GL_Bind(int texnum)
{
    if (gl_nobind.value)
	texnum = char_texture;
    if (currenttexture == texnum)
	return;
    currenttexture = texnum;

    glBindTexture(GL_TEXTURE_2D, texnum);
}


/*
=============================================================================

  scrap allocation

  Allocate all the little status bar obejcts into a single texture
  to crutch up stupid hardware / drivers

=============================================================================
*/

#define MAX_SCRAPS   2
#define BLOCK_WIDTH  256
#define BLOCK_HEIGHT 256
#define BLOCK_BYTES  (BLOCK_WIDTH * BLOCK_HEIGHT * 4)

typedef struct {
    GLuint glnum;
    qboolean dirty;
    int allocated[BLOCK_WIDTH];
    qpic_t pic;
    byte texels[BLOCK_BYTES]; /* referenced via pic->data */
} scrap_t;

static scrap_t gl_scraps[MAX_SCRAPS];

static void
Scrap_Init(void)
{
    int i;
    scrap_t *scrap;

    memset(gl_scraps, 0, sizeof(gl_scraps));
    scrap = gl_scraps;
    for (i = 0; i < MAX_SCRAPS; i++, scrap++) {
	scrap->pic.width = BLOCK_WIDTH;
	scrap->pic.height = BLOCK_HEIGHT;
	scrap->pic.data = scrap->texels;
	glGenTextures(1, &scrap->glnum);
    }
}

/*
 * Scrap_AllocBlock
 *   Returns a scrap and the position inside it
 */
static scrap_t *
Scrap_AllocBlock(int w, int h, int *x, int *y)
{
    int i, j;
    int best, best2;
    int scrapnum;
    scrap_t *scrap;

    /*
     * I'm sure that x & y are always set when we return from this function,
     * but silence the compiler warning anyway. May as well crash with
     * these silly values if that happens.
     */
    *x = *y = 0x818181;

    scrap = gl_scraps;
    for (scrapnum = 0; scrapnum < MAX_SCRAPS; scrapnum++, scrap++) {
	best = BLOCK_HEIGHT;

	for (i = 0; i < BLOCK_WIDTH - w; i++) {
	    best2 = 0;

	    for (j = 0; j < w; j++) {
		if (scrap->allocated[i + j] >= best)
		    break;
		if (scrap->allocated[i + j] > best2)
		    best2 = scrap->allocated[i + j];
	    }
	    if (j == w) {
		/* this is a valid spot */
		*x = i;
		*y = best = best2;
	    }
	}

	if (best + h > BLOCK_HEIGHT)
	    continue;

	for (i = 0; i < w; i++)
	    scrap->allocated[*x + i] = best + h;

	if (*x == 0x818181 || *y == 0x818181)
	    Sys_Error("%s: block allocation problem", __func__);

	scrap->dirty = true;

	return scrap;
    }

    Sys_Error("%s: full", __func__);
}


static void
Scrap_Flush(GLuint texnum)
{
    int i;
    scrap_t *scrap;

    scrap = gl_scraps;
    for (i = 0; i < MAX_SCRAPS; i++, scrap++) {
	if (scrap->dirty && texnum == scrap->glnum) {
	    GL_Bind(scrap->glnum);
	    GL_Upload8(scrap->texels, BLOCK_WIDTH, BLOCK_HEIGHT, false, true);
	    scrap->dirty = false;
	    return;
	}
    }
}

//=============================================================================
/* Support Routines */

typedef struct cachepic_s {
    char name[MAX_QPATH];
    glpic_t glpic;
} cachepic_t;

#define MAX_CACHED_PICS 128
static cachepic_t menu_cachepics[MAX_CACHED_PICS];
static int menu_numcachepics;

static byte menuplyr_pixels[4096];

static int
GL_LoadPicTexture(const qpic_t *pic)
{
    return GL_LoadTexture("", pic->width, pic->height, pic->data, false, true);
}

const qpic_t *
Draw_PicFromWad(const char *name)
{
    qpic_t *pic;
    dpic_t *dpic;
    glpic_t *glpic;
    scrap_t *scrap;

    glpic = Hunk_AllocName(sizeof(*glpic), "qpic_t");
    dpic = W_GetLumpName(&host_gfx, name);

    /* Set up the embedded pic */
    pic = &glpic->pic;
    pic->width = dpic->width;
    pic->height = dpic->height;
    pic->data = dpic->data;

    /* load little ones into the scrap */
    if (pic->width < 64 && pic->height < 64) {
	int x, y;
	int i, j, k;

	scrap = Scrap_AllocBlock(pic->width, pic->height, &x, &y);
	k = 0;
	for (i = 0; i < pic->height; i++)
	    for (j = 0; j < pic->width; j++, k++)
		scrap->pic.data[(y + i) * BLOCK_WIDTH + x + j] = pic->data[k];
	glpic->texnum = scrap->glnum;
	glpic->sl = (x + 0.01) / (float)BLOCK_WIDTH;
	glpic->sh = (x + pic->width - 0.01) / (float)BLOCK_WIDTH;
	glpic->tl = (y + 0.01) / (float)BLOCK_WIDTH;
	glpic->th = (y + pic->height - 0.01) / (float)BLOCK_WIDTH;

	return pic;
    }

    glpic->texnum = GL_LoadPicTexture(pic);
    glpic->sl = 0;
    glpic->sh = 1;
    glpic->tl = 0;
    glpic->th = 1;

    return pic;
}


/*
================
Draw_CachePic
================
*/
const qpic_t *
Draw_CachePic(const char *path)
{
    cachepic_t *cachepic;
    dpic_t *dpic;
    qpic_t *pic;
    int i;

    cachepic = menu_cachepics;
    for (i = 0; i < menu_numcachepics; i++, cachepic++)
	if (!strcmp(path, cachepic->name))
	    return &cachepic->glpic.pic;

    if (menu_numcachepics == MAX_CACHED_PICS)
	Sys_Error("menu_numcachepics == MAX_CACHED_PICS");
    menu_numcachepics++;

    /* load the pic from disk */
    snprintf(cachepic->name, sizeof(cachepic->name), "%s", path);
    dpic = COM_LoadHunkFile(path);
    if (!dpic)
	Sys_Error("%s: failed to load %s", __func__, path);
    SwapPic(dpic);
    cachepic->glpic.pic.width = dpic->width;
    cachepic->glpic.pic.height = dpic->height;
    cachepic->glpic.pic.data = dpic->data;
    pic = &cachepic->glpic.pic;

    // HACK HACK HACK --- we need to keep the bytes for
    // the translatable player picture just for the menu
    // configuration dialog
    if (!strcmp(path, "gfx/menuplyr.lmp"))
	memcpy(menuplyr_pixels, pic->data, pic->width * pic->height);

    cachepic->glpic.texnum = GL_LoadPicTexture(pic);
    cachepic->glpic.sl = 0;
    cachepic->glpic.sh = 1;
    cachepic->glpic.tl = 0;
    cachepic->glpic.th = 1;

    return pic;
}

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 8

static void
Draw_ScaledCharToConback(qpic_t *conback, int num, byte *dest)
{
    int row, col;
    byte *source, *src;
    int drawlines, drawwidth;
    int x, y, fstep, f;

    drawlines = conback->height * CHAR_HEIGHT / 200;
    drawwidth = conback->width * CHAR_WIDTH / 320;

    row = num >> 4;
    col = num & 15;
    source = draw_chars + (row << 10) + (col << 3);
    fstep = 320 * 0x10000 / conback->width;

    for (y = 0; y < drawlines; y++, dest += conback->width) {
	src = source + (y * CHAR_HEIGHT / drawlines) * 128;
	f = 0;
	for (x = 0; x < drawwidth; x++, f += fstep) {
	    if (src[f >> 16] != 255)
		dest[x] = 0x60 + src[f >> 16];
	}
    }
}

/*
 * Draw_ConbackString
 *
 * This function draws a string to a very specific location on the console
 * background. The position is such that for a 320x200 background, the text
 * will be 6 pixels from the bottom and 11 pixels from the right. For other
 * sizes, the positioning is scaled so as to make it appear the same size and
 * at the same location.
 */
static void
Draw_ConbackString(qpic_t *conback, const char *str)
{
    int len, row, col, i, x;
    byte *dest;

    len = strlen(str);
    row = conback->height - ((CHAR_HEIGHT + 6) * conback->height / 200);
    col = conback->width - ((11 + CHAR_WIDTH * len) * conback->width / 320);

    dest = conback->data + conback->width * row + col;
    for (i = 0; i < len; i++) {
	x = i * CHAR_WIDTH * conback->width / 320;
	Draw_ScaledCharToConback(conback, str[i], dest + x);
    }
}

typedef struct {
    const char *name;
    int minimize, maximize;
} glmode_t;

static glmode_t gl_texturemodes[] = {
    { "GL_NEAREST", GL_NEAREST, GL_NEAREST },
    { "GL_LINEAR", GL_LINEAR, GL_LINEAR },
    { "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
    { "GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
    { "GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
    { "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

/*
===============
Draw_TextureMode_f
===============
*/
static void
Draw_TextureMode_f(void)
{
    int i;
    gltexture_t *glt;

    if (Cmd_Argc() == 1) {
	for (i = 0; i < 6; i++)
	    if (gl_filter_min == gl_texturemodes[i].minimize) {
		Con_Printf("%s\n", gl_texturemodes[i].name);
		return;
	    }
	Con_Printf("current filter is unknown???\n");
	return;
    }

    for (i = 0; i < 6; i++) {
	if (!strcasecmp(gl_texturemodes[i].name, Cmd_Argv(1)))
	    break;
    }
    if (i == 6) {
	Con_Printf("bad filter name\n");
	return;
    }

    gl_filter_min = gl_texturemodes[i].minimize;
    gl_filter_max = gl_texturemodes[i].maximize;

    // change all the existing mipmap texture objects
    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	if (glt->mipmap) {
	    GL_Bind(glt->texnum);
	    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			    gl_filter_min);
	    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			    gl_filter_max);
	}
    }
}

static struct stree_root *
Draw_TextureMode_Arg_f(const char *arg)
{
    int i, arg_len;
    struct stree_root *root;

    root = Z_Malloc(sizeof(struct stree_root));
    if (root) {
	*root = STREE_ROOT;
	STree_AllocInit();
	arg_len = arg ? strlen(arg) : 0;
	for (i = 0; i < ARRAY_SIZE(gl_texturemodes); i++) {
	    if (!arg || !strncasecmp(gl_texturemodes[i].name, arg, arg_len))
		STree_InsertAlloc(root, gl_texturemodes[i].name, false);
	}
    }
    return root;
}

/*
===============
Draw_Init
===============
*/
void
Draw_Init(void)
{
    int i;
    dpic_t *dpic;
    qpic_t *pic;
    char version[5];

    Cvar_RegisterVariable(&gl_nobind);
    Cvar_RegisterVariable(&gl_max_size);
    Cvar_RegisterVariable(&gl_picmip);
    Cvar_RegisterVariable(&gl_constretch);

    // FIXME - could do better to check on each texture upload with
    //         GL_PROXY_TEXTURE_2D
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &i);
    if (gl_max_size.value > i) {
	Con_DPrintf("Reducing gl_max_size from %i to %i\n",
		    (int)gl_max_size.value, i);
	Cvar_Set("gl_max_size", va("%i", i));
    }

    Cmd_AddCommand("gl_texturemode", Draw_TextureMode_f);
    Cmd_SetCompletion("gl_texturemode", Draw_TextureMode_Arg_f);

    // load the console background and the charset
    // by hand, because we need to write the version
    // string into the background before turning
    // it into a texture
    draw_chars = W_GetLumpName(&host_gfx, "conchars");
    for (i = 0; i < 256 * 64; i++)
	if (draw_chars[i] == 0)
	    draw_chars[i] = 255;	// proper transparent color

    // now turn them into textures
    char_texture = GL_LoadTexture("charset", 128, 128, draw_chars, false, true);
    cs_texture = GL_LoadTexture("crosshair", 8, 8, cs_data, false, true);

    conback = Hunk_AllocName(sizeof(*conback), "qpic_t");
    dpic = COM_LoadHunkFile("gfx/conback.lmp");
    if (!dpic)
	Sys_Error("Couldn't load gfx/conback.lmp");
    SwapPic(dpic);
    conback->pic.width = dpic->width;
    conback->pic.height = dpic->height;
    conback->pic.data = dpic->data;
    pic = &conback->pic;

    /* hack the version number directly into the pic */
    snprintf(version, sizeof(version), "%s", stringify(TYR_VERSION));
    Draw_ConbackString(pic, version);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    conback->texnum = GL_LoadTexture("conback", pic->width, pic->height,
				     pic->data, false, false);
    conback->sl = 0;
    conback->sh = 1;
    conback->tl = 0;
    conback->th = 1;

#ifdef NQ_HACK
    pic->width = vid.width;
    pic->height = vid.height;
#endif
#ifdef QW_HACK
    pic->width = vid.conwidth;
    pic->height = vid.conheight;
#endif

    // save a texture slot for translated picture
    glGenTextures(1, &translate_texture);

    // create textures for scraps
    Scrap_Init();

    //
    // get the other pics we need
    //
    draw_disc = Draw_PicFromWad("disc");
    draw_backtile = Draw_PicFromWad("backtile");
}

/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void
Draw_Character(int x, int y, int num)
{
    int row, col;
    float frow, fcol, size;

    if (num == 32)
	return;			// space

    num &= 255;

    if (y <= -8)
	return;			// totally off screen

    row = num >> 4;
    col = num & 15;

    frow = row * 0.0625;
    fcol = col * 0.0625;
    size = 0.0625;

    GL_Bind(char_texture);
    glBegin(GL_QUADS);
    glTexCoord2f(fcol, frow);
    glVertex2f(x, y);
    glTexCoord2f(fcol + size, frow);
    glVertex2f(x + 8, y);
    glTexCoord2f(fcol + size, frow + size);
    glVertex2f(x + 8, y + 8);
    glTexCoord2f(fcol, frow + size);
    glVertex2f(x, y + 8);
    glEnd();
}

/*
================
Draw_String
================
*/
void
Draw_String(int x, int y, const char *str)
{
    while (*str) {
	Draw_Character(x, y, *str);
	str++;
	x += 8;
    }
}

/*
================
Draw_Alt_String
================
*/
void
Draw_Alt_String(int x, int y, const char *str)
{
    while (*str) {
	Draw_Character(x, y, (*str) | 0x80);
	str++;
	x += 8;
    }
}

void
Draw_Crosshair(void)
{
    int x, y;
    unsigned char *pColor;

    if (crosshair.value == 2) {
	x = scr_vrect.x + scr_vrect.width / 2 - 3 + cl_crossx.value;
	y = scr_vrect.y + scr_vrect.height / 2 - 3 + cl_crossy.value;

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	pColor = (unsigned char *)&d_8to24table[(byte)crosshaircolor.value];
	glColor4ubv(pColor);
	GL_Bind(cs_texture);

	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);
	glVertex2f(x - 4, y - 4);
	glTexCoord2f(1, 0);
	glVertex2f(x + 12, y - 4);
	glTexCoord2f(1, 1);
	glVertex2f(x + 12, y + 12);
	glTexCoord2f(0, 1);
	glVertex2f(x - 4, y + 12);
	glEnd();

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    } else if (crosshair.value)
	Draw_Character(scr_vrect.x + scr_vrect.width / 2 - 4 +
		       cl_crossx.value,
		       scr_vrect.y + scr_vrect.height / 2 - 4 +
		       cl_crossy.value, '+');
}


/*
=============
Draw_Pic
=============
*/
void
Draw_Pic(int x, int y, const qpic_t *pic)
{
    const glpic_t *glpic;

    glpic = const_container_of(pic, glpic_t, pic);
    Scrap_Flush(glpic->texnum);

    glColor4f(1, 1, 1, 1);
    GL_Bind(glpic->texnum);
    glBegin(GL_QUADS);
    glTexCoord2f(glpic->sl, glpic->tl);
    glVertex2f(x, y);
    glTexCoord2f(glpic->sh, glpic->tl);
    glVertex2f(x + pic->width, y);
    glTexCoord2f(glpic->sh, glpic->th);
    glVertex2f(x + pic->width, y + pic->height);
    glTexCoord2f(glpic->sl, glpic->th);
    glVertex2f(x, y + pic->height);
    glEnd();
}

void
Draw_SubPic(int x, int y, const qpic_t *pic, int srcx, int srcy, int width,
	    int height)
{
    const glpic_t *glpic;
    float newsl, newtl, newsh, newth;
    float oldglwidth, oldglheight;

    glpic = const_container_of(pic, glpic_t, pic);
    Scrap_Flush(glpic->texnum);

    oldglwidth = glpic->sh - glpic->sl;
    oldglheight = glpic->th - glpic->tl;

    newsl = glpic->sl + (srcx * oldglwidth) / pic->width;
    newsh = newsl + (width * oldglwidth) / pic->width;

    newtl = glpic->tl + (srcy * oldglheight) / pic->height;
    newth = newtl + (height * oldglheight) / pic->height;

    glColor4f(1, 1, 1, 1);
    GL_Bind(glpic->texnum);
    glBegin(GL_QUADS);
    glTexCoord2f(newsl, newtl);
    glVertex2f(x, y);
    glTexCoord2f(newsh, newtl);
    glVertex2f(x + width, y);
    glTexCoord2f(newsh, newth);
    glVertex2f(x + width, y + height);
    glTexCoord2f(newsl, newth);
    glVertex2f(x, y + height);
    glEnd();
}

/*
=============
Draw_TransPic
=============
*/
void
Draw_TransPic(int x, int y, const qpic_t *pic)
{
    if (x < 0 || (unsigned)(x + pic->width) > vid.width ||
	y < 0 || (unsigned)(y + pic->height) > vid.height) {
	Sys_Error("%s: bad coordinates", __func__);
    }

    Draw_Pic(x, y, pic);
}


/*
=============
Draw_TransPicTranslate

Only used for the player color selection menu
=============
*/
void
Draw_TransPicTranslate(int x, int y, const qpic_t *pic, byte *translation)
{
    int v, u;
    unsigned trans[64 * 64], *dest;
    byte *src;
    int p;

    GL_Bind(translate_texture);

    dest = trans;
    for (v = 0; v < 64; v++, dest += 64) {
	src = &menuplyr_pixels[((v * pic->height) >> 6) * pic->width];
	for (u = 0; u < 64; u++) {
	    p = src[(u * pic->width) >> 6];
	    if (p == 255)
		dest[u] = p;
	    else
		dest[u] = d_8to24table[translation[p]];
	}
    }

    glTexImage2D(GL_TEXTURE_2D, 0, gl_alpha_format, 64, 64, 0, GL_RGBA,
		 GL_UNSIGNED_BYTE, trans);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(x, y);
    glTexCoord2f(1, 0);
    glVertex2f(x + pic->width, y);
    glTexCoord2f(1, 1);
    glVertex2f(x + pic->width, y + pic->height);
    glTexCoord2f(0, 1);
    glVertex2f(x, y + pic->height);
    glEnd();
}


/*
================
Draw_ConsoleBackground

================
*/
static void
Draw_ConsolePic(int lines, float offset, const qpic_t *pic, float alpha)
{
    const glpic_t *glpic;

    glpic = const_container_of(pic, glpic_t, pic);
    Scrap_Flush(glpic->texnum);

    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glCullFace(GL_FRONT);
    glColor4f(1, 1, 1, alpha);
    GL_Bind(glpic->texnum);

    glBegin (GL_QUADS);
    glTexCoord2f (0, offset);
    glVertex2f (0, 0);
    glTexCoord2f (1, offset);
    glVertex2f (vid.conwidth, 0);
    glTexCoord2f (1, 1);
    glVertex2f (vid.conwidth, lines);
    glTexCoord2f (0, 1);
    glVertex2f (0, lines);
    glEnd();

    glColor4f(1, 1, 1, 1);
    glEnable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
}

void
Draw_ConsoleBackground(int lines)
{
    int y;
    float offset, alpha;

    y = (vid.height * 3) >> 2;

    if (gl_constretch.value)
	offset = 0.0f;
    else
	offset = (vid.conheight - lines) / (float)vid.conheight;

    if (lines > y)
	alpha = 1.0f;
    else
	alpha = (float) 1.1 * lines / y;

    Draw_ConsolePic(lines, offset, &conback->pic, alpha);

#ifdef QW_HACK
    {
	const char *version;
	int x;

	// hack the version number directly into the pic
	y = lines - 14;
	if (!cls.download) {
	    version = va("TyrQuake (%s) QuakeWorld", stringify(TYR_VERSION));
	    x = vid.conwidth - (strlen(version) * 8 + 11) -
		(vid.conwidth * 8 / conback->pic.width) * 7;
	    Draw_Alt_String(x, y, version);
	}
    }
#endif
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void
Draw_TileClear(int x, int y, int w, int h)
{
    const glpic_t *glpic = const_container_of(draw_backtile, glpic_t, pic);

    glColor3f(1, 1, 1);
    GL_Bind(glpic->texnum);
    glBegin(GL_QUADS);
    glTexCoord2f(x / 64.0, y / 64.0);
    glVertex2f(x, y);
    glTexCoord2f((x + w) / 64.0, y / 64.0);
    glVertex2f(x + w, y);
    glTexCoord2f((x + w) / 64.0, (y + h) / 64.0);
    glVertex2f(x + w, y + h);
    glTexCoord2f(x / 64.0, (y + h) / 64.0);
    glVertex2f(x, y + h);
    glEnd();
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void
Draw_Fill(int x, int y, int w, int h, int c)
{
    glDisable(GL_TEXTURE_2D);
    glColor3f(host_basepal[c * 3] / 255.0,
	      host_basepal[c * 3 + 1] / 255.0,
	      host_basepal[c * 3 + 2] / 255.0);

    glBegin(GL_QUADS);

    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);

    glEnd();
    glColor3f(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
}

//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void
Draw_FadeScreen(void)
{
    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColor4f(0, 0, 0, 0.8);
    glBegin(GL_QUADS);

    glVertex2f(0, 0);
    glVertex2f(vid.width, 0);
    glVertex2f(vid.width, vid.height);
    glVertex2f(0, vid.height);

    glEnd();
    glColor4f(1, 1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    Sbar_Changed();
}

//=============================================================================

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void
Draw_BeginDisc(void)
{
    if (!draw_disc)
	return;
    glDrawBuffer(GL_FRONT);
    Draw_Pic(vid.width - 24, 0, draw_disc);
    glDrawBuffer(GL_BACK);
}


/*
================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void
Draw_EndDisc(void)
{
}

/*
================
GL_Set2D

Setup as if the screen was 320*200
================
*/
void
GL_Set2D(void)
{
    glViewport(glx, gly, glwidth, glheight);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, vid.width, vid.height, 0, -99999, 99999);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
//      glDisable(GL_ALPHA_TEST);

    glColor4f(1, 1, 1, 1);
}

//====================================================================

/*
================
GL_FindTexture
================
*/
int
GL_FindTexture(const char *identifier)
{
    int i;
    gltexture_t *glt;

    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	if (!strcmp(identifier, glt->identifier))
	    return gltextures[i].texnum;
    }

    return -1;
}

/*
===============
GL_Upload32
===============
*/
static void
GL_Upload32(qtexture32_t *texture, qboolean mipmap, qboolean alpha)
{
    const int format = alpha ? gl_alpha_format : gl_solid_format;
    qtexture32_t *scaled;
    int width, height, mark;

    /* find the scaled size */
    width = 1;
    while (width < texture->width)
	width <<= 1;
    height = 1;
    while (height < texture->height)
	height <<= 1;

    width >>= (int)gl_picmip.value;
    width = qclamp(width, 1, (int)gl_max_size.value);
    height >>= (int)gl_picmip.value;
    height = qclamp(height, 1, (int)gl_max_size.value);

    mark = Hunk_LowMark();

    if (width != texture->width || height != texture->height) {
	scaled = QTexture32_Alloc(width, height);
	QTexture32_Stretch(texture, scaled);
    } else {
	scaled = texture;
    }

    if (mipmap) {
	int miplevel = 0;
	while (1) {
	    glTexImage2D(GL_TEXTURE_2D, miplevel, format,
			 scaled->width, scaled->height, 0,
			 GL_RGBA, GL_UNSIGNED_BYTE, scaled->pixels);
	    if (scaled->width == 1 && scaled->height == 1)
		break;

	    QTexture32_MipMap(scaled);
	    miplevel++;
	}
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    } else {
	glTexImage2D(GL_TEXTURE_2D, 0, format,
		     scaled->width, scaled->height, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, scaled->pixels);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    }

    Hunk_FreeToLowMark(mark);
}

/*
===============
GL_Upload8
===============
*/
void
GL_Upload8(const byte *data, int width, int height, qboolean mipmap,
	   qboolean alpha)
{
    qtexture32_t *texture;
    int mark;

    mark = Hunk_LowMark();

    texture = QTexture32_Alloc(width, height);
    QTexture32_8to32(data, width, height, width, alpha, texture);

    GL_Upload32(texture, mipmap, alpha);

    Hunk_FreeToLowMark(mark);
}

/*
================
GL_LoadTexture
================
*/
int
GL_LoadTexture(const char *identifier, int width, int height,
	       const byte *data, qboolean mipmap, qboolean alpha)
{
    int i;
    gltexture_t *glt;
    unsigned short crc;

    crc = CRC_Block(data, width * height);

    // see if the texture is already present
    if (identifier[0]) {
	for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	    if (!strcmp(identifier, glt->identifier)) {
		if (crc != glt->crc
		    || width != glt->width || height != glt->height)
		    goto GL_LoadTexture_setup;
		else
		    return glt->texnum;
	    }
	}
    }

    if (numgltextures == MAX_GLTEXTURES)
	Sys_Error("numgltextures == MAX_GLTEXTURES");

    glt = &gltextures[numgltextures];
    numgltextures++;

    strncpy(glt->identifier, identifier, sizeof(glt->identifier) - 1);
    glt->identifier[sizeof(glt->identifier) - 1] = '\0';

    glGenTextures(1, &glt->texnum);

  GL_LoadTexture_setup:
    glt->crc = crc;
    glt->width = width;
    glt->height = height;
    glt->mipmap = mipmap;

#ifdef NQ_HACK
    if (!isDedicated) {
	GL_Bind(glt->texnum);
	GL_Upload8(data, width, height, mipmap, alpha);
    }
#else
    GL_Bind(glt->texnum);
    GL_Upload8(data, width, height, mipmap, alpha);
#endif

    return glt->texnum;
}
