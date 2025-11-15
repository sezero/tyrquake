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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#include <string.h>

#include "client.h"
#include "cmd.h"
#include "common.h"
#include "console.h"
#include "draw.h"
#include "keys.h"
#include "menu.h"
#include "pcx.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"
#include "tga.h"
#include "vid.h"
#include "view.h"

#ifdef GLQUAKE
#include "glquake.h"
#else
#include "d_iface.h"
#include "r_local.h"
#endif

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "client.h"
#endif

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions

syncronous draw mode or async
One off screen buffer, with updates either copied or xblited
Need to double buffer?

async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint();
SlowPrint();
Screen_Update();
Con_Printf();

net
turn off messages option

the refresh is always rendered, unless the console is full screen

console is:
	notify lines
	half
	full
*/

static qboolean scr_initialized;	/* ready to draw */

// only the refresh window will be updated unless these variables are flagged
int scr_copytop;
int scr_copyeverything;

float scr_con_current;
static float scr_conlines;		/* lines of console to display */

int scr_fullupdate;
static int clearconsole;
int clearnotify;

vrect_t scr_vrect;

qboolean scr_disabled_for_loading;
qboolean scr_block_drawing;
qboolean scr_skipupdate;

static void
SCR_SbarAlpha_f(cvar_t *cvar)
{
    if (cvar->value < 0.0f || cvar->value > 1.0f) {
        cvar->value = qclamp(cvar->value, 0.0f, 1.0f);
        Cvar_SetValue(cvar->name, cvar->value);
    }
}

cvar_t scr_sbaralpha = { "scr_sbaralpha", "0.75", .flags = CVAR_CONFIG, .callback = SCR_SbarAlpha_f };
static cvar_t scr_centertime = { "scr_centertime", "2" };
static cvar_t scr_printspeed = { "scr_printspeed", "8" };

/* Ratio of console background width to backbuffer width */
float scr_conbackscale = 1.0f;

/* Hud scaling, set reasonable defaults - re-calculated when scr_hudscale cvar is registered */
float scr_scale = 1.0f;
int scr_scaled_width = 320;
int scr_scaled_height = 200;

static void
SCR_SetHudscale(float scale)
{
    if (!scale) {
        // Choose a reasonable fractional scale based on 800x600 being 1:1
        scale = (vid.height * 8 / 600) / 8.0f;
        if (scale < 1.0f)
            scale = 1.0f;
    }

    scr_scale = qclamp(scale, 0.25f, 16.0f);
    scr_scaled_width = SCR_ScaleCoord(vid.conwidth);
    scr_scaled_height = SCR_ScaleCoord(vid.conheight);

    Con_CheckResize();

    vid.recalc_refdef = true; /* Since scaling of sb_lines has changed */
}

/*
 * Callback for changes to hud scaling
 */
static void
SCR_Hudscale_Cvar_f(cvar_t *cvar)
{
    // Clamp to reasonable values
    float scale = cvar->value;
    if (scale && (scale < 0.25f || scale > 16.0f)) {
        scale = qclamp(cvar->value, 0.25f, 16.0f);
        Con_Printf("INFO: clamped %s value to %.2f\n", cvar->name, scale);
    }

    SCR_SetHudscale(scale);
}

static cvar_t scr_hudscale = {
    .name = "scr_hudscale",
    .string = "0",
    .flags = CVAR_CONFIG,
    .callback = SCR_Hudscale_Cvar_f
};

void
SCR_CheckResize()
{
    SCR_SetHudscale(scr_hudscale.value);
}

static void
SCR_Hudscale_f()
{
    switch (Cmd_Argc()) {
        case 1:
            Con_Printf("HUD scaling factor: %g%s\n", scr_scale, scr_hudscale.value ? "" : " (automatic)");
            break;
        case 2:
            Cvar_Set("scr_hudscale", Cmd_Argv(1));
            break;
        default:
            Con_Printf("Usage: %s [scaling_factor]\n", Cmd_Argv(0));
            break;
    }
}


cvar_t scr_viewsize = { "viewsize", "110", .flags = CVAR_CONFIG };
cvar_t scr_fov = { "fov", "90" };	// 10 - 170
static cvar_t scr_conspeed = { "scr_conspeed", "300" };
static cvar_t scr_showram = { "showram", "1" };
static cvar_t scr_showturtle = { "showturtle", "0" };
static cvar_t scr_showpause = { "showpause", "1" };
static cvar_t show_fps = { "show_fps", "0" };	/* set for running times */
#ifndef GLQUAKE
static vrect_t *pconupdate;
#endif

