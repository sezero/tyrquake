/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2013 Kevin Shanahan

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

#include <stdlib.h>

#include "cmd.h"
#include "common.h"
#include "console.h"
#include "draw.h"
#include "keys.h"
#include "menu.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"
#include "vid.h"

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "quakedef.h"
#endif

#ifdef GLQUAKE
#include "glquake.h"
#else
#include "r_local.h"
#endif

qvidmode_t *vid_modelist;
qvidmode_t vid_windowed_mode;
qvidmode_t vid_fullscreen_mode;

const qvidmode_t *vid_currentmode = &vid_windowed_mode;

int vid_nummodes;

/* FIXME - vid mode testing */
int vid_testingmode;
int vid_realmode;
double vid_testendtime;

static const char *VID_GetModeDescription(const qvidmode_t *mode);

static cvar_t vid_fullscreen = { "vid_fullscreen", "0", CVAR_VIDEO };
static cvar_t vid_width = { "vid_width", "800", CVAR_VIDEO };
static cvar_t vid_height = { "vid_height", "600", CVAR_VIDEO };
static cvar_t vid_bpp = { "vid_bpp", "32", CVAR_VIDEO };
static cvar_t vid_refreshrate = { "vid_refreshrate", "60", CVAR_VIDEO };

cvar_t vid_window_x = { "vid_window_x", "0", CVAR_VIDEO };
cvar_t vid_window_y = { "vid_window_y", "0", CVAR_VIDEO };
cvar_t vid_window_centered = { "vid_window_centered", "1", CVAR_VIDEO };
cvar_t vid_window_remember_position = { "vid_window_remember_position", "1", CVAR_VIDEO };

static void
VID_SetModeCvars(const qvidmode_t *mode)
{
    Cvar_SetValue("vid_fullscreen", (mode == &vid_windowed_mode) ? 0 : 1);
    Cvar_SetValue("vid_width", mode->width);
    Cvar_SetValue("vid_height", mode->height);
    Cvar_SetValue("vid_bpp", mode->bpp);
    Cvar_SetValue("vid_refreshrate", mode->refresh);
}

/* Compare function for qsort - highest res to lowest */
static int
qvidmodecmp(const void *inmode1, const void *inmode2)
{
    const qvidmode_t *const mode1 = inmode1;
    const qvidmode_t *const mode2 = inmode2;

    if (mode1->width < mode2->width)
	return -1;
    if (mode1->width > mode2->width)
	return 1;
    if (mode1->height < mode2->height)
	return -1;
    if (mode1->height > mode2->height)
	return 1;
    if (mode1->bpp < mode2->bpp)
	return -1;
    if (mode1->bpp > mode2->bpp)
	return 1;
    if (mode1->refresh < mode2->refresh)
	return -1;
    if (mode1->refresh > mode2->refresh)
	return 1;

    return 0;
}

void
VID_SortModeList(qvidmode_t *modelist, int nummodes)
{
    /* First entry is for windowed mode */
    qsort(modelist, nummodes, sizeof(qvidmode_t), qvidmodecmp);
}

typedef enum {
    VID_MENU_CURSOR_RESOLUTION,
    VID_MENU_CURSOR_BPP,
    VID_MENU_CURSOR_REFRESH,
    VID_MENU_CURSOR_FULLSCREEN,
    VID_MENU_CURSOR_TEST,
    VID_MENU_CURSOR_APPLY,
    VID_MENU_CURSOR_LINES,
    VID_MENU_CURSOR_WIDTH,
    VID_MENU_CURSOR_HEIGHT,
} vid_menu_cursor_t;

typedef struct {
    qvidmode_t mode;
    qboolean fullscreen;
    qboolean configure_window;
    vid_menu_cursor_t cursor;
} vid_menustate_t;

static vid_menustate_t vid_menustate;

void
VID_MenuInitState(const qvidmode_t *mode)
{
    vid_menustate.mode = *mode;
    vid_menustate.fullscreen = (vid_currentmode != &vid_windowed_mode);
    vid_menustate.configure_window = false;
    vid_menustate.cursor = VID_MENU_CURSOR_RESOLUTION;
}

