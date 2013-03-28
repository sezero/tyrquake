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

#include "cmd.h"
#include "common.h"
#include "console.h"
#include "draw.h"
#include "keys.h"
#include "menu.h"
#include "sound.h"
#include "vid.h"

#ifdef NQ_HACK
#include "host.h" /* realtime */
#endif
#ifdef QW_HACK
#include "quakedef.h" /* realtime */
#endif

qvidmode_t modelist[MAX_MODE_LIST + 1];
int nummodes;

/* FIXME - vid mode testing */
int vid_testingmode;
int vid_realmode;
double vid_testendtime;

static int vid_line;
static int vid_wmodes;
static int firstupdate = 1;

typedef struct {
    const qvidmode_t *mode;
    const char *desc;
    int iscur;
    int width;
    int height;
} modedesc_t;

#define NUM_WINDOWED_MODES 5
#define MAX_COLUMN_SIZE 5
#define VID_ROW_SIZE 3
#define MODE_AREA_HEIGHT (MAX_COLUMN_SIZE + 7)
#define MAX_MODEDESCS (MAX_COLUMN_SIZE * 3 + NUM_WINDOWED_MODES)

static modedesc_t modedescs[MAX_MODEDESCS];

static const char *VID_GetModeDescription(const qvidmode_t *mode);
static const char *VID_GetModeDescriptionMemCheck(const qvidmode_t *mode);

/*
================
VID_MenuDraw
================
*/
void
VID_MenuDraw(void)
{
    const qpic_t *p;
    const char *ptr;
    int lnummodes, i, j, k, column, row, dup, dupmode;
    char temp[100];
    const qvidmode_t *mode;
    modedesc_t tmodedesc;

    p = Draw_CachePic("gfx/vidmodes.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    for (i = 0; i < NUM_WINDOWED_MODES; i++) {
	ptr = VID_GetModeDescriptionMemCheck(&modelist[i]);
	modedescs[i].mode = &modelist[i];
	modedescs[i].desc = ptr;
	modedescs[i].iscur = 0;

	if (vid_modenum == i)
	    modedescs[i].iscur = 1;
    }

    vid_wmodes = NUM_WINDOWED_MODES;
    lnummodes = nummodes;

    dupmode = 0;	// FIXME - uninitialized -> guesssing 0

    for (i = VID_MODE_FULLSCREEN_DEFAULT; i < lnummodes; i++) {
	ptr = VID_GetModeDescriptionMemCheck(&modelist[i]);
	mode = &modelist[i];

	// we only have room for 15 fullscreen modes, so don't allow
	// 360-wide modes, because if there are 5 320-wide modes and
	// 5 360-wide modes, we'll run out of space
	if (ptr && ((mode->width != 360) || COM_CheckParm("-allow360"))) {
	    dup = 0;
	    for (j = VID_MODE_FULLSCREEN_DEFAULT; j < vid_wmodes; j++) {
		if (!strcmp(modedescs[j].desc, ptr)) {
		    dup = 1;
		    dupmode = j;
		    break;
		}
	    }

	    if (dup || (vid_wmodes < MAX_MODEDESCS)) {
		if (!dup || COM_CheckParm("-noforcevga")) {
		    if (dup) {
			k = dupmode;
		    } else {
			k = vid_wmodes;
		    }

		    modedescs[k].mode = &modelist[i];
		    modedescs[k].desc = ptr;
		    modedescs[k].iscur = 0;
		    modedescs[k].width = mode->width;
		    modedescs[k].height = mode->height;

		    if (i == vid_modenum)
			modedescs[k].iscur = 1;

		    if (!dup)
			vid_wmodes++;
		}
	    }
	}
    }

    /*
     * Sort the modes on width & height
     * (to handle picking up oddball dibonly modes after all the others)
     */
    for (i = VID_MODE_FULLSCREEN_DEFAULT; i < (vid_wmodes - 1); i++) {
	for (j = (i + 1); j < vid_wmodes; j++) {
	    if (modedescs[i].width > modedescs[j].width	||
		(modedescs[i].width == modedescs[j].width &&
		 modedescs[i].height > modedescs[j].height)) {
		tmodedesc = modedescs[i];
		modedescs[i] = modedescs[j];
		modedescs[j] = tmodedesc;
	    }
	}
    }
    M_Print(13 * 8, 36, "Windowed Modes");

    column = 16;
    row = 36 + 2 * 8;

    for (i = 0; i < NUM_WINDOWED_MODES; i++) {
	if (modedescs[i].iscur)
	    M_PrintWhite(column, row, modedescs[i].desc);
	else
	    M_Print(column, row, modedescs[i].desc);

	column += 13 * 8;
	if (!((i + 1) % VID_ROW_SIZE)) {
	    column = 16;
	    row += 8;
	}
    }
    /* go to next row if previous row not filled */
    if (NUM_WINDOWED_MODES % VID_ROW_SIZE)
	row += 8;

    if (vid_wmodes > NUM_WINDOWED_MODES) {
	M_Print(12 * 8, row + 8, "Fullscreen Modes");

	column = 16;
	row += 3 * 8;

	for (i = VID_MODE_FULLSCREEN_DEFAULT; i < vid_wmodes; i++) {
	    if (modedescs[i].iscur)
		M_PrintWhite(column, row, modedescs[i].desc);
	    else
		M_Print(column, row, modedescs[i].desc);

	    column += 13 * 8;
	    if (!((i - NUM_WINDOWED_MODES + 1) % VID_ROW_SIZE)) {
		column = 16;
		row += 8;
	    }
	}
    }

    /* line cursor */
    if (vid_testingmode) {
	snprintf(temp, sizeof(temp), "TESTING %s", modedescs[vid_line].desc);
	M_Print(13 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 4, temp);
	M_Print(9 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 6,
		"Please wait 5 seconds...");
    } else {
	M_Print(9 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8,
		"Press Enter to set mode");
	M_Print(6 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 3,
		"T to test mode for 5 seconds");
	mode = &modelist[vid_modenum];
	ptr = VID_GetModeDescription(mode);

	if (ptr) {
	    snprintf(temp, sizeof(temp), "D to set default: %s", ptr);
	    M_Print(2 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 5, temp);
	}

	mode = &modelist[(int)Cvar_VariableValue("_vid_default_mode_win")];
	ptr = VID_GetModeDescription(mode);
	if (ptr) {
	    snprintf(temp, sizeof(temp), "Current default: %s", ptr);
	    M_Print(3 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 6, temp);
	}

	M_Print(15 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 8, "Esc to exit");

	if (vid_line < NUM_WINDOWED_MODES) {
	    row = 36 + 2 * 8 + (vid_line / VID_ROW_SIZE) * 8;
	    column = 8 + (vid_line % VID_ROW_SIZE) * 13 * 8;
	} else {
	    row = 36 + (5 + (NUM_WINDOWED_MODES + 2) / VID_ROW_SIZE) * 8;
	    row += ((vid_line - NUM_WINDOWED_MODES) / VID_ROW_SIZE) * 8;
	    column = 8 + ((vid_line - NUM_WINDOWED_MODES) % VID_ROW_SIZE) *
		13 * 8;
	}
	M_DrawCharacter(column, row, 12 + ((int)(realtime * 4) & 1));
    }
}