static const qpic8_t *scr_ram;
static const qpic8_t *scr_net;
static const qpic8_t *scr_turtle;

static char scr_centerstring[1024];
static float scr_centertime_start;	// for slow victory printing
float scr_centertime_off;
static int scr_center_lines;
static int scr_erase_lines;
static int scr_erase_center;

#ifdef NQ_HACK
static qboolean scr_drawloading;
static float scr_disabled_time;
#endif
#ifdef QW_HACK
static float oldsbar;
static cvar_t scr_allowsnap = { "scr_allowsnap", "0" };
#endif

//=============================================================================

/*
==============
SCR_DrawRam
==============
*/
static void
SCR_DrawRam(void)
{
    if (!scr_showram.value)
	return;

    if (!r_cache_thrash)
	return;

    Draw_Pic(scr_vrect.x + 32, scr_vrect.y, scr_ram);
}


/*
==============
SCR_DrawTurtle
==============
*/
static void
SCR_DrawTurtle(void)
{
    static int count;

    if (!scr_showturtle.value)
	return;

    if (host_frametime < 0.05) {
	count = 0;
	return;
    }

    count++;
    if (count < 3)
	return;

    Draw_Pic(scr_vrect.x, scr_vrect.y, scr_turtle);
}


/*
==============
SCR_DrawNet
==============
*/
static void
SCR_DrawNet(void)
{
#ifdef NQ_HACK
    if (realtime - cl.last_received_message < 0.3)
	return;
#endif
#ifdef QW_HACK
    if (cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged <
	UPDATE_BACKUP - 1)
	return;
#endif

    if (cls.demoplayback)
	return;

    Draw_Pic(scr_vrect.x + 64, scr_vrect.y, scr_net);
}


static void
SCR_DrawFPS(void)
{
    static double lastframetime;
    static int lastfps;
    double t;
    int x, y;
    char st[80];

    if (!show_fps.value)
	return;

    t = Sys_DoubleTime();
    if ((t - lastframetime) >= 1.0) {
	lastfps = fps_count;
	fps_count = 0;
	lastframetime = t;
    }

    qsnprintf(st, sizeof(st), "%3d FPS", lastfps);
    x = scr_scaled_width - strlen(st) * 8 - 8;
    y = scr_scaled_height - sb_lines - 8;
    Draw_String(x, y, st);
}


/*
==============
DrawPause
==============
*/
static void
SCR_DrawPause(void)
{
    const qpic8_t *pic;

    if (!scr_showpause.value)	// turn off for screenshots
	return;

    if (!cl.paused)
	return;

    pic = Draw_CachePic("gfx/pause.lmp");
    Draw_Pic((scr_scaled_width - pic->width) / 2,
	     (scr_scaled_height - 48 - pic->height) / 2, pic);
}

//=============================================================================

/*
==================
SCR_SetUpToDrawConsole
==================
*/
static void
SCR_SetUpToDrawConsole(void)
{
    Con_CheckResize();

#ifdef NQ_HACK
    if (scr_drawloading)
	return;			// never a console with loading plaque
#endif

// decide on the height of the console
#ifdef NQ_HACK
    con_forcedup = !cl.worldmodel || cls.state != ca_active;
#endif
#ifdef QW_HACK
    con_forcedup = cls.state != ca_active;
#endif

    if (con_forcedup) {
	scr_conlines = vid.height;	// full screen
	scr_con_current = scr_conlines;
    } else if (key_dest == key_console)
	scr_conlines = vid.height / 2;	// half screen
    else
	scr_conlines = 0;	// none visible

    /*
     * Calculate console movement based on speed and elapsed time.  We
     * scale the movement speed based on the original base resolution
     * of 320x200.
     */
    if (scr_conlines < scr_con_current) {
	scr_con_current -= scr_conspeed.value * host_frametime * vid.height / 200;
	if (scr_conlines > scr_con_current)
	    scr_con_current = scr_conlines;

    } else if (scr_conlines > scr_con_current) {
	scr_con_current += scr_conspeed.value * host_frametime * vid.height / 200;
	if (scr_conlines < scr_con_current)
	    scr_con_current = scr_conlines;
    }

    if (!vid.numpages || clearconsole++ < vid.numpages) {
#ifdef GLQUAKE
	scr_copytop = 1;
	Draw_TileClear(0, (int)scr_con_current, scr_scaled_width,
		       scr_scaled_height - (int)scr_con_current);
#endif
	Sbar_Changed();
    } else if (clearnotify++ < vid.numpages) {
	scr_copytop = 1;
	Draw_TileClearScaled(0, 0, scr_scaled_width, con_notifylines);
    } else
	con_notifylines = 0;
}