/*
================
VID_MenuDraw
================
*/
void
VID_MenuDraw_(const vid_menustate_t *menu)
{
    static const int cursor_heights[] = { 48, 56, 64, 72, 88, 96, 0, 64, 80 };
    const qpic8_t *pic;
    const char *text;
    vid_menu_cursor_t cursor = VID_MENU_CURSOR_RESOLUTION;
    int rwidth, rheight, divisor, length;

    /* Calculate relative width/height for aspect ratio */
    divisor = Q_gcd(menu->mode.width, menu->mode.height);
    rwidth = menu->mode.width / divisor;
    rheight = menu->mode.height / divisor;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));

    pic = Draw_CachePic("gfx/p_option.lmp");
    M_DrawTransPic((320 - pic->width) / 2, 4, pic);

    text = "Video Options";
    M_PrintWhite((320 - 8 * strlen(text)) / 2, 32, text);

    M_Print(16, cursor_heights[cursor], "        Video mode");
    M_Print(184, cursor_heights[cursor], va("%ix%i (%i:%i)", menu->mode.width, menu->mode.height, rwidth, rheight));
    cursor++;

    if (menu->configure_window) {
        cursor = VID_MENU_CURSOR_WIDTH;
        // TODO: sub-menu for manual width/height adjustment?
        M_Print(16, cursor_heights[cursor], "             Width");
        M_DrawTextBox(184 - 8, cursor_heights[cursor] - 8, 6, 1);
        if (menu->mode.width)
            M_Print(184, cursor_heights[cursor], va("%i", menu->mode.width));
        cursor++;

        M_Print(16, cursor_heights[cursor], "            Height");
        M_DrawTextBox(184 - 8, cursor_heights[cursor] - 8, 6, 1);
        if (menu->mode.height)
            M_Print(184, cursor_heights[cursor], va("%i", menu->mode.height));
        cursor++;
    } else {
        M_Print(16, cursor_heights[cursor], "       Color depth");
        M_Print(184, cursor_heights[cursor], va("%i", menu->mode.bpp));
        cursor++;

        /* Refresh rate is not always available */
        if (menu->mode.refresh && menu->fullscreen)
            text = va("%i Hz", menu->mode.refresh);
        else
            text = "n/a";
        M_Print(16, cursor_heights[cursor], "      Refresh rate");
        M_Print(184, cursor_heights[cursor], text);
        cursor++;

        M_Print(16, cursor_heights[cursor], "        Fullscreen");
        M_DrawCheckbox(184, cursor_heights[cursor], menu->fullscreen);
        cursor++;

        M_Print(184, cursor_heights[cursor], "Test changes");
        cursor++;

        M_Print (184, cursor_heights[cursor], "Apply changes");
        cursor++;
    }

    /* cursors */
    if (menu->cursor == VID_MENU_CURSOR_WIDTH) {
        length = menu->mode.width ? strlen(va("%d", menu->mode.width)) : 0;
        M_DrawCursor(184 + 8 * length, cursor_heights[menu->cursor], 10);
        M_DrawCharacter(168, cursor_heights[menu->cursor], 13);
    } else if (menu->cursor == VID_MENU_CURSOR_HEIGHT) {
        length = menu->mode.height ? strlen(va("%d", menu->mode.height)) : 0;
        M_DrawCursor(184 + 8 * length, cursor_heights[menu->cursor], 10);
        M_DrawCharacter(168, cursor_heights[menu->cursor], 13);
    } else {
        M_DrawCursor(168, cursor_heights[menu->cursor], 12);
    }
}

void
VID_MenuDraw(void)
{
    VID_MenuDraw_(&vid_menustate);
}

static const qvidmode_t *
VID_FindNextResolution(int width, int height)
{
    const qvidmode_t *mode;

    /* Find where the given resolution fits into the modelist */
    mode = vid_modelist;
    while (mode - vid_modelist < vid_nummodes) {
	if (mode->width > width)
	    break;
	if (mode->width == width && mode->height > height)
	    break;
	mode++;
    }

    /* If we didn't find anything bigger, return the first mode */
    if (mode - vid_modelist == vid_nummodes)
	return vid_modelist;

    return mode;
}

static const qvidmode_t *
VID_FindPrevResolution(int width, int height)
{
    const qvidmode_t *mode;

    /* Find where the given resolution fits into the modelist */
    mode = vid_modelist;
    while (mode - vid_modelist < vid_nummodes) {
	if (mode->width == width && mode->height == height)
	    break;
	if (mode->width > width)
	    break;
	if (mode->width == width && mode->height > height)
	    break;
	mode++;
    }

    /* The preceding resolution is the one we want */
    if (mode == vid_modelist)
	mode = &vid_modelist[vid_nummodes - 1];
    else
	mode--;

    /* Now, run backwards to find the first mode for this resolution */
    while (mode > vid_modelist) {
	const qvidmode_t *check = mode - 1;
	if (check->width != mode->width || check->height != mode->height)
	    break;
	mode--;
    }

    return mode;
}

