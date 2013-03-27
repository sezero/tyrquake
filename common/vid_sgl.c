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

#ifdef APPLE_OPENGL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "SDL.h"

#include "console.h"
#include "glquake.h"
#include "input.h"
#include "quakedef.h"
#include "sdl_common.h"
#include "sys.h"
#include "vid.h"

#ifdef NQ_HACK
#include "host.h"
#endif

#define WARP_WIDTH 320
#define WARP_HEIGHT 200

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
unsigned char d_15to8table[65536];

viddef_t vid;

qboolean VID_IsFullScreen(void) { return false; }
void VID_LockBuffer(void) {}
void VID_UnlockBuffer(void) {}

void (*VID_SetGammaRamp)(unsigned short ramp[3][256]) = NULL;

float gldepthmin, gldepthmax;
qboolean gl_mtexable;
cvar_t gl_ztrick = { "gl_ztrick", "1" };

void VID_Update(vrect_t *rects) {}
void D_BeginDirectRect(int x, int y, const byte *pbitmap, int width, int height) {}
void D_EndDirectRect(int x, int y, int width, int height) {}

/*
 * FIXME!!
 *
 * Move stuff around or create abstractions so these hacks aren't needed
 */

#ifndef _WIN32
void Sys_SendKeyEvents(void)
{
    IN_ProcessEvents();
}
#endif

#ifdef _WIN32
#include <windows.h>

qboolean DDActive;
qboolean scr_skipupdate;
HWND mainwindow;
void VID_ForceLockState(int lk) {}
int VID_ForceUnlockedAndReturnState(void) { return 0; }
void VID_SetDefaultMode(void) {}
qboolean window_visible(void) { return true; }
#endif


/*
 * MODESETTING STUFF (command line only)
 * FIXME - I'm sorry, it's horrible :(
 */
typedef struct {
    int width;
    int height;
    int modenum;
    int fullscreen;
    int bpp;
    int refresh;
    char modedesc[13];
    typeof(SDL_PIXELFORMAT_UNKNOWN) format;
} vmode_t;

/*
 * modelist - Last entry is a custom mode for command line parameters
 */
#define MAX_MODE_LIST 1
#define VID_MODE_CMDLINE MAX_MODE_LIST
static vmode_t modelist[MAX_MODE_LIST + 1];

static SDL_PixelFormat *sdl_desktop_format = NULL;

