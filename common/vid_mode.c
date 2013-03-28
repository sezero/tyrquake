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


static void
VID_InitModedescs(void)
{
    modedesc_t *modedesc;
    int i, j, dupmode;
    qboolean dup;

    for (i = 0; i < NUM_WINDOWED_MODES; i++) {
	modedescs[i].mode = &modelist[i];
	modedescs[i].iscur = 0;
	if (vid_modenum == i)
	    modedescs[i].iscur = 1;
    }

    vid_wmodes = NUM_WINDOWED_MODES;
    dupmode = VID_MODE_NONE;

    for (i = VID_MODE_FULLSCREEN_DEFAULT; i < nummodes; i++) {
	const qvidmode_t *mode = &modelist[i];
	const char *desc = VID_GetModeDescriptionMemCheck(mode);
	if (!desc)
	    continue;

	// we only have room for 15 fullscreen modes, so don't allow
	// 360-wide modes, because if there are 5 320-wide modes and
	// 5 360-wide modes, we'll run out of space
	if (mode->width == 360 && !COM_CheckParm("-allow360"))
	    continue;

	dup = false;
	for (j = VID_MODE_FULLSCREEN_DEFAULT; j < vid_wmodes; j++) {
	    const qvidmode_t *mode2 = modedescs[j].mode;
	    const char *desc2 = VID_GetModeDescriptionMemCheck(mode2);
	    if (!strcmp(desc, desc2)) {
		dup = true;
		dupmode = j;
		break;
	    }
	}
	if (!dup && vid_wmodes == MAX_MODEDESCS)
	    continue;
	if (dup && !COM_CheckParm("-noforcevga"))
	    continue;

	if (dup) {
	    modedesc = &modedescs[dupmode];
	} else {
	    modedesc = &modedescs[vid_wmodes];
	    vid_wmodes++;
	}
	modedesc->mode = mode;
	modedesc->desc = desc;
	modedesc->iscur = (mode->modenum == vid_modenum);
	modedesc->width = mode->width;
	modedesc->height = mode->height;
    }

    /*
     * Sort the modes on width & height
     * (to handle picking up oddball dibonly modes after all the others)
     */
    for (i = VID_MODE_FULLSCREEN_DEFAULT; i < (vid_wmodes - 1); i++) {
	modedesc_t *desc1 = &modedescs[i];
	for (j = (i + 1); j < vid_wmodes; j++) {
	    modedesc_t *desc2 = &modedescs[j];
	    modedesc_t temp;
	    if (desc1->width < desc2->width)
		continue;
	    if (desc1->width == desc2->width && desc1->height <= desc2->height)
		continue;
	    /* swap */
	    temp = *desc1;
	    *desc1 = *desc2;
	    *desc2 = temp;
	}
    }
}
/*
================
VID_MenuDraw
================
*/
void
VID_MenuDraw(void)
{
    const qvidmode_t *mode;
    const modedesc_t *modedesc;
    const char *desc;
    const qpic_t *pic;
    int i, column, row;
    char temp[100];

    VID_InitModedescs();

    pic = Draw_CachePic("gfx/vidmodes.lmp");
    M_DrawPic((320 - pic->width) / 2, 4, pic);

    M_Print(13 * 8, 36, "Windowed Modes");

    column = 16;
    row = 36 + 2 * 8;
    for (i = 0; i < NUM_WINDOWED_MODES; i++) {
	modedesc = &modedescs[i];
	if (modedesc->iscur)
	    M_PrintWhite(column, row, modedesc->desc);
	else
	    M_Print(column, row, modedesc->desc);

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
	    modedesc = &modedescs[i];
	    if (modedesc->iscur)
		M_PrintWhite(column, row, modedesc->desc);
	    else
		M_Print(column, row, modedesc->desc);

	    column += 13 * 8;
	    if (!((i - NUM_WINDOWED_MODES + 1) % VID_ROW_SIZE)) {
		column = 16;
		row += 8;
	    }
	}
    }

    /* line cursor */
    if (vid_testingmode) {
	modedesc = &modedescs[vid_line];
	snprintf(temp, sizeof(temp), "TESTING %s", modedesc->desc);
	M_Print(13 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 4, temp);
	M_Print(9 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 6,
		"Please wait 5 seconds...");
    } else {
	M_Print(9 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8,
		"Press Enter to set mode");
	M_Print(6 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 3,
		"T to test mode for 5 seconds");
	mode = &modelist[vid_modenum];
	desc = VID_GetModeDescription(mode);
	if (desc) {
	    snprintf(temp, sizeof(temp), "D to set default: %s", desc);
	    M_Print(2 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 5, temp);
	}

	mode = &modelist[(int)Cvar_VariableValue("_vid_default_mode_win")];
	desc = VID_GetModeDescription(mode);
	if (desc) {
	    snprintf(temp, sizeof(temp), "Current default: %s", desc);
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
    int i;
    const char *pinfo;
    qboolean na;
    const qvidmode_t *mode;

    na = false;

    for (i = 0; i < nummodes; i++) {
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