/*
 * VID_BestModeMatch
 *
 * Find the best match for bpp and refresh in 'mode' in the modelist.
 * 'match' is a pointer to the first mode with matching resolution.
 */
static const qvidmode_t *
VID_BestModeMatch(const qvidmode_t *mode, const qvidmode_t *test)
{
    const qvidmode_t *match;

    for (match = test; test - vid_modelist < vid_nummodes; test++) {
	if (test->width != mode->width || test->height != mode->height)
	    break;

	/* If this bpp is a better match, take match this mode */
	if (abs(test->bpp - mode->bpp) < abs(match->bpp - mode->bpp)) {
	    match = test;
	    continue;
	}
	if (test->bpp != match->bpp)
	    continue;

	/* If the bpp are equal, take the better match for refresh */
	if (abs(test->refresh - mode->refresh) < abs(match->refresh - mode->refresh))
	    match = test;
    }

    return match;
}

static const qvidmode_t *
VID_FindMode(int width, int height, int bpp, int refresh)
{
    const qvidmode_t *mode;

    for (mode = vid_modelist; mode - vid_modelist < vid_nummodes; mode++) {
	if (mode->width != width || mode->height != height)
	    continue;
	if (bpp && mode->bpp != bpp)
	    continue;
        if (refresh && mode->refresh != refresh)
            continue;
	return mode;
    }

    return NULL;
}

static const qvidmode_t *
VID_FindNextBpp(const qvidmode_t *mode)
{
    const qvidmode_t *first, *test, *match, *next;

    /* If we're passed an invalid mode, just return the default mode */
    test = VID_FindMode(mode->width, mode->height, 0, 0);
    if (!test)
	return vid_modelist;

    /*
     * Scan the mode list until we find the entries with matching bpp.
     * Find the next bpp (if any) with the same resolution.
     * If we didn't find the next, loop back to the first entry.
     */
    first = test;
    match = NULL;
    next = NULL;
    while (test - vid_modelist < vid_nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (match && match->bpp != test->bpp) {
	    next = test;
	    break;
	}
	if (!match && test->bpp == mode->bpp)
	    match = test;
	test++;
    }
    if (!next)
	next = first;

    /* Match the refresh rate as best we can for this resolution/bpp */
    test = next;
    while (test - vid_modelist < vid_nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp != next->bpp)
	    break;
	if (abs(test->refresh - mode->refresh) < abs(next->refresh - mode->refresh))
	    next = test;
	test++;
    }

    return next;
}

static const qvidmode_t *
VID_FindPrevBpp(const qvidmode_t *mode)
{
    const qvidmode_t *prev, *test;

    /* If we're passed an invalid mode, just return the default mode */
    test = VID_FindMode(mode->width, mode->height, 0, 0);
    if (!test)
	return vid_modelist;

    /*
     * Scan the mode list until we find the entries with matching bpp.
     * Scan backwards for the previous bpp (if any) with same resolution.
     * If we didn't find a previous, loop back the last one.
     */
    prev = NULL;
    while (test - vid_modelist < vid_nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp == mode->bpp)
	    break;
	prev = test++;
    }
    if (!prev) {
	/* place prev on the last mode for this resolution */
	while (test - vid_modelist < vid_nummodes) {
	    if (test->width != mode->width || test->height != mode->height)
		break;
	    prev = test++;
	}
    }
    /* Should never happen... */
    if (!prev)
	return mode;

    /* Find the best matching refresh at the new bpp */
    test = prev;
    while (test > vid_modelist) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp != prev->bpp)
	    break;
	if (abs(test->refresh - mode->refresh) < abs(prev->refresh - mode->refresh))
	    prev = test;
	test--;
    }

    return prev;
}