static qboolean
VID_CheckCmdlineMode(void)
{
    int width, height, bpp, fullscreen, windowed;

    printf("com_argc == %d\n", com_argc);

    width = COM_CheckParm("-width");

    printf("width == %d\n", width);

    if (width)
	width = (com_argc > width + 1) ? atoi(com_argv[width + 1]) : 0;

    printf("width == %d\n", width);

    height = COM_CheckParm("-height");
    if (height)
	height = (com_argc > height + 1) ? atoi(com_argv[height + 1]) : 0;
    bpp = COM_CheckParm("-bpp");
    if (bpp)
	bpp = (com_argc > bpp + 1) ? atoi(com_argv[bpp + 1]) : 0;
    fullscreen = COM_CheckParm("-fullscreen");
    windowed = COM_CheckParm("-windowed");

    /* If nothing was specified, just go with the build-in default */
    if (!width && !height && !bpp && !fullscreen && !windowed)
	return false;

    /* Default to windowed mode unless fullscreen requested */
    fullscreen = fullscreen && !windowed;

    if (!width && !height) {
	width = modelist[0].width;
	width = modelist[0].height;
    } else if (!width) {
	width = height * 4 / 3;
    } else if (!height) {
	height = width * 3 / 4;
    }
    if (!bpp)
	bpp = modelist[0].bpp;

    printf("height == %d\n", height);
    printf("bpp == %d\n", bpp);

    modelist[VID_MODE_CMDLINE].modenum = VID_MODE_CMDLINE;
    modelist[VID_MODE_CMDLINE].fullscreen = fullscreen;
    modelist[VID_MODE_CMDLINE].width = width;
    modelist[VID_MODE_CMDLINE].height = height;
    modelist[VID_MODE_CMDLINE].refresh = 60; /* play it safe, I guess? */

    if (bpp == modelist[0].bpp) {
	modelist[VID_MODE_CMDLINE].bpp = bpp;
	modelist[VID_MODE_CMDLINE].format = modelist[0].format;
	return true;
    }

    /* Don't know what to do with wierd layouts */
    switch (SDL_PIXELTYPE(sdl_desktop_format->format)) {
    case SDL_PIXELTYPE_PACKED16:
    case SDL_PIXELTYPE_PACKED32:
	break;
    default:
	modelist[VID_MODE_CMDLINE].bpp = modelist[0].bpp;
	modelist[VID_MODE_CMDLINE].format = modelist[0].format;
	return true;
    }

    /* Also don't know what to do with non 16/32 formats... */
    if (bpp == 15)
	bpp = 16;
    if (bpp == 24)
	bpp = 32;
    if (bpp != 16 && bpp != 32) {
	modelist[VID_MODE_CMDLINE].bpp = modelist[0].bpp;
	modelist[VID_MODE_CMDLINE].format = modelist[0].format;
	return true;
    }

    /*
     * BPP not same as desktop, guess a similar pixel format
     * I suspect this doesn't really work too well...
     */
    modelist[VID_MODE_CMDLINE].bpp = bpp;
    switch (SDL_PIXELORDER(sdl_desktop_format->format)) {
    case SDL_PACKEDORDER_XRGB:
	modelist[VID_MODE_CMDLINE].format = (bpp == 16) ? SDL_PIXELFORMAT_RGB565   : SDL_PIXELFORMAT_RGB888;
	break;
    case SDL_PACKEDORDER_RGBX:
	modelist[VID_MODE_CMDLINE].format = (bpp == 16) ? SDL_PIXELFORMAT_RGB565   : SDL_PIXELFORMAT_RGBA8888;
	break;
    case SDL_PACKEDORDER_ARGB:
	modelist[VID_MODE_CMDLINE].format = (bpp == 16) ? SDL_PIXELFORMAT_ARGB4444 : SDL_PIXELFORMAT_ARGB8888;
	break;
    case SDL_PACKEDORDER_RGBA:
	modelist[VID_MODE_CMDLINE].format = (bpp == 16) ? SDL_PIXELFORMAT_RGBA4444 : SDL_PIXELFORMAT_RGBA8888;
	break;
    case SDL_PACKEDORDER_XBGR:
	modelist[VID_MODE_CMDLINE].format = (bpp == 16) ? SDL_PIXELFORMAT_BGR565   : SDL_PIXELFORMAT_ABGR8888;
	break;
    case SDL_PACKEDORDER_BGRX:
	modelist[VID_MODE_CMDLINE].format = (bpp == 16) ? SDL_PIXELFORMAT_BGR565   : SDL_PIXELFORMAT_BGRA8888;
	break;
    case SDL_PACKEDORDER_ABGR:
	modelist[VID_MODE_CMDLINE].format = (bpp == 16) ? SDL_PIXELFORMAT_ABGR4444 : SDL_PIXELFORMAT_ABGR8888;
	break;
    case SDL_PACKEDORDER_BGRA:
	modelist[VID_MODE_CMDLINE].format = (bpp == 16) ? SDL_PIXELFORMAT_BGRA4444 : SDL_PIXELFORMAT_BGRA8888;
	break;
    default:
	modelist[VID_MODE_CMDLINE].format = modelist[0].format;
    }
    return true;
}

int texture_mode = GL_LINEAR;

static SDL_GLContext gl_context = NULL;

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;
static int gl_num_texture_units;

static qboolean
VID_GL_CheckExtn(const char *extn)
{
    const char *found;
    const int len = strlen(extn);
    char nextc;

    found = strstr(gl_extensions, extn);
    while (found) {
	nextc = found[len];
	if (nextc == ' ' || !nextc)
	    return true;
	found = strstr(found + len, extn);
    }

    return false;
}

static void
VID_InitGL(void)
{
    gl_vendor = (const char *)glGetString(GL_VENDOR);
    gl_renderer = (const char *)glGetString(GL_RENDERER);
    gl_version = (const char *)glGetString(GL_VERSION);
    gl_extensions = (const char *)glGetString(GL_EXTENSIONS);

    printf("GL_VENDOR: %s\n", gl_vendor);
    printf("GL_RENDERER: %s\n", gl_renderer);
    printf("GL_VERSION: %s\n", gl_version);
    printf("GL_EXTENSIONS: %s\n", gl_extensions);

    gl_mtexable = false;
    if (!COM_CheckParm("-nomtex") && VID_GL_CheckExtn("GL_ARB_multitexture")) {
	qglMultiTexCoord2fARB = SDL_GL_GetProcAddress("glMultiTexCoord2fARB");
	qglActiveTextureARB = SDL_GL_GetProcAddress("glActiveTextureARB");

	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &gl_num_texture_units);
	if (gl_num_texture_units >= 2 &&
	    qglMultiTexCoord2fARB && qglActiveTextureARB)
	    gl_mtexable = true;
	Con_Printf("ARB multitexture extension enabled\n"
		   "-> %i texture units available\n",
		   gl_num_texture_units);
    }

    glClearColor(0.5, 0.5, 0.5, 0);
    glCullFace(GL_FRONT);
    glEnable(GL_TEXTURE_2D);

    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.666);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glShadeModel(GL_FLAT);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