/*
==================
SCR_DrawConsole
==================
*/
static void
SCR_DrawConsole(void)
{
    if (scr_con_current) {
	scr_copyeverything = 1;
	Con_DrawConsole(scr_con_current);
	clearconsole = 0;
    } else {
	if (key_dest == key_game || key_dest == key_message)
	    Con_DrawNotify();	// only draw notify in game
    }
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void
SCR_CenterPrint(const char *str)
{
    strncpy(scr_centerstring, str, sizeof(scr_centerstring));
    scr_centerstring[sizeof(scr_centerstring) - 1] = 0;
    scr_centertime_off = scr_centertime.value;
    scr_centertime_start = cl.time;

    /* count the number of lines for centering */
    scr_center_lines = 1;
    str = scr_centerstring;
    while (*str) {
	if (*str == '\n')
	    scr_center_lines++;
	str++;
    }
}

#ifndef GLQUAKE
static void
SCR_EraseCenterString(void)
{
    int y, height;

    if (scr_erase_center++ > vid.numpages) {
	scr_erase_lines = 0;
	return;
    }

    if (scr_center_lines <= 4)
	y = scr_scaled_height * 0.35;
    else
	y = 48;

    /* Make sure we don't draw off the bottom of the screen*/
    height = qmin(8 * scr_erase_lines, scr_scaled_height - y - 1);

    scr_copytop = 1;
    Draw_TileClearScaled(0, y, scr_scaled_width, height);
}
#endif

static void
SCR_DrawCenterString(void)
{
    char *start;
    int l;
    int j;
    int x, y;
    int remaining;

    scr_copytop = 1;
    if (scr_center_lines > scr_erase_lines)
	scr_erase_lines = scr_center_lines;

    scr_centertime_off -= host_frametime;

    if (scr_centertime_off <= 0 && !cl.intermission)
	return;
    if (key_dest != key_game)
	return;

// the finale prints the characters one at a time
    if (cl.intermission)
	remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
    else
	remaining = 9999;

    scr_erase_center = 0;
    start = scr_centerstring;

    if (scr_center_lines <= 4)
	y = scr_scaled_height * 0.35;
    else
	y = 48;

    do {
	// scan the width of the line
	for (l = 0; l < 40; l++)
	    if (start[l] == '\n' || !start[l])
		break;
	x = (scr_scaled_width - l * 8) / 2;
	for (j = 0; j < l; j++, x += 8) {
	    Draw_Character(x, y, start[j]);
	    if (!remaining--)
		return;
	}

	y += 8;

	while (*start && *start != '\n')
	    start++;

	if (!*start)
	    break;
	start++;		// skip the \n
    } while (1);
}

//=============================================================================

static const char *scr_notifystring;
static qboolean scr_drawdialog;

static void
SCR_DrawNotifyString(void)
{
    const char *start;
    int l;
    int j;
    int x, y;

    start = scr_notifystring;

    y = scr_scaled_height * 0.35;

    do {
	// scan the width of the line
	for (l = 0; l < 40; l++)
	    if (start[l] == '\n' || !start[l])
		break;
	x = (scr_scaled_width - l * 8) / 2;
	for (j = 0; j < l; j++, x += 8)
	    Draw_Character(x, y, start[j]);

	y += 8;

	while (*start && *start != '\n')
	    start++;

	if (!*start)
	    break;
	start++;		// skip the \n
    } while (1);
}


/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.
==================
*/
int
SCR_ModalMessage(const char *text)
{
#ifdef NQ_HACK
    if (cls.state == ca_dedicated)
	return true;
#endif

    scr_notifystring = text;

// draw a fresh screen
    scr_fullupdate = 0;
    scr_drawdialog = true;
    SCR_UpdateScreen();
    scr_drawdialog = false;

    S_ClearBuffer();		// so dma doesn't loop current sound

    do {
	key_count = -1;		// wait for a key down and up
	Sys_SendKeyEvents();
	Sys_Sleep();
    } while (key_lastpress != 'y' && key_lastpress != 'n'
	     && key_lastpress != K_ESCAPE);

    scr_fullupdate = 0;
    SCR_UpdateScreen();

    return key_lastpress == 'y';
}

//============================================================================

/*
====================
CalcFov
====================
*/
static float
SCR_CalcFovY(float fov_x, float width, float height)
{
    fov_x = qclamp(fov_x, 1.0f, 179.0f);

    float x = width / tan(fov_x / 360 * M_PI);
    float a = atan(height / x);
    a = a * 360 / M_PI;

    return a;
}

static float
SCR_CalcFovX(float fov_y, float width, float height)
{
    fov_y = qclamp(fov_y, 1.0f, 179.0f);

    float y = height / tan(fov_y / 360 * M_PI);
    float a = atan(width / y);
    a = a * 360 / M_PI;

    return a;
}

void
SCR_CalcFOV(refdef_t *refdef, float fov)
{
    refdef->fov_x = fov;

    /*
     * Calculate screen aspect based on the passed in refdef.vrect
     *
     * Once aspect is wide enough, we can start to see top and bottom of the view getting clipped
     * off.  Anything more than a 640x432 (~1.5) aspect ratio and we fudge the fov by setting in the
     * vertical direction and using the proportional horizontal fov to match.
     */
    float screen_aspect = (float)refdef->vrect.width / (float)refdef->vrect.height;

    if (screen_aspect < 640.0f / 432.0f) {
        refdef->fov_y = SCR_CalcFovY(refdef->fov_x, refdef->vrect.width, refdef->vrect.height);
    } else {
        refdef->fov_y = SCR_CalcFovY(refdef->fov_x, 640, 432);
        refdef->fov_x = SCR_CalcFovX(refdef->fov_y, refdef->vrect.width, refdef->vrect.height);
    }
}


/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
static void
SCR_CalcRefdef()
{
    vrect_t vrect;
    float size;

    scr_fullupdate = 0;		// force a background redraw
    vid.recalc_refdef = 0;

// force the status bar to redraw
    Sbar_Changed();

//========================================

// bound viewsize
    if (scr_viewsize.value < 30)
	Cvar_Set("viewsize", "30");
    if (scr_viewsize.value > 120)
	Cvar_Set("viewsize", "120");

// bound field of view
    if (scr_fov.value < 10)
	Cvar_Set("fov", "10");
    if (scr_fov.value > 170)
	Cvar_Set("fov", "170");

// intermission is always full screen
    if (cl.intermission)
	size = 120;
    else
	size = scr_viewsize.value;

    if (size >= 120)
	sb_lines = 0;		// no status bar at all
    else if (size >= 110)
	sb_lines = 24;		// no inventory
    else
	sb_lines = 24 + 16 + 8;

    /* Remove tile fill along side status bar when view is >= 100% */
    sb_lines_hidden = scr_viewsize.value < 100.0f ? sb_lines : 0;

// these calculations mirror those in R_Init() for r_refdef, but take no
// account of water warping
    vrect.x = 0;
    vrect.y = 0;
    vrect.width = vid.width;
    vrect.height = vid.height;

    R_SetVrect(&vrect, &scr_vrect, sb_lines_hidden);
    R_SetVrect(&vrect, &r_refdef.vrect, sb_lines_hidden);
    SCR_CalcFOV(&r_refdef, scr_fov.value);
    R_ViewChanged(&vrect, sb_lines_hidden, vid.aspect);

// guard against going from one mode to another that's less than half the
// vertical resolution
    if (scr_con_current > vid.height)
	scr_con_current = vid.height;
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
static void
SCR_SizeUp_f(void)
{
    Cvar_SetValue("viewsize", scr_viewsize.value + 10);
    vid.recalc_refdef = 1;
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void
SCR_SizeDown_f(void)
{
    Cvar_SetValue("viewsize", scr_viewsize.value - 10);
    vid.recalc_refdef = 1;
}

/*
==============================================================================

				SCREEN SHOTS

==============================================================================
*/

static const char *
SCR_ScreenShotFilename(const char *prefix, char *buffer, int buffer_length)
{
    char path[MAX_OSPATH];
    int length = qsnprintf(path, sizeof(path), "%s/%s00.tga", com_gamedir, prefix);
    char *digits = path + length - 6;
    int i;
    for (i = 0; i < 100; i++) {
	digits[0] = i / 10 + '0';
	digits[1] = i % 10 + '0';
	if (Sys_FileTime(path) == -1)
            break;
    }
    if (i == 100)
        return NULL;

    char *filename = path + length - (strlen(prefix) + 6);
    if (strlen(filename) >= buffer_length)
        return NULL;

    qstrncpy(buffer, filename, buffer_length);
    return buffer;
}

#ifdef QW_HACK
/*
Find closest color in the palette for named color
*/
static int
MipColor(int r, int g, int b)
{
    int i;
    float dist;
    int best;
    float bestdist;
    int r1, g1, b1;
    static int lr = -1, lg = -1, lb = -1;
    static int lastbest;

    if (r == lr && g == lg && b == lb)
	return lastbest;

    best = 0;
    bestdist = 256 * 256 * 3;
    for (i = 0; i < 256; i++) {
	r1 = host_basepal[i * 3] - r;
	g1 = host_basepal[i * 3 + 1] - g;
	b1 = host_basepal[i * 3 + 2] - b;
	dist = r1 * r1 + g1 * g1 + b1 * b1;
	if (dist < bestdist) {
	    bestdist = dist;
	    best = i;
	}
    }
    lr = r;
    lg = g;
    lb = b;
    lastbest = best;
    return best;
}

static void
SCR_DrawCharToSnap(int num, byte *dest, int width)
{
    int row = num >> 4;
    int col = num & 15;
    const byte *source = draw_chars + (row << 10) + (col << 3);

    const int stride = -128;
    source -= 7 * stride;

    int drawline = 8;
    while (drawline--) {
	for (int x = 0; x < 8; x++)
	    if (source[x])
		dest[x] = source[x];
	    else
		dest[x] = 98;
	source += stride;
	dest += width;
    }
}

static void
SCR_DrawStringToSnap(const char *s, byte *buf, int x, int y, int width, int height)
{
    byte *dest = buf + (height - y - 8) * width + x;
    const byte *p = (const byte *)s;
    while (*p) {
	SCR_DrawCharToSnap(*p++, dest, width);
	dest += 8;
    }
}

/*
==================
SCR_RSShot_f
==================
*/
static void
SCR_RSShot_f(void)
{
    if (CL_IsUploading())
	return;			// already one pending

    if (cls.state < ca_onserver)
	return;			// gotta be connected

    if (!scr_allowsnap.value) {
	MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	MSG_WriteString(&cls.netchan.message, "snap\n");
	Con_Printf("Refusing remote screen shot request.\n");
	return;
    }

    Con_Printf("Remote screen shot requested.\n");

    int mark = Hunk_LowMark();

#ifdef GLQUAKE
    byte *pixel_buffer = Hunk_AllocName(glwidth * glheight * 3, "screenshot");
    glReadPixels(glx, gly, glwidth, glheight, GL_RGB, GL_UNSIGNED_BYTE, pixel_buffer);

    const int src_width = glwidth;
    const int src_height = glheight;
#else
    D_EnableBackBufferAccess();	// enable direct drawing of console to back

    /* Converting to a 24-bit buffer just to share the code path with GLQuake */
    byte *pixel_buffer = Hunk_AllocName(vid.width * vid.height * 3, "screenshot");
    {
        const byte *src = vid.buffer;
        int stride = vid.rowbytes;
        if (stride < 0) {
            src += stride * (vid.height - 1);
            stride = -stride;
        }

        byte *dst = pixel_buffer;
        for (uint32_t y = 0; y < vid.height; y++, src += stride) {
            for (uint32_t x = 0; x < vid.width; x++) {
                *dst++ = host_basepal[src[x] * 3 + 0];
                *dst++ = host_basepal[src[x] * 3 + 1];
                *dst++ = host_basepal[src[x] * 3 + 2];
            }
        }
    }
    D_DisableBackBufferAccess();

    const int src_width = vid.width;
    const int src_height = vid.height;
#endif

    // Resample
    int w = qmin(src_width, RSSHOT_WIDTH);
    int h = qmin(src_height, RSSHOT_HEIGHT);
    float fracw = (float)src_width / (float)w;
    float frach = (float)src_height / (float)h;

    for (int y = 0; y < h; y++) {
	byte *dest = pixel_buffer + (w * 3 * y);
	for (int x = 0; x < w; x++) {
	    int r = 0;
            int g = 0;
            int b = 0;

	    int dx = x * fracw;
	    int dex = (x + 1) * fracw;
	    if (dex == dx)
		dex++;		// at least one
	    int dy = y * frach;
	    int dey = (y + 1) * frach;
	    if (dey == dy)
		dey++;		// at least one

	    int count = 0;
	    for ( /* */ ; dy < dey; dy++) {
		const byte *src = pixel_buffer + (src_width * 3 * dy) + dx * 3;
		for (int nx = dx; nx < dex; nx++) {
		    r += *src++;
		    g += *src++;
		    b += *src++;
		    count++;
		}
	    }
	    r /= count;
	    g /= count;
	    b /= count;
	    *dest++ = r;
	    *dest++ = g;
	    *dest++ = b;
	}
    }

    // convert to eight bit
    for (int y = 0; y < h; y++) {
	const byte *src = pixel_buffer + (w * 3 * y);
	byte *dest = pixel_buffer + (w * y);
	for (int x = 0; x < w; x++) {
	    *dest++ = MipColor(src[0], src[1], src[2]);
	    src += 3;
	}
    }

    time_t now;
    time(&now);

    char string_buffer[80];
    qstrncpy(string_buffer, ctime(&now), sizeof(string_buffer));
    SCR_DrawStringToSnap(string_buffer, pixel_buffer, w - strlen(string_buffer) * 8, 0, w, h);

    qstrncpy(string_buffer, cls.servername, sizeof(string_buffer));
    SCR_DrawStringToSnap(string_buffer, pixel_buffer, w - strlen(string_buffer) * 8, 10, w, h);

    qstrncpy(string_buffer, name.string, sizeof(string_buffer));
    SCR_DrawStringToSnap(string_buffer, pixel_buffer, w - strlen(string_buffer) * 8, 20, w, h);

    struct tga_hunkfile tga = TGA_CreateHunkFile8(pixel_buffer, w, h, w);
    CL_StartUpload((byte *)tga.data, tga.size);

    Hunk_FreeToLowMark(mark);
    Con_Printf("Sending screenshot to server...\n");
}

#endif /* QW_HACK */

#ifdef GLQUAKE
int glx, gly, glwidth, glheight; // TODO: cleanup these globals */
#endif

/*
==================
SCR_ScreenShot_f
==================
*/
static void
SCR_ScreenShot_f(void)
{
    char filename_buffer[16];
    const char *filename = SCR_ScreenShotFilename("quake", filename_buffer, sizeof(filename_buffer));
    if (!filename) {
        Con_Printf("%s: Couldn't create a TGA file\n", __func__);
        return;
    }

    int mark = Hunk_LowMark();

#ifdef GLQUAKE
    byte *pixel_buffer = Hunk_AllocName(glwidth * glheight * 3, "screenshot");
    glReadPixels(glx, gly, glwidth, glheight, GL_RGB, GL_UNSIGNED_BYTE, pixel_buffer);
    struct tga_hunkfile tga = TGA_CreateHunkFile24(pixel_buffer, glwidth, glheight, glwidth);
#else
    D_EnableBackBufferAccess();
    struct tga_hunkfile tga = TGA_CreateHunkFile8(vid.buffer, vid.width, vid.height, vid.rowbytes);
    D_DisableBackBufferAccess();
#endif

    COM_WriteFile(filename, tga.data, tga.size);
    Hunk_FreeToLowMark(mark);
    Con_Printf("Wrote %s\n", filename);
}

//=============================================================================

#ifdef NQ_HACK
/*
===============
SCR_BeginLoadingPlaque

================
*/
void
SCR_BeginLoadingPlaque(void)
{
    S_StopAllSounds(true);

    if (cls.state != ca_active)
	return;

// redraw with no console and the loading plaque
    Con_ClearNotify();
    scr_centertime_off = 0;
    scr_con_current = 0;

    scr_drawloading = true;
    scr_fullupdate = 0;
    Sbar_Changed();
    SCR_UpdateScreen();
    scr_drawloading = false;

    scr_disabled_for_loading = true;
    scr_disabled_time = realtime;
    scr_fullupdate = 0;
}

/*
==============
SCR_DrawLoading
==============
*/
static void
SCR_DrawLoading(void)
{
    const qpic8_t *pic;

    if (!scr_drawloading)
	return;

    pic = Draw_CachePic("gfx/loading.lmp");
    Draw_Pic((scr_scaled_width - pic->width) / 2,
	     (scr_scaled_height - 48 - pic->height) / 2, pic);
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void
SCR_EndLoadingPlaque(void)
{
    scr_disabled_for_loading = false;
    scr_fullupdate = 0;
    Con_ClearNotify();
}
#endif /* NQ_HACK */

//=============================================================================

#ifdef GLQUAKE
static void
SCR_TileClear(void)
{
    int scaled_sb_lines = SCR_Scale(sb_lines_hidden);

    if (r_refdef.vrect.x > 0) {
	// left
	Draw_TileClear(0, 0, r_refdef.vrect.x, vid.height - scaled_sb_lines);
	// right
	Draw_TileClear(r_refdef.vrect.x + r_refdef.vrect.width, 0,
		       vid.width - r_refdef.vrect.x + r_refdef.vrect.width,
		       vid.height - scaled_sb_lines);
    }
    if (r_refdef.vrect.y > 0) {
	// top
	Draw_TileClear(r_refdef.vrect.x, 0,
		       r_refdef.vrect.x + r_refdef.vrect.width,
		       r_refdef.vrect.y);
	// bottom
	Draw_TileClear(r_refdef.vrect.x,
		       r_refdef.vrect.y + r_refdef.vrect.height,
		       r_refdef.vrect.width,
		       vid.height - scaled_sb_lines - (r_refdef.vrect.height + r_refdef.vrect.y));
    }
}
#endif

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
void
SCR_UpdateScreen(void)
{
    static float old_viewsize, old_fov;
#ifndef GLQUAKE
    vrect_t vrect;

    if (scr_skipupdate)
	return;
#endif
    if (scr_block_drawing)
	return;

#ifdef NQ_HACK
    if (scr_disabled_for_loading) {
	/*
	 * FIXME - this really needs to be fixed properly.
	 * Simply starting a new game and typing "changelevel foo" will hang
	 * the engine for 5s (was 60s!) if foo.bsp does not exist.
	 */
	if (realtime - scr_disabled_time > 5) {
	    scr_disabled_for_loading = false;
	    Con_Printf("load failed.\n");
	} else
	    return;
    }
#endif
#ifdef QW_HACK
    if (scr_disabled_for_loading)
	return;
#endif

#if defined(_WIN32) && !defined(GLQUAKE)
    /* Don't suck up CPU if minimized */
    if (!window_visible())
	return;
#endif

#ifdef NQ_HACK
    if (cls.state == ca_dedicated)
	return;			// stdout only
#endif

    if (!scr_initialized || !con_initialized)
	return;			// not initialized yet

    scr_copytop = 0;
    scr_copyeverything = 0;

    /*
     * Check for vid setting changes
     */
    if (old_fov != scr_fov.value) {
	old_fov = scr_fov.value;
	vid.recalc_refdef = true;
    }
    if (old_viewsize != scr_viewsize.value) {
	old_viewsize = scr_viewsize.value;
	vid.recalc_refdef = true;
    }
#ifdef QW_HACK
    if (oldsbar != cl_sbar.value) {
	oldsbar = cl_sbar.value;
	vid.recalc_refdef = true;
    }
#endif

    if (vid.recalc_refdef)
	SCR_CalcRefdef();

#ifdef GLQUAKE
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
#endif

    /*
     * do 3D refresh drawing, and then update the screen
     */
#ifdef GLQUAKE
    SCR_SetUpToDrawConsole();
#else
    D_EnableBackBufferAccess();	/* for overlay stuff, if drawing directly */

    if (!vid.numpages || scr_fullupdate++ < vid.numpages) {
	/* clear the entire screen */
	scr_copyeverything = 1;
	Draw_TileClear(0, 0, vid.width, vid.height);
	Sbar_Changed();
    }
    pconupdate = NULL;
    SCR_SetUpToDrawConsole();
    SCR_EraseCenterString();

    /* for adapters that can't stay mapped in for linear writes all the time */
    D_DisableBackBufferAccess();

    VID_LockBuffer();
#endif /* !GLQUAKE */

    V_RenderView();

#ifdef GLQUAKE
    GL_Set2D();

    /* draw any areas not covered by the refresh */
    SCR_TileClear();

#ifdef QW_HACK /* FIXME - draw from same place as SW renderer? */
    if (r_netgraph.value)
	R_NetGraph();
#endif

#else /* !GLQUAKE */
    VID_UnlockBuffer();
    D_EnableBackBufferAccess();	// of all overlay stuff if drawing directly
#endif /* !GLQUAKE */

    if (scr_drawdialog) {
	Sbar_Draw();
	if (con_forcedup)
	    Draw_ConsoleBackground(vid.height);
	Draw_FadeScreen();
	SCR_DrawNotifyString();
	scr_copyeverything = true;
#ifdef NQ_HACK
    } else if (scr_drawloading) {
	SCR_DrawLoading();
	Sbar_Draw();
#endif
    } else if (cl.intermission == 1 && key_dest == key_game) {
	Sbar_IntermissionOverlay();
    } else if (cl.intermission == 2 && key_dest == key_game) {
	Sbar_FinaleOverlay();
	SCR_DrawCenterString();
#if defined(NQ_HACK) && !defined(GLQUAKE) /* FIXME? */
    } else if (cl.intermission == 3 && key_dest == key_game) {
	SCR_DrawCenterString();
#endif
    } else {
        Draw_Crosshair();
	SCR_DrawRam();
	SCR_DrawNet();
	SCR_DrawFPS();
	SCR_DrawTurtle();
	SCR_DrawPause();
	SCR_DrawCenterString();
	Sbar_Draw();
	SCR_DrawConsole();
	M_Draw();
    }

#ifndef GLQUAKE
    /* for adapters that can't stay mapped in for linear writes all the time */
    D_DisableBackBufferAccess();
    if (pconupdate)
	D_UpdateRects(pconupdate);
#endif

    V_UpdatePalette();

#ifdef GLQUAKE
    GL_EndRendering();
#else
    /*
     * update one of three areas
     */
    if (scr_copyeverything) {
	vrect.x = 0;
	vrect.y = 0;
	vrect.width = vid.width;
	vrect.height = vid.height;
        vrect.pnext = NULL;
	VID_Update(&vrect);
    } else if (scr_copytop) {
	vrect.x = 0;
	vrect.y = 0;
	vrect.width = vid.width;
	vrect.height = vid.height - SCR_Scale(sb_lines_hidden);
        vrect.pnext = NULL;
	VID_Update(&vrect);
    } else {
	vrect.x = scr_vrect.x;
	vrect.y = scr_vrect.y;
	vrect.width = scr_vrect.width;
	vrect.height = scr_vrect.height;
        vrect.pnext = NULL;
	VID_Update(&vrect);
    }
#endif
}

#if !defined(GLQUAKE) && defined(_WIN32)
/*
==================
SCR_UpdateWholeScreen
FIXME - vid_win.c only?
==================
*/
void
SCR_UpdateWholeScreen(void)
{
    scr_fullupdate = 0;
    SCR_UpdateScreen();
}
#endif

//=============================================================================

void
SCR_AddCommands()
{
    Cmd_AddCommand("hudscale", SCR_Hudscale_f);
    Cmd_AddCommand("screenshot", SCR_ScreenShot_f);
    Cmd_AddCommand("sizeup", SCR_SizeUp_f);
    Cmd_AddCommand("sizedown", SCR_SizeDown_f);
#ifdef QW_HACK
    Cmd_AddCommand("snap", SCR_RSShot_f);
#endif
}

void
SCR_RegisterVariables()
{
    Cvar_RegisterVariable(&scr_fov);
    Cvar_RegisterVariable(&scr_viewsize);
    Cvar_RegisterVariable(&scr_conspeed);
    Cvar_RegisterVariable(&scr_hudscale);
    Cvar_RegisterVariable(&scr_sbaralpha);
    Cvar_RegisterVariable(&scr_showram);
    Cvar_RegisterVariable(&scr_showturtle);
    Cvar_RegisterVariable(&scr_showpause);
    Cvar_RegisterVariable(&scr_centertime);
    Cvar_RegisterVariable(&scr_printspeed);
    Cvar_RegisterVariable(&show_fps);
#ifdef QW_HACK
    Cvar_RegisterVariable(&scr_allowsnap);
#endif
}

/*
==================
SCR_Init
==================
*/
void
SCR_Init(void)
{
    scr_ram = Draw_PicFromWad("ram");
    scr_net = Draw_PicFromWad("net");
    scr_turtle = Draw_PicFromWad("turtle");

    SCR_SetHudscale(scr_hudscale.value);

    scr_initialized = true;
}