static const qvidmode_t *
VID_FindNextRefresh(const qvidmode_t *mode)
{
    const qvidmode_t *test, *first, *next, *match;

    first = NULL;
    for (test = vid_modelist; test - vid_modelist < vid_nummodes; test++) {
	if (test->width != mode->width || test->height != mode->height)
	    continue;
	if (test->bpp != mode->bpp)
	    continue;
	first = test;
	break;
    }

    if (!first)
	return mode;

    next = NULL;
    match = NULL;
    while (test - vid_modelist < vid_nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp != mode->bpp)
	    break;
	if (match && test->refresh != mode->refresh) {
	    next = test;
	    break;
	}
	if (!match && test->refresh == mode->refresh)
	    match = test;
	test++;
    }

    return next ? next : first;
}

static const qvidmode_t *
VID_FindPrevRefresh(const qvidmode_t *mode)
{
    const qvidmode_t *test, *first, *prev;

    first = NULL;
    for (test = vid_modelist; test - vid_modelist < vid_nummodes; test++) {
	if (test->width != mode->width || test->height != mode->height)
	    continue;
	if (test->bpp != mode->bpp)
	    continue;
	first = test;
	break;
    }
    if (!first)
	return mode;

    prev = NULL;
    while (test - vid_modelist < vid_nummodes) {
	if (test->width != mode->width || test->height != mode->height)
	    break;
	if (test->bpp != mode->bpp)
	    break;
	if (test->refresh == mode->refresh && prev)
	    break;
	prev = test++;
    }

    return prev ? prev : mode;
}

static void
VID_Menu_ExitConfigureWindow(vid_menustate_t *menu)
{
    menu->configure_window = false;
    menu->cursor = VID_MENU_CURSOR_APPLY;

#ifndef GLQUAKE
    /* The software render needs width to be a multiple of 8 */
    menu->mode.width &= ~7;
#endif

    menu->mode.width = qclamp(menu->mode.width, MINWIDTH, MAXWIDTH);
    menu->mode.height = qclamp(menu->mode.height, MINHEIGHT, MAXHEIGHT);
}