static int
VID_SetMode(int modenum, unsigned char *palette)
{
    Uint32 flags;
    int err;
    vmode_t *mode = &modelist[modenum];

    if (gl_context)
	SDL_GL_DeleteContext(gl_context);
    if (sdl_window)
	SDL_DestroyWindow(sdl_window);

    flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;
    if (mode->fullscreen)
	flags |= SDL_WINDOW_FULLSCREEN;

    sdl_window = SDL_CreateWindow("TyrQuake",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  mode->width, mode->height, flags);
    if (!sdl_window)
	Sys_Error("%s: Unable to create window: %s", __func__, SDL_GetError());

    gl_context = SDL_GL_CreateContext(sdl_window);
    if (!gl_context)
	Sys_Error("%s: Unable to create OpenGL context: %s",
		  __func__, SDL_GetError());

    err = SDL_GL_MakeCurrent(sdl_window, gl_context);
    if (err)
	Sys_Error("%s: SDL_GL_MakeCurrent() failed: %s",
		  __func__, SDL_GetError());

    VID_InitGL();

    vid.numpages = 1;
    vid.width = vid.conwidth = mode->width;
    vid.height = vid.conheight = mode->height;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

    return true;
}

void
VID_Init(unsigned char *palette)
{
    int err;
    SDL_DisplayMode desktop_mode;
    qboolean cmdline_mode;

    Cvar_RegisterVariable(&gl_ztrick);

    Q_SDL_InitOnce();
    err = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (err < 0)
	Sys_Error("VID: Couldn't load SDL: %s", SDL_GetError());

    err = SDL_GetDesktopDisplayMode(0, &desktop_mode);
    if (err)
	Sys_Error("%s: Unable to query desktop display mode (%s)",
		  __func__, SDL_GetError());
    sdl_desktop_format = SDL_AllocFormat(desktop_mode.format);
    if (!sdl_desktop_format)
	Sys_Error("%s: Unable to allocate desktop pixel format (%s)",
		  __func__, SDL_GetError());

    /* Quick hack - set the default vid mode here */
    modelist[0].width = 800;
    modelist[0].height = 600;
    modelist[0].modenum = 0;
    modelist[0].fullscreen = 0;
    modelist[0].bpp = sdl_desktop_format->BitsPerPixel;
    modelist[0].format = sdl_desktop_format->format;

    cmdline_mode = VID_CheckCmdlineMode();
    VID_SetMode(cmdline_mode ? VID_MODE_CMDLINE : 0, palette);

    VID_SetPalette(palette);
}

void
VID_Shutdown(void)
{
    if (sdl_window)
	SDL_DestroyWindow(sdl_window);
    if (sdl_desktop_format)
	SDL_FreeFormat(sdl_desktop_format);
}

void
GL_BeginRendering(int *x, int *y, int *width, int *height)
{
    *x = *y = 0;
    *width = vid.width;
    *height = vid.height;
}

void
GL_EndRendering(void)
{
    glFlush();
    SDL_GL_SwapWindow(sdl_window);
}

void
VID_SetPalette(unsigned char *palette)
{
    unsigned i, r, g, b, pixel;

    switch (gl_solid_format) {
    case GL_RGB:
    case GL_RGBA:
	for (i = 0; i < 256; i++) {
	    r = palette[0];
	    g = palette[1];
	    b = palette[2];
	    palette += 3;
	    pixel = (0xff << 24) | (r << 0) | (g << 8) | (b << 16);
	    d_8to24table[i] = LittleLong(pixel);
	}
	d_8to24table[255] &= LittleLong(0xffffff);	// 255 is transparent
	break;
    default:
	Sys_Error("%s: unsupported texture format (%d)", __func__,
		  gl_solid_format);
    }
}

void
VID_ShiftPalette(unsigned char *palette)
{
    /* Done via gl_polyblend instead */
    //VID_SetPalette(palette);
}