/*
================
VID_MenuKey
================
*/
void
VID_MenuKey(int key)
{
    if (vid_testingmode)
	return;

    switch (key) {
    case K_ESCAPE:
	S_LocalSound("misc/menu1.wav");
	M_Menu_Options_f();
	break;

    case K_LEFTARROW:
	S_LocalSound("misc/menu1.wav");
	if (vid_line < NUM_WINDOWED_MODES) {
	    if (!vid_line) {
		vid_line = VID_ROW_SIZE - 1;
	    } else if (vid_line % VID_ROW_SIZE) {
		vid_line -= 1;
	    } else {
		vid_line += VID_ROW_SIZE - 1;
		if (vid_line >= NUM_WINDOWED_MODES)
		    vid_line = NUM_WINDOWED_MODES - 1;
	    }
	} else if ((vid_line - NUM_WINDOWED_MODES) % VID_ROW_SIZE) {
	    vid_line -= 1;
	} else {
	    vid_line += VID_ROW_SIZE - 1;
	    if (vid_line >= vid_wmodes)
		vid_line = vid_wmodes - 1;
	}
	break;

    case K_RIGHTARROW:
	S_LocalSound("misc/menu1.wav");
	if (vid_line < NUM_WINDOWED_MODES) {
	    if ((vid_line + 1) % VID_ROW_SIZE) {
		vid_line += 1;
		if (vid_line >= NUM_WINDOWED_MODES)
		    vid_line = ((NUM_WINDOWED_MODES - 1) / VID_ROW_SIZE) *
			VID_ROW_SIZE;
	    } else {
		vid_line -= VID_ROW_SIZE - 1;
	    }
	} else if ((vid_line - NUM_WINDOWED_MODES + 1) % VID_ROW_SIZE) {
	    vid_line += 1;
	    if (vid_line >= vid_wmodes)
		vid_line = ((vid_line - NUM_WINDOWED_MODES) / VID_ROW_SIZE) *
		    VID_ROW_SIZE + NUM_WINDOWED_MODES;
	} else {
	    vid_line -= VID_ROW_SIZE - 1;
	}
	break;

    case K_UPARROW:
	S_LocalSound("misc/menu1.wav");
	if (vid_line < NUM_WINDOWED_MODES + VID_ROW_SIZE &&
	    vid_line >= NUM_WINDOWED_MODES) {
	    /* Going from fullscreen section to windowed section */
	    vid_line -= NUM_WINDOWED_MODES % VID_ROW_SIZE;
	    while (vid_line >= NUM_WINDOWED_MODES)
		vid_line -= VID_ROW_SIZE;
	} else if (vid_line < VID_ROW_SIZE) {
	    /* From top to bottom */
	    vid_line += (vid_wmodes / VID_ROW_SIZE + 1) * VID_ROW_SIZE;
	    vid_line += NUM_WINDOWED_MODES % VID_ROW_SIZE;
	    while (vid_line >= vid_wmodes)
		vid_line -= VID_ROW_SIZE;
	} else {
	    vid_line -= VID_ROW_SIZE;
	}
	break;

    case K_DOWNARROW:
	S_LocalSound("misc/menu1.wav");
	if (vid_line < NUM_WINDOWED_MODES &&
	    vid_line + VID_ROW_SIZE >= NUM_WINDOWED_MODES) {
	    /* windowed to fullscreen section */
	    vid_line = NUM_WINDOWED_MODES + (vid_line % VID_ROW_SIZE);
	} else if (vid_line + VID_ROW_SIZE >= vid_wmodes) {
	    /* bottom to top */
	    vid_line = (vid_line - NUM_WINDOWED_MODES) % VID_ROW_SIZE;
	} else {
	    vid_line += VID_ROW_SIZE;
	}
	break;

    case K_ENTER:
	S_LocalSound("misc/menu1.wav");
	VID_SetMode(modedescs[vid_line].mode, host_basepal);
	break;

    case 'T':
    case 't':
	S_LocalSound("misc/menu1.wav");
	// have to set this before setting the mode because WM_PAINT
	// happens during the mode set and does a VID_Update, which
	// checks vid_testingmode
	vid_testingmode = 1;
	vid_testendtime = realtime + 5.0;

	if (!VID_SetMode(modedescs[vid_line].mode, host_basepal)) {
	    vid_testingmode = 0;
	}
	break;

    case 'D':
    case 'd':
	S_LocalSound("misc/menu1.wav");
	firstupdate = 0;
	Cvar_SetValue("_vid_default_mode_win", vid_modenum);
	break;

    default:
	break;
    }
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

    if (mode->fullscreen)
	snprintf(pinfo, sizeof(pinfo), "%4d x %4d x %2d @ %3dHz",
		 mode->width, mode->height, mode->bpp, mode->refresh);
    else
	snprintf(pinfo, sizeof(pinfo), "%4d x %4d windowed",
		 mode->width, mode->height);

    return pinfo;
}