/*
================
VID_MenuKey
================
*/
void
VID_MenuKey_(vid_menustate_t *menu, knum_t keynum)
{
    const qvidmode_t *mode;
    char string[6];
    int length;

    switch (keynum) {
    case K_ESCAPE:
	S_LocalSound("misc/menu1.wav");
        if (menu->configure_window) {
            VID_Menu_ExitConfigureWindow(menu);
        } else {
            M_Menu_Options_f();
        }
	break;
    case K_UPARROW:
	S_LocalSound("misc/menu1.wav");
	if (menu->cursor == VID_MENU_CURSOR_RESOLUTION)
	    menu->cursor = VID_MENU_CURSOR_LINES - 1;
	else if (menu->cursor == VID_MENU_CURSOR_WIDTH)
            menu->cursor = VID_MENU_CURSOR_HEIGHT;
        else if (menu->cursor == VID_MENU_CURSOR_HEIGHT)
            menu->cursor = VID_MENU_CURSOR_WIDTH;
        else
	    menu->cursor--;
	break;
    case K_DOWNARROW:
	S_LocalSound("misc/menu1.wav");
	if (menu->cursor == VID_MENU_CURSOR_LINES - 1)
	    menu->cursor = VID_MENU_CURSOR_RESOLUTION;
	else if (menu->cursor == VID_MENU_CURSOR_WIDTH)
            menu->cursor = VID_MENU_CURSOR_HEIGHT;
        else if (menu->cursor == VID_MENU_CURSOR_HEIGHT)
            menu->cursor = VID_MENU_CURSOR_WIDTH;
	else
	    menu->cursor++;
	break;
    case K_LEFTARROW:
	S_LocalSound("misc/menu3.wav");
        if (!vid_nummodes)
            break;
	switch (menu->cursor) {
	case VID_MENU_CURSOR_RESOLUTION:
	    mode = VID_FindPrevResolution(menu->mode.width, menu->mode.height);
	    menu->mode.width = mode->width;
	    menu->mode.height = mode->height;
	    mode = VID_BestModeMatch(&menu->mode, mode);
	    menu->mode = *mode;
	    break;
	case VID_MENU_CURSOR_BPP:
	    mode = VID_FindPrevBpp(&menu->mode);
	    menu->mode.bpp = mode->bpp;
	    menu->mode.refresh = mode->refresh;
	    break;
	case VID_MENU_CURSOR_REFRESH:
	    if (menu->fullscreen) {
		mode = VID_FindPrevRefresh(&menu->mode);
		menu->mode.refresh = mode->refresh;
	    }
	    break;
	case VID_MENU_CURSOR_FULLSCREEN:
            if (!menu->fullscreen && !vid_nummodes)
                break; // There may be no fullscreen modes available!
	    menu->fullscreen = !menu->fullscreen;
            if (menu->fullscreen) {
		mode = VID_FindNextRefresh(&menu->mode);
		menu->mode.refresh = mode->refresh;
            }
	    break;
        default:
	    break;
	}
	break;
    case K_RIGHTARROW:
	S_LocalSound("misc/menu3.wav");
        if (!vid_nummodes)
            break;
	switch (menu->cursor) {
        case VID_MENU_CURSOR_RESOLUTION:
	    mode = VID_FindNextResolution(menu->mode.width, menu->mode.height);
	    menu->mode.width = mode->width;
	    menu->mode.height = mode->height;
	    mode = VID_BestModeMatch(&menu->mode, mode);
	    menu->mode = *mode;
	    break;
	case VID_MENU_CURSOR_BPP:
	    mode = VID_FindNextBpp(&menu->mode);
	    menu->mode.bpp = mode->bpp;
	    menu->mode.refresh = mode->refresh;
	    break;
	case VID_MENU_CURSOR_REFRESH:
	    if (menu->fullscreen) {
		mode = VID_FindNextRefresh(&menu->mode);
		menu->mode.refresh = mode->refresh;
	    }
	    break;
	case VID_MENU_CURSOR_FULLSCREEN:
	    menu->fullscreen = !menu->fullscreen;
            if (menu->fullscreen) {
		mode = VID_FindNextRefresh(&menu->mode);
		menu->mode.refresh = mode->refresh;
            }
	    break;
	default:
	    break;
	}
	break;
    case K_ENTER:
	S_LocalSound("misc/menu1.wav");
        if (menu->configure_window) {
            VID_Menu_ExitConfigureWindow(menu);
            break;
        }
	switch (menu->cursor) {
        case VID_MENU_CURSOR_RESOLUTION:
            /* If we're in windowed mode, enter the width/height submenu */
            if (!menu->fullscreen) {
                menu->configure_window = true;
                menu->cursor = VID_MENU_CURSOR_HEIGHT;
            }
            break;
	case VID_MENU_CURSOR_APPLY:
	    /* If it's a windowed mode, update the modelist entry */
	    if (!menu->fullscreen) {
		vid_windowed_mode = menu->mode;
                VID_SetModeCvars(&vid_windowed_mode);
                Cbuf_AddText("vid_restart\n");
		break;
	    }
	    /* If fullscreen, give the existing mode from modelist array */
            mode = VID_FindMode(menu->mode.width, menu->mode.height, menu->mode.bpp, menu->mode.refresh);
            if (mode) {
                VID_SetModeCvars(mode);
                Cbuf_AddText("vid_restart\n");
	    }
	    break;
	case VID_MENU_CURSOR_FULLSCREEN:
	    menu->fullscreen = !menu->fullscreen;
            if (menu->fullscreen) {
		mode = VID_FindNextRefresh(&menu->mode);
		menu->mode.refresh = mode->refresh;
            }
	    break;
	default:
	    break;
	}
        break;
    case K_BACKSPACE:
        if (menu->cursor == VID_MENU_CURSOR_WIDTH) {
            if (menu->mode.width) {
                qsnprintf(string, sizeof(string), "%d", menu->mode.width);
                string[strlen(string) - 1] = 0;
                menu->mode.width = atoi(string);
            }
        } else if (menu->cursor == VID_MENU_CURSOR_HEIGHT) {
            if (menu->mode.height) {
                qsnprintf(string, sizeof(string), "%d", menu->mode.height);
                string[strlen(string) - 1] = 0;
                menu->mode.height = atoi(string);
            }
        }
        break;
    default:
        if (keynum < K_0 || keynum > K_9)
            break;
        if (menu->cursor == VID_MENU_CURSOR_WIDTH) {
            length = qsnprintf(string, sizeof(string), "%d", menu->mode.width);
            if (length < sizeof(string) - 1) {
                string[length] = keynum;
                string[length + 1] = 0;
                menu->mode.width = atoi(string);
            }
        } else if (menu->cursor == VID_MENU_CURSOR_HEIGHT) {
            length = qsnprintf(string, sizeof(string), "%d", menu->mode.height);
            if (length < sizeof(string) - 1) {
                string[length] = keynum;
                string[length + 1] = 0;
                menu->mode.height = atoi(string);
            }
        }
	break;
    }
}

void
VID_MenuKey(knum_t keynum)
{
    VID_MenuKey_(&vid_menustate, keynum);
}

/*
=================
VID_GetModeDescription

Tacks on "windowed" or "fullscreen"
=================
*/
static const char *
VID_GetModeDescription(const qvidmode_t *mode)
{
    static char pinfo[40];

    if (mode != &vid_windowed_mode)
	qsnprintf(pinfo, sizeof(pinfo), "%4d x %4d x %2d @ %3dHz",
		  mode->width, mode->height, mode->bpp, mode->refresh);
    else
	qsnprintf(pinfo, sizeof(pinfo), "%4d x %4d windowed",
		  mode->width, mode->height);

    return pinfo;
}

/*
=================
VID_DescribeCurrentMode_f
=================
*/
static void
VID_DescribeCurrentMode_f(void)
{
    Con_Printf("%s\n", VID_GetModeDescription(vid_currentmode));
}

/*
=================
VID_DescribeModes_f
=================
*/
static void
VID_DescribeModes_f(void)
{
    const qvidmode_t *mode;
    const char *description;
    qboolean unavailable;
    int i;

    if (!vid_nummodes) {
        Con_Printf("No fullscreen modes are available.\n");
        return;
    }

    unavailable = false;

    for (i = 0; i < vid_nummodes; i++) {
        mode = &vid_modelist[i];
        description = VID_GetModeDescription(mode);
        if (VID_CheckAdequateMem(mode->width, mode->height)) {
            Con_Printf("%3d: %s\n", i, description);
        } else {
            Con_Printf(" **: %s\n", description);
            unavailable = true;
        }
    }

    if (unavailable) {
        Con_Printf("\n[**: not enough system RAM for mode]\n");
    }
}

/*
 * Backwards compatibility with the old 'vid_mode' method of video settings
 */
static void
VID_Mode_f()
{
    int modenum;
    const qvidmode_t *mode;

    if (Cmd_Argc() < 2) {
        if (vid_currentmode == &vid_windowed_mode) {
            Con_Printf("\"vid_mode\" is \"windowed\"\n");
        } else {
            Con_Printf("\"vid_mode\" is \"%d\"\n", (int)(vid_currentmode - vid_modelist));
        }
        return;
    }

    mode = vid_currentmode;
    if (!strcmp(Cmd_Argv(1), "windowed")) {
        mode = &vid_windowed_mode;
    } else {
        modenum = Q_atoi(Cmd_Argv(1));
        if (modenum > 0 && modenum < vid_nummodes)
            mode = &vid_modelist[modenum];
    }

    if (mode != vid_currentmode) {
        VID_SetModeCvars(mode);
        Cbuf_AddText("vid_restart\n");
    }
}

const qvidmode_t *
VID_GetModeFromCvars()
{
    const qvidmode_t *mode;
    int width, height, bpp, refresh;

    width = vid_width.value;
    height = vid_height.value;
    bpp = vid_bpp.value;
    refresh = vid_refreshrate.value;

    /* Basic sanity checks */
    if (width < MINWIDTH || width > MAXWIDTH || height < MINHEIGHT || height > MAXHEIGHT)
        return NULL;

    if (vid_fullscreen.value) {
        /* Find the mode in the valid fullscreen modes list */
        mode = VID_FindMode(width, height, bpp, refresh);
    } else {
        /* Set the parameters for the windowed mode */
        vid_windowed_mode.width = width;
        vid_windowed_mode.height = height;
        vid_windowed_mode.bpp = bpp;
        mode = &vid_windowed_mode;
    }

    return mode;
}

/*
 * Check if the mode looks valid, then ask the vid driver to set the mode
 */
static void
VID_Restart_f()
{
    qvidmode_t previous_windowed_mode;
    const qvidmode_t *previous_mode, *new_mode;
    qboolean success;

    if (vid_currentmode == &vid_windowed_mode) {
        previous_windowed_mode = vid_windowed_mode;
        previous_mode = &previous_windowed_mode;
    } else {
        previous_mode = vid_currentmode;
    }

    new_mode = VID_GetModeFromCvars();
    if (!new_mode)
        return;

    scr_block_drawing = true;
    success = VID_SetMode(new_mode, host_basepal);
    if (success) {
        scr_block_drawing = false;
        vid_currentmode = new_mode;
        return;
    }

    /* Attempt to restore the previous video mode instead */
    if (previous_mode == &previous_windowed_mode) {
        vid_windowed_mode = previous_windowed_mode;
    }

    VID_SetMode(vid_currentmode, host_basepal);
    scr_block_drawing = false;
}