/*
=================
VID_GetModeDescriptionMemCheck
=================
*/
static const char *
VID_GetModeDescriptionMemCheck(const qvidmode_t *mode)
{
    if (VID_CheckAdequateMem(mode->width, mode->height))
	return mode->modedesc;

    return NULL;
}

/*
=================
VID_DescribeModes_f
=================
*/
void
VID_DescribeModes_f(void)
{
    int i, lnummodes;
    const char *pinfo;
    qboolean na;
    const qvidmode_t *mode;

    na = false;

    lnummodes = nummodes;

    for (i = 0; i < lnummodes; i++) {
	mode = &modelist[i];
	pinfo = VID_GetModeDescription(mode);
	if (VID_CheckAdequateMem(mode->width, mode->height)) {
	    Con_Printf("%2d: %s\n", i, pinfo);
	} else {
	    Con_Printf("**: %s\n", pinfo);
	    na = true;
	}
    }

    if (na) {
	Con_Printf("\n[**: not enough system RAM for mode]\n");
    }
}

/*
=================
VID_DescribeCurrentMode_f
=================
*/
void
VID_DescribeCurrentMode_f(void)
{
    Con_Printf("%s\n", VID_GetModeDescription(&modelist[vid_modenum]));
}


/*
=================
VID_NumModes_f
=================
*/
void
VID_NumModes_f(void)
{
    if (nummodes == 1)
	Con_Printf("%d video mode is available\n", nummodes);
    else
	Con_Printf("%d video modes are available\n", nummodes);
}


/*
=================
VID_DescribeMode_f
=================
*/
void
VID_DescribeMode_f(void)
{
    int modenum;

    modenum = Q_atoi(Cmd_Argv(1));
    Con_Printf("%s\n", VID_GetModeDescription(&modelist[modenum]));
}