void
VID_UpdateWindowPositionCvars(int x, int y)
{
    vrect_t desktop;

    VID_GetDesktopRect(&desktop);
    x = qclamp(x, 0, desktop.width - (MINWIDTH / 2));
    y = qclamp(y, 0, desktop.height - (MINHEIGHT / 2));

    if ((int)vid_window_x.value != x)
        Cvar_SetValue("vid_window_x", x);
    if ((int)vid_window_y.value != y)
        Cvar_SetValue("vid_window_y", y);
}

void
VID_InitModeCvars(void)
{
    Cvar_RegisterVariable(&vid_fullscreen);
    Cvar_RegisterVariable(&vid_width);
    Cvar_RegisterVariable(&vid_height);
    Cvar_RegisterVariable(&vid_bpp);
    Cvar_RegisterVariable(&vid_refreshrate);
    Cvar_RegisterVariable(&vid_window_x);
    Cvar_RegisterVariable(&vid_window_y);
    Cvar_RegisterVariable(&vid_window_centered);
    Cvar_RegisterVariable(&vid_window_remember_position);
}

void
VID_LoadConfig()
{
    // Load video config and init
    Cbuf_InsertText("exec video.cfg\n");
    Cbuf_Execute();
}

void
VID_InitModeCommands()
{
    Cmd_AddCommand("vid_mode", VID_Mode_f);
    Cmd_AddCommand("vid_describecurrentmode", VID_DescribeCurrentMode_f);
    Cmd_AddCommand("vid_describemodes", VID_DescribeModes_f);
    Cmd_AddCommand("vid_restart", VID_Restart_f);
}

/*
 * Check command line paramters to see if any special mode properties
 * were requested. Try to find a matching mode from the modelist.
 */
const qvidmode_t *
VID_GetCmdlineMode(void)
{
    int width, height, bpp, fullscreen, windowed;
    qvidmode_t *mode, *next;

    width = COM_CheckParm("-width");
    if (width)
	width = (com_argc > width + 1) ? atoi(com_argv[width + 1]) : 0;

    height = COM_CheckParm("-height");
    if (height)
	height = (com_argc > height + 1) ? atoi(com_argv[height + 1]) : 0;
    bpp = COM_CheckParm("-bpp");
    if (bpp)
	bpp = (com_argc > bpp + 1) ? atoi(com_argv[bpp + 1]) : 0;
    fullscreen = COM_CheckParm("-fullscreen");
    windowed = COM_CheckParm("-window") || COM_CheckParm("-w");

    /* If nothing was specified, don't return a mode */
    if (!width && !height && !bpp && !fullscreen && !windowed)
	return NULL;

    /* Default to fullscreen mode unless windowed requested */
    fullscreen = fullscreen || !windowed;

    /* FIXME - default to desktop resolution? */
    if (!width && !height) {
	width = vid_windowed_mode.width;
	height = vid_windowed_mode.height;
    } else if (!width) {
	width = height * 4 / 3;
    } else if (!height) {
	height = width * 3 / 4;
    }
    if (!bpp)
	bpp = vid_windowed_mode.bpp;

    /* If windowed mode was requested, set up and return */
    if (!fullscreen) {
	mode = &vid_windowed_mode;
	mode->width = width;
	mode->height = height;
	mode->bpp = bpp;

	return mode;
    }

    /* Search for a matching mode */
    for (mode = vid_modelist; mode - vid_modelist < vid_nummodes; mode++) {
	if (mode->width != width || mode->height != height)
	    continue;
	if (mode->bpp != bpp)
	    continue;
	break;
    }
    if (mode - vid_modelist == vid_nummodes)
	Sys_Error("Requested video mode (%dx%dx%d) not available.", width, height, bpp);

    /* Return the highest refresh rate at this width/height/bpp */
    for (next = mode + 1; next - vid_modelist < vid_nummodes; mode = next++) {
	if (next->width != width || next->height != height)
	    break;
	if (next->bpp != bpp)
	    break;
    }

    return mode;
}

qboolean
VID_IsFullScreen(void)
{
    return vid_currentmode != &vid_windowed_mode;
}
