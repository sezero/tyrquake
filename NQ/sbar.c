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
// sbar.c -- status bar code

#include "client.h"
#include "cmd.h"
#include "common.h"
#include "draw.h"
#include "protocol.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "server.h"
#include "vid.h"
#include "wad.h"

/* if >= vid.numpages, no update needed */
int sb_updates;

/* num frame for '-' stats digit */
#define STAT_MINUS 10
const qpic8_t *sb_nums[2][11];
const qpic8_t *sb_colon, *sb_slash;
const qpic8_t *sb_ibar;
const qpic8_t *sb_sbar;
const qpic8_t *sb_scorebar;

/* 0 is active, 1 is owned, 2-5 are flashes */
const qpic8_t *sb_weapons[7][8];

const qpic8_t *sb_ammo[4];
const qpic8_t *sb_sigil[4];
const qpic8_t *sb_armor[3];
const qpic8_t *sb_items[32];

/* [0][] is gibbed, [1][] is dead, [2-6][] are alive */
/* [][0] is static, [][1] is temporary animation */
const qpic8_t *sb_faces[7][2];

const qpic8_t *sb_face_invis;
const qpic8_t *sb_face_quad;
const qpic8_t *sb_face_invuln;
const qpic8_t *sb_face_invis_invuln;

qboolean sb_showscores;

int sb_lines;                   // scan lines to draw
int sb_lines_hidden;            // scan lines obscured totally by the status bar

const qpic8_t *rsb_invbar[2];
const qpic8_t *rsb_weapons[5];
const qpic8_t *rsb_items[2];
const qpic8_t *rsb_ammo[3];
const qpic8_t *rsb_teambord;	// PGM 01/19/97 - team color border

//MED 01/04/97 added two more weapons + 3 alternates for grenade launcher
const qpic8_t *hsb_weapons[7][5]; // 0 is active, 1 is owned, 2-5 are flashes

//MED 01/04/97 added array to simplify weapon parsing
int hipweapons[4] = {
    HIT_LASER_CANNON_BIT,
    HIT_MJOLNIR_BIT,
    4,
    HIT_PROXIMITY_GUN_BIT
};

//MED 01/04/97 added hipnotic items array
const qpic8_t *hsb_items[2];

void Sbar_MiniDeathmatchOverlay(void);
void Sbar_DeathmatchOverlay(void);

/*
===============
Sbar_ShowScores

Tab key down
===============
*/
void
Sbar_ShowScores(void)
{
    if (sb_showscores)
	return;
    sb_showscores = true;
    sb_updates = 0;
}

/*
===============
Sbar_DontShowScores

Tab key up
===============
*/
void
Sbar_DontShowScores(void)
{
    sb_showscores = false;
    sb_updates = 0;
}

/*
===============
Sbar_Changed
===============
*/
void
Sbar_Changed(void)
{
    sb_updates = 0;		// update next frame
}

void
Sbar_AddCommands()
{
    Cmd_AddCommand("+showscores", Sbar_ShowScores);
    Cmd_AddCommand("-showscores", Sbar_DontShowScores);
}

static void
Sbar_InitPics()
{
    int i;

    for (i = 0; i < 10; i++) {
	sb_nums[0][i] = Draw_PicFromWad(va("num_%i", i));
	sb_nums[1][i] = Draw_PicFromWad(va("anum_%i", i));
    }

    sb_nums[0][10] = Draw_PicFromWad("num_minus");
    sb_nums[1][10] = Draw_PicFromWad("anum_minus");

    sb_colon = Draw_PicFromWad("num_colon");
    sb_slash = Draw_PicFromWad("num_slash");

    sb_weapons[0][0] = Draw_PicFromWad("inv_shotgun");
    sb_weapons[0][1] = Draw_PicFromWad("inv_sshotgun");
    sb_weapons[0][2] = Draw_PicFromWad("inv_nailgun");
    sb_weapons[0][3] = Draw_PicFromWad("inv_snailgun");
    sb_weapons[0][4] = Draw_PicFromWad("inv_rlaunch");
    sb_weapons[0][5] = Draw_PicFromWad("inv_srlaunch");
    sb_weapons[0][6] = Draw_PicFromWad("inv_lightng");

    sb_weapons[1][0] = Draw_PicFromWad("inv2_shotgun");
    sb_weapons[1][1] = Draw_PicFromWad("inv2_sshotgun");
    sb_weapons[1][2] = Draw_PicFromWad("inv2_nailgun");
    sb_weapons[1][3] = Draw_PicFromWad("inv2_snailgun");
    sb_weapons[1][4] = Draw_PicFromWad("inv2_rlaunch");
    sb_weapons[1][5] = Draw_PicFromWad("inv2_srlaunch");
    sb_weapons[1][6] = Draw_PicFromWad("inv2_lightng");

    for (i = 0; i < 5; i++) {
	sb_weapons[2 + i][0] = Draw_PicFromWad(va("inva%i_shotgun", i + 1));
	sb_weapons[2 + i][1] = Draw_PicFromWad(va("inva%i_sshotgun", i + 1));
	sb_weapons[2 + i][2] = Draw_PicFromWad(va("inva%i_nailgun", i + 1));
	sb_weapons[2 + i][3] = Draw_PicFromWad(va("inva%i_snailgun", i + 1));
	sb_weapons[2 + i][4] = Draw_PicFromWad(va("inva%i_rlaunch", i + 1));
	sb_weapons[2 + i][5] = Draw_PicFromWad(va("inva%i_srlaunch", i + 1));
	sb_weapons[2 + i][6] = Draw_PicFromWad(va("inva%i_lightng", i + 1));
    }

    sb_ammo[0] = Draw_PicFromWad("sb_shells");
    sb_ammo[1] = Draw_PicFromWad("sb_nails");
    sb_ammo[2] = Draw_PicFromWad("sb_rocket");
    sb_ammo[3] = Draw_PicFromWad("sb_cells");

    sb_armor[0] = Draw_PicFromWad("sb_armor1");
    sb_armor[1] = Draw_PicFromWad("sb_armor2");
    sb_armor[2] = Draw_PicFromWad("sb_armor3");

    sb_items[0] = Draw_PicFromWad("sb_key1");
    sb_items[1] = Draw_PicFromWad("sb_key2");
    sb_items[2] = Draw_PicFromWad("sb_invis");
    sb_items[3] = Draw_PicFromWad("sb_invuln");
    sb_items[4] = Draw_PicFromWad("sb_suit");
    sb_items[5] = Draw_PicFromWad("sb_quad");

    sb_sigil[0] = Draw_PicFromWad("sb_sigil1");
    sb_sigil[1] = Draw_PicFromWad("sb_sigil2");
    sb_sigil[2] = Draw_PicFromWad("sb_sigil3");
    sb_sigil[3] = Draw_PicFromWad("sb_sigil4");

    sb_faces[4][0] = Draw_PicFromWad("face1");
    sb_faces[4][1] = Draw_PicFromWad("face_p1");
    sb_faces[3][0] = Draw_PicFromWad("face2");
    sb_faces[3][1] = Draw_PicFromWad("face_p2");
    sb_faces[2][0] = Draw_PicFromWad("face3");
    sb_faces[2][1] = Draw_PicFromWad("face_p3");
    sb_faces[1][0] = Draw_PicFromWad("face4");
    sb_faces[1][1] = Draw_PicFromWad("face_p4");
    sb_faces[0][0] = Draw_PicFromWad("face5");
    sb_faces[0][1] = Draw_PicFromWad("face_p5");

    sb_face_invis = Draw_PicFromWad("face_invis");
    sb_face_invuln = Draw_PicFromWad("face_invul2");
    sb_face_invis_invuln = Draw_PicFromWad("face_inv2");
    sb_face_quad = Draw_PicFromWad("face_quad");

    sb_sbar = Draw_PicFromWad("sbar");
    sb_ibar = Draw_PicFromWad("ibar");
    sb_scorebar = Draw_PicFromWad("scorebar");

//MED 01/04/97 added new hipnotic weapons
    if (hipnotic) {
	hsb_weapons[0][0] = Draw_PicFromWad("inv_laser");
	hsb_weapons[0][1] = Draw_PicFromWad("inv_mjolnir");
	hsb_weapons[0][2] = Draw_PicFromWad("inv_gren_prox");
	hsb_weapons[0][3] = Draw_PicFromWad("inv_prox_gren");
	hsb_weapons[0][4] = Draw_PicFromWad("inv_prox");

	hsb_weapons[1][0] = Draw_PicFromWad("inv2_laser");
	hsb_weapons[1][1] = Draw_PicFromWad("inv2_mjolnir");
	hsb_weapons[1][2] = Draw_PicFromWad("inv2_gren_prox");
	hsb_weapons[1][3] = Draw_PicFromWad("inv2_prox_gren");
	hsb_weapons[1][4] = Draw_PicFromWad("inv2_prox");

	for (i = 0; i < 5; i++) {
	    hsb_weapons[2 + i][0] =
		Draw_PicFromWad(va("inva%i_laser", i + 1));
	    hsb_weapons[2 + i][1] =
		Draw_PicFromWad(va("inva%i_mjolnir", i + 1));
	    hsb_weapons[2 + i][2] =
		Draw_PicFromWad(va("inva%i_gren_prox", i + 1));
	    hsb_weapons[2 + i][3] =
		Draw_PicFromWad(va("inva%i_prox_gren", i + 1));
	    hsb_weapons[2 + i][4] = Draw_PicFromWad(va("inva%i_prox", i + 1));
	}

	hsb_items[0] = Draw_PicFromWad("sb_wsuit");
	hsb_items[1] = Draw_PicFromWad("sb_eshld");
    }

    if (rogue) {
	rsb_invbar[0] = Draw_PicFromWad("r_invbar1");
	rsb_invbar[1] = Draw_PicFromWad("r_invbar2");

	rsb_weapons[0] = Draw_PicFromWad("r_lava");
	rsb_weapons[1] = Draw_PicFromWad("r_superlava");
	rsb_weapons[2] = Draw_PicFromWad("r_gren");
	rsb_weapons[3] = Draw_PicFromWad("r_multirock");
	rsb_weapons[4] = Draw_PicFromWad("r_plasma");

	rsb_items[0] = Draw_PicFromWad("r_shield1");
	rsb_items[1] = Draw_PicFromWad("r_agrav1");

// PGM 01/19/97 - team color border
	rsb_teambord = Draw_PicFromWad("r_teambord");
// PGM 01/19/97 - team color border

	rsb_ammo[0] = Draw_PicFromWad("r_ammolava");
	rsb_ammo[1] = Draw_PicFromWad("r_ammomulti");
	rsb_ammo[2] = Draw_PicFromWad("r_ammoplasma");
    }
}

/*
===============
Sbar_Init
===============
*/
void
Sbar_Init(void)
{
    Sbar_InitPics();
}

//=============================================================================

// drawing routines are relative to the status bar location

/*
=============
Sbar_DrawPic
=============
*/
static void
Sbar_DrawPic(int x, int y, const qpic8_t *pic)
{
    float alpha = sb_lines_hidden ? 1.0f : scr_sbaralpha.value;
    if (cl.gametype == GAME_DEATHMATCH)
	Draw_PicAlpha(x, y + (scr_scaled_height - SBAR_HEIGHT), pic, alpha);
    else
	Draw_PicAlpha(x + ((scr_scaled_width - 320) >> 1), y + (scr_scaled_height - SBAR_HEIGHT), pic, alpha);
}

/*
=============
Sbar_DrawTransPic
=============
*/
static void
Sbar_DrawTransPic(int x, int y, const qpic8_t *pic)
{
    float alpha = sb_lines_hidden ? 1.0f : scr_sbaralpha.value;
    if (cl.gametype == GAME_DEATHMATCH)
	Draw_TransPicAlpha(x, y + (scr_scaled_height - SBAR_HEIGHT), pic, TRANSPARENT_COLOR, alpha);
    else
	Draw_TransPicAlpha(x + ((scr_scaled_width - 320) >> 1), y + (scr_scaled_height - SBAR_HEIGHT), pic, TRANSPARENT_COLOR, alpha);
}

/*
================
Sbar_DrawCharacter

Draws one solid graphics character
================
*/
static void
Sbar_DrawCharacter(int x, int y, int num)
{
    if (cl.gametype == GAME_DEATHMATCH)
	Draw_CharacterAlpha(x + 4, y + scr_scaled_height - SBAR_HEIGHT, num, scr_sbaralpha.value);
    else
	Draw_CharacterAlpha(x + ((scr_scaled_width - 320) >> 1) + 4, y + scr_scaled_height - SBAR_HEIGHT, num, scr_sbaralpha.value);
}

/*
================
Sbar_DrawString
================
*/
static void
Sbar_DrawString(int x, int y, const char *str)
{
    if (cl.gametype == GAME_DEATHMATCH)
	Draw_StringAlpha(x, y + scr_scaled_height - SBAR_HEIGHT, str, scr_sbaralpha.value);
    else
	Draw_StringAlpha(x + ((scr_scaled_width - 320) >> 1), y + scr_scaled_height - SBAR_HEIGHT, str, scr_sbaralpha.value);
}

/*
=============
Sbar_itoa
=============
*/
static int
Sbar_itoa(int num, char *buf)
{
    char *str;
    int pow10;
    int dig;

    str = buf;

    if (num < 0) {
	*str++ = '-';
	num = -num;
    }

    for (pow10 = 10; num >= pow10; pow10 *= 10);

    do {
	pow10 /= 10;
	dig = num / pow10;
	*str++ = '0' + dig;
	num -= dig * pow10;
    } while (pow10 != 1);

    *str = 0;

    return str - buf;
}


/*
=============
Sbar_DrawNum
=============
*/
static void
Sbar_DrawNum(int x, int y, int num, int digits, int color)
{
    char str[12];
    char *ptr;
    int l, frame;

    l = Sbar_itoa(num, str);
    ptr = str;
    if (l > digits)
	ptr += (l - digits);
    if (l < digits)
	x += (digits - l) * 24;

    while (*ptr) {
	if (*ptr == '-')
	    frame = STAT_MINUS;
	else
	    frame = *ptr - '0';

	Sbar_DrawTransPic(x, y, sb_nums[color][frame]);
	x += 24;
	ptr++;
    }
}

//=============================================================================

int fragsort[MAX_SCOREBOARD];

char scoreboardtext[MAX_SCOREBOARD][20];
int scoreboardtop[MAX_SCOREBOARD];
int scoreboardbottom[MAX_SCOREBOARD];
int scoreboardcount[MAX_SCOREBOARD];
int scoreboardlines;

/*
===============
Sbar_SortFrags
===============
*/
void
Sbar_SortFrags(void)
{
    int i, j, k;

// sort by frags
    scoreboardlines = 0;
    for (i = 0; i < cl.maxclients; i++) {
	if (cl.players[i].name[0]) {
	    fragsort[scoreboardlines] = i;
	    scoreboardlines++;
	}
    }

    for (i = 0; i < scoreboardlines; i++)
	for (j = 0; j < scoreboardlines - 1 - i; j++)
	    if (cl.players[fragsort[j]].frags <
		cl.players[fragsort[j + 1]].frags) {
		k = fragsort[j];
		fragsort[j] = fragsort[j + 1];
		fragsort[j + 1] = k;
	    }
}

static int
Sbar_ColorForMap(int m)
{
    m = qclamp(m, 0, 13) * 16;

    //return m < 128 ? m + 8 : m + 8;
    return m + 8;
}

/*
===============
Sbar_UpdateScoreboard
===============
*/
void
Sbar_UpdateScoreboard(void)
{
    int i, k;
    player_info_t *p;

    Sbar_SortFrags();

// draw the text
    memset(scoreboardtext, 0, sizeof(scoreboardtext));

    for (i = 0; i < scoreboardlines; i++) {
	k = fragsort[i];
	p = &cl.players[k];
	qsnprintf(scoreboardtext[i] + 1, sizeof(scoreboardtext[0]) - 1, "%3i %s", p->frags, p->name);

	scoreboardtop[i] = Sbar_ColorForMap(p->topcolor);
	scoreboardbottom[i] = Sbar_ColorForMap(p->bottomcolor);
    }
}



/*
===============
Sbar_SoloScoreboard
===============
*/
void
Sbar_SoloScoreboard(void)
{
    char str[80];
    int minutes, seconds, tens, units;
    int l;

    qsnprintf(str, sizeof(str), "Monsters:%3i /%3i", cl.stats[STAT_MONSTERS],
	      cl.stats[STAT_TOTALMONSTERS]);
    Sbar_DrawString(8, 4, str);

    qsnprintf(str, sizeof(str), "Secrets :%3i /%3i", cl.stats[STAT_SECRETS],
	      cl.stats[STAT_TOTALSECRETS]);
    Sbar_DrawString(8, 12, str);

// time
    minutes = cl.time / 60;
    seconds = cl.time - 60 * minutes;
    tens = seconds / 10;
    units = seconds - 10 * tens;
    qsnprintf(str, sizeof(str), "Time :%3i:%i%i", minutes, tens, units);
    Sbar_DrawString(184, 4, str);

// draw level name
    l = strlen(cl.levelname);
    Sbar_DrawString(232 - l * 4, 12, cl.levelname);
}

/*
===============
Sbar_DrawScoreboard
===============
*/
void
Sbar_DrawScoreboard(void)
{
    Sbar_SoloScoreboard();
    if (cl.gametype == GAME_DEATHMATCH)
	Sbar_DeathmatchOverlay();
#if 0
    int i, j, c;
    int x, y;
    int l;
    int top, bottom;
    player_info_t *p;

    if (cl.gametype != GAME_DEATHMATCH) {
	Sbar_SoloScoreboard();
	return;
    }

    Sbar_UpdateScoreboard();

    l = scoreboardlines <= 6 ? scoreboardlines : 6;

    for (i = 0; i < l; i++) {
	x = 20 * (i & 1);
	y = i / 2 * 8;

	p = &cl.players[fragsort[i]];
	if (!p->name[0])
	    continue;

	// draw background
	top = Sbar_ColorForMap(p->topcolor);
	bottom = Sbar_ColorForMap(p->bottomcolor);

	Draw_FillAlpha(x * 8 + 10 + ((scr_scaled_width - 320) >> 1),
                       y + scr_scaled_height - SBAR_HEIGHT, 28, 4, top, scr_sbaralpha.value);
	Draw_FillAlpha(x * 8 + 10 + ((scr_scaled_width - 320) >> 1),
                       y + 4 + scr_scaled_height - SBAR_HEIGHT, 28, 4, bottom, scr_sbaralpha.value);

	// draw text
	for (j = 0; j < 20; j++) {
	    c = scoreboardtext[i][j];
	    if (c == 0 || c == ' ')
		continue;
	    Sbar_DrawCharacter((x + j) * 8, y, c);
	}
    }
#endif
}

//=============================================================================

/*
===============
Sbar_DrawInventory
===============
*/
void
Sbar_DrawInventory(void)
{
    int i;
    char num[6];
    float time;
    int flashon;

    if (rogue) {
	if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN)
	    Sbar_DrawPic(0, -24, rsb_invbar[0]);
	else
	    Sbar_DrawPic(0, -24, rsb_invbar[1]);
    } else {
	Sbar_DrawPic(0, -24, sb_ibar);
    }

// weapons
    for (i = 0; i < 7; i++) {
	if (cl.stats[STAT_ITEMS] & (IT_SHOTGUN << i)) {
	    time = cl.item_gettime[i];
	    flashon = qmax((int)((cl.time - time) * 10), 0);
	    if (flashon >= 10) {
		if (cl.stats[STAT_ACTIVEWEAPON] == (IT_SHOTGUN << i))
		    flashon = 1;
		else
		    flashon = 0;
	    } else
		flashon = (flashon % 5) + 2;

	    Sbar_DrawPic(i * 24, -16, sb_weapons[flashon][i]);

	    if (flashon > 1)
		sb_updates = 0;	// force update to remove flash
	}
    }

// MED 01/04/97
// hipnotic weapons
    if (hipnotic) {
	int grenadeflashing = 0;

	for (i = 0; i < 4; i++) {
	    if (cl.stats[STAT_ITEMS] & (1 << hipweapons[i])) {
		time = cl.item_gettime[hipweapons[i]];
		flashon = (int)((cl.time - time) * 10);
		if (flashon >= 10) {
		    if (cl.stats[STAT_ACTIVEWEAPON] == (1 << hipweapons[i]))
			flashon = 1;
		    else
			flashon = 0;
		} else
		    flashon = (flashon % 5) + 2;

		// check grenade launcher
		if (i == 2) {
		    if (cl.stats[STAT_ITEMS] & HIT_PROXIMITY_GUN) {
			if (flashon) {
			    grenadeflashing = 1;
			    Sbar_DrawPic(96, -16, hsb_weapons[flashon][2]);
			}
		    }
		} else if (i == 3) {
		    if (cl.stats[STAT_ITEMS] & (IT_SHOTGUN << 4)) {
			if (flashon && !grenadeflashing) {
			    Sbar_DrawPic(96, -16, hsb_weapons[flashon][3]);
			} else if (!grenadeflashing) {
			    Sbar_DrawPic(96, -16, hsb_weapons[0][3]);
			}
		    } else
			Sbar_DrawPic(96, -16, hsb_weapons[flashon][4]);
		} else
		    Sbar_DrawPic(176 + (i * 24), -16,
				 hsb_weapons[flashon][i]);
		if (flashon > 1)
		    sb_updates = 0;	// force update to remove flash
	    }
	}
    }

    if (rogue) {
	// check for powered up weapon.
	if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN) {
	    for (i = 0; i < 5; i++) {
		if (cl.stats[STAT_ACTIVEWEAPON] == (RIT_LAVA_NAILGUN << i)) {
		    Sbar_DrawPic((i + 2) * 24, -16, rsb_weapons[i]);
		}
	    }
	}
    }
// ammo counts
    for (i = 0; i < 4; i++) {
	qsnprintf(num, sizeof(num), "%3i", cl.stats[STAT_SHELLS + i]);
	if (num[0] != ' ')
	    Sbar_DrawCharacter((6 * i + 1) * 8 - 2, -24, 18 + num[0] - '0');
	if (num[1] != ' ')
	    Sbar_DrawCharacter((6 * i + 2) * 8 - 2, -24, 18 + num[1] - '0');
	if (num[2] != ' ')
	    Sbar_DrawCharacter((6 * i + 3) * 8 - 2, -24, 18 + num[2] - '0');
    }

    flashon = 0;
    // items
    for (i = 0; i < 6; i++)
	if (cl.stats[STAT_ITEMS] & (1 << (17 + i))) {
	    time = cl.item_gettime[17 + i];
	    if (time && time > cl.time - 2 && flashon) {	// flash frame
		sb_updates = 0;
	    } else {
		//MED 01/04/97 changed keys
		if (!hipnotic || (i > 1)) {
		    Sbar_DrawPic(192 + i * 16, -16, sb_items[i]);
		}
	    }
	    if (time && time > cl.time - 2)
		sb_updates = 0;
	}
    //MED 01/04/97 added hipnotic items
    // hipnotic items
    if (hipnotic) {
	for (i = 0; i < 2; i++)
	    if (cl.stats[STAT_ITEMS] & (1 << (24 + i))) {
		time = cl.item_gettime[24 + i];
		if (time && time > cl.time - 2 && flashon) {	// flash frame
		    sb_updates = 0;
		} else {
		    Sbar_DrawPic(288 + i * 16, -16, hsb_items[i]);
		}
		if (time && time > cl.time - 2)
		    sb_updates = 0;
	    }
    }

    if (rogue) {
	// new rogue items
	for (i = 0; i < 2; i++) {
	    if (cl.stats[STAT_ITEMS] & (1 << (29 + i))) {
		time = cl.item_gettime[29 + i];

		if (time && time > cl.time - 2 && flashon) {	// flash frame
		    sb_updates = 0;
		} else {
		    Sbar_DrawPic(288 + i * 16, -16, rsb_items[i]);
		}

		if (time && time > cl.time - 2)
		    sb_updates = 0;
	    }
	}
    } else {
	// sigils
	for (i = 0; i < 4; i++) {
	    if (cl.stats[STAT_ITEMS] & (1 << (28 + i))) {
		time = cl.item_gettime[28 + i];
		if (time && time > cl.time - 2 && flashon) {	// flash frame
		    sb_updates = 0;
		} else
		    Sbar_DrawPic(320 - 32 + i * 8, -16, sb_sigil[i]);
		if (time && time > cl.time - 2)
		    sb_updates = 0;
	    }
	}
    }
}

//=============================================================================

/*
===============
Sbar_DrawFrags
===============
*/
void
Sbar_DrawFrags(void)
{
    int i, k, l;
    int top, bottom;
    int x, y, f;
    int xofs;
    char num[12];
    player_info_t *p;

    Sbar_SortFrags();

// draw the text
    l = scoreboardlines <= 4 ? scoreboardlines : 4;

    x = 23;
    if (cl.gametype == GAME_DEATHMATCH)
	xofs = 0;
    else
	xofs = (scr_scaled_width - 320) >> 1;
    y = scr_scaled_height - SBAR_HEIGHT - 23;

    for (i = 0; i < l; i++) {
	k = fragsort[i];
	p = &cl.players[k];
	if (!p->name[0])
	    continue;

	// draw background
	top = Sbar_ColorForMap(p->topcolor);
	bottom = Sbar_ColorForMap(p->bottomcolor);

	Draw_FillAlpha(xofs + x * 8 + 10, y, 28, 4, top, scr_sbaralpha.value);
	Draw_FillAlpha(xofs + x * 8 + 10, y + 4, 28, 3, bottom, scr_sbaralpha.value);

	// draw number
	f = p->frags;
	qsnprintf(num, sizeof(num), "%3i", f);

	Sbar_DrawCharacter((x + 1) * 8, -24, num[0]);
	Sbar_DrawCharacter((x + 2) * 8, -24, num[1]);
	Sbar_DrawCharacter((x + 3) * 8, -24, num[2]);

	if (k == cl.viewentity - 1) {
	    Sbar_DrawCharacter(x * 8 + 2, -24, 16);
	    Sbar_DrawCharacter((x + 4) * 8 - 4, -24, 17);
	}
	x += 4;
    }
}

//=============================================================================


/*
===============
Sbar_DrawFace
===============
*/
void
Sbar_DrawFace(void)
{
    int f, anim;

// PGM 01/19/97 - team color drawing
// PGM 03/02/97 - fixed so color swatch only appears in CTF modes
    if (rogue &&
	(cl.maxclients != 1) &&
	(teamplay.value > 3) && (teamplay.value < 7)) {
	int top, bottom;
	int xofs;
	char num[12];
	player_info_t *p;

	p = &cl.players[cl.viewentity - 1];
	// draw background
	top = Sbar_ColorForMap(p->topcolor);
	bottom = Sbar_ColorForMap(p->bottomcolor);

	if (cl.gametype == GAME_DEATHMATCH)
	    xofs = 113;
	else
	    xofs = ((vid.width - 320) >> 1) + 113;

	Sbar_DrawPic(112, 0, rsb_teambord);
	Draw_FillAlpha(xofs, scr_scaled_height - SBAR_HEIGHT + 3, 22, 9, top, scr_sbaralpha.value);
	Draw_FillAlpha(xofs, scr_scaled_height - SBAR_HEIGHT + 12, 22, 9, bottom, scr_sbaralpha.value);

	// draw number
	f = p->frags;
	qsnprintf(num, sizeof(num), "%3i", f);

	if (top == 8) {
	    if (num[0] != ' ')
		Sbar_DrawCharacter(109, 3, 18 + num[0] - '0');
	    if (num[1] != ' ')
		Sbar_DrawCharacter(116, 3, 18 + num[1] - '0');
	    if (num[2] != ' ')
		Sbar_DrawCharacter(123, 3, 18 + num[2] - '0');
	} else {
	    Sbar_DrawCharacter(109, 3, num[0]);
	    Sbar_DrawCharacter(116, 3, num[1]);
	    Sbar_DrawCharacter(123, 3, num[2]);
	}

	return;
    }
// PGM 01/19/97 - team color drawing

    if ((cl.stats[STAT_ITEMS] & (IT_INVISIBILITY | IT_INVULNERABILITY))
	== (IT_INVISIBILITY | IT_INVULNERABILITY)) {
	Sbar_DrawPic(112, 0, sb_face_invis_invuln);
	return;
    }
    if (cl.stats[STAT_ITEMS] & IT_QUAD) {
	Sbar_DrawPic(112, 0, sb_face_quad);
	return;
    }
    if (cl.stats[STAT_ITEMS] & IT_INVISIBILITY) {
	Sbar_DrawPic(112, 0, sb_face_invis);
	return;
    }
    if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY) {
	Sbar_DrawPic(112, 0, sb_face_invuln);
	return;
    }

    if (cl.stats[STAT_HEALTH] >= 100)
	f = 4;
    else
	f = cl.stats[STAT_HEALTH] / 20;

    if (cl.time <= cl.faceanimtime) {
	anim = 1;
	sb_updates = 0;		// make sure the anim gets drawn over
    } else
	anim = 0;
    Sbar_DrawPic(112, 0, sb_faces[f][anim]);
}

/*
===============
Sbar_Draw
===============
*/
void
Sbar_Draw(void)
{
    if (scr_con_current == vid.height)
	return;			// console is full screen

    if (sb_lines_hidden == sb_lines && vid.numpages && sb_updates >= vid.numpages)
	return;

    scr_copyeverything = 1;

    sb_updates++;

    if (sb_lines_hidden && scr_scaled_width > 320)
        Draw_TileClearScaled(0, scr_scaled_height - sb_lines_hidden, scr_scaled_width, sb_lines_hidden);

    if (sb_lines > 24) {
	Sbar_DrawInventory();
	if (cl.maxclients != 1)
	    Sbar_DrawFrags();
    }

    if (sb_showscores || cl.stats[STAT_HEALTH] <= 0) {
	Sbar_DrawPic(0, 0, sb_scorebar);
	Sbar_DrawScoreboard();
	sb_updates = 0;
    } else if (sb_lines) {
	Sbar_DrawPic(0, 0, sb_sbar);

	// keys (hipnotic only)
	//MED 01/04/97 moved keys here so they would not be overwritten
	if (hipnotic) {
	    if (cl.stats[STAT_ITEMS] & IT_KEY1)
		Sbar_DrawPic(209, 3, sb_items[0]);
	    if (cl.stats[STAT_ITEMS] & IT_KEY2)
		Sbar_DrawPic(209, 12, sb_items[1]);
	}
	// armor
	if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY) {
	    Sbar_DrawNum(24, 0, 666, 3, 1);
	    Sbar_DrawPic(0, 0, draw_disc);
	} else {
	    if (rogue) {
		Sbar_DrawNum(24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
		if (cl.stats[STAT_ITEMS] & RIT_ARMOR3)
		    Sbar_DrawPic(0, 0, sb_armor[2]);
		else if (cl.stats[STAT_ITEMS] & RIT_ARMOR2)
		    Sbar_DrawPic(0, 0, sb_armor[1]);
		else if (cl.stats[STAT_ITEMS] & RIT_ARMOR1)
		    Sbar_DrawPic(0, 0, sb_armor[0]);
	    } else {
		Sbar_DrawNum(24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
		if (cl.stats[STAT_ITEMS] & IT_ARMOR3)
		    Sbar_DrawPic(0, 0, sb_armor[2]);
		else if (cl.stats[STAT_ITEMS] & IT_ARMOR2)
		    Sbar_DrawPic(0, 0, sb_armor[1]);
		else if (cl.stats[STAT_ITEMS] & IT_ARMOR1)
		    Sbar_DrawPic(0, 0, sb_armor[0]);
	    }
	}

	// face
	Sbar_DrawFace();

	// health
	Sbar_DrawNum(136, 0, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);

	// ammo icon
	if (rogue) {
	    if (cl.stats[STAT_ITEMS] & RIT_SHELLS)
		Sbar_DrawPic(224, 0, sb_ammo[0]);
	    else if (cl.stats[STAT_ITEMS] & RIT_NAILS)
		Sbar_DrawPic(224, 0, sb_ammo[1]);
	    else if (cl.stats[STAT_ITEMS] & RIT_ROCKETS)
		Sbar_DrawPic(224, 0, sb_ammo[2]);
	    else if (cl.stats[STAT_ITEMS] & RIT_CELLS)
		Sbar_DrawPic(224, 0, sb_ammo[3]);
	    else if (cl.stats[STAT_ITEMS] & RIT_LAVA_NAILS)
		Sbar_DrawPic(224, 0, rsb_ammo[0]);
	    else if (cl.stats[STAT_ITEMS] & RIT_PLASMA_AMMO)
		Sbar_DrawPic(224, 0, rsb_ammo[1]);
	    else if (cl.stats[STAT_ITEMS] & RIT_MULTI_ROCKETS)
		Sbar_DrawPic(224, 0, rsb_ammo[2]);
	} else {
	    if (cl.stats[STAT_ITEMS] & IT_SHELLS)
		Sbar_DrawPic(224, 0, sb_ammo[0]);
	    else if (cl.stats[STAT_ITEMS] & IT_NAILS)
		Sbar_DrawPic(224, 0, sb_ammo[1]);
	    else if (cl.stats[STAT_ITEMS] & IT_ROCKETS)
		Sbar_DrawPic(224, 0, sb_ammo[2]);
	    else if (cl.stats[STAT_ITEMS] & IT_CELLS)
		Sbar_DrawPic(224, 0, sb_ammo[3]);
	}

	Sbar_DrawNum(248, 0, cl.stats[STAT_AMMO], 3, cl.stats[STAT_AMMO] <= 10);
    }

    if (scr_scaled_width > 320) {
	if (cl.gametype == GAME_DEATHMATCH)
	    Sbar_MiniDeathmatchOverlay();
    }
}

//=============================================================================

/*
==================
Sbar_IntermissionNumber

==================
*/
void
Sbar_IntermissionNumber(int x, int y, int num, int digits, int color)
{
    char str[12];
    char *ptr;
    int l, frame;

    l = Sbar_itoa(num, str);
    ptr = str;
    if (l > digits)
	ptr += (l - digits);
    if (l < digits)
	x += (digits - l) * 24;

    while (*ptr) {
	if (*ptr == '-')
	    frame = STAT_MINUS;
	else
	    frame = *ptr - '0';

	Draw_TransPic(x, y, sb_nums[color][frame], TRANSPARENT_COLOR);
	x += 24;
	ptr++;
    }
}

/*
==================
Sbar_DeathmatchOverlay

==================
*/
void
Sbar_DeathmatchOverlay(void)
{
    const qpic8_t *pic;
    int i, k, l;
    int top, bottom;
    int x, y, f;
    char num[12];
    player_info_t *p;

    scr_copyeverything = 1;
    scr_fullupdate = 0;

    pic = Draw_CachePic("gfx/ranking.lmp");
    Draw_Pic(160 - pic->width / 2, 8, pic);

// scores
    Sbar_SortFrags();

// draw the text
    l = scoreboardlines;

    x = 80 + ((scr_scaled_width - 320) >> 1);
    y = 40;
    for (i = 0; i < l; i++) {
	k = fragsort[i];
	p = &cl.players[k];
	if (!p->name[0])
	    continue;

	// draw background
	top = Sbar_ColorForMap(p->topcolor);
	bottom = Sbar_ColorForMap(p->bottomcolor);

	Draw_FillAlpha(x, y, 40, 4, top, scr_sbaralpha.value);
	Draw_FillAlpha(x, y + 4, 40, 4, bottom, scr_sbaralpha.value);

	// draw number
	f = p->frags;
	qsnprintf(num, sizeof(num), "%3i", f);

	Draw_CharacterAlpha(x + 8, y, num[0], scr_sbaralpha.value);
	Draw_CharacterAlpha(x + 16, y, num[1], scr_sbaralpha.value);
	Draw_CharacterAlpha(x + 24, y, num[2], scr_sbaralpha.value);

	if (k == cl.viewentity - 1)
	    Draw_CharacterAlpha(x - 8, y, 12, scr_sbaralpha.value);

#if 0
	{
	    int total;
	    int n, minutes, tens, units;

	    // draw time
	    total = cl.completed_time - p->entertime;
	    minutes = (int)total / 60;
	    n = total - minutes * 60;
	    tens = n / 10;
	    units = n % 10;

	    qsnprintf(num, sizeof(num), "%3i:%i%i", minutes, tens, units);

	    Draw_StringAlpha(x + 48, y, num, scr_sbaralpha.value);
	}
#endif

	// draw name
	Draw_StringAlpha(x + 64, y, p->name, scr_sbaralpha.value);

	y += 10;
    }
}

/*
==================
Sbar_DeathmatchOverlay

==================
*/
void
Sbar_MiniDeathmatchOverlay(void)
{
    int x, y, line, numlines, top, bottom;

    /* Don't bother if not enough room */
    if (scr_scaled_width < 512 || !sb_lines)
	return;

    scr_copyeverything = 1;
    scr_fullupdate = 0;

    Sbar_SortFrags();

    /* Check for space to draw the text */
    y = scr_scaled_height - sb_lines;
    numlines = sb_lines / 8;
    if (numlines < 3)
	return;

    /* Find client in the scoreboard, if not there (spectator) display top */
    for (line = 0; line < scoreboardlines; line++)
	if (fragsort[line] == cl.viewentity - 1)
	    break;
    if (line == scoreboardlines)
	line = 0;

    /* Put the client in the centre of the displayed lines */
    line = qclamp(line - numlines / 2, 0, scoreboardlines - numlines);

    x = 324;
    while (line < scoreboardlines && y < scr_scaled_height - 8 + 1) {
	const int playernum = fragsort[line++];
	const player_info_t *player = &cl.players[playernum];
	if (!player->name[0])
	    continue;

	/* draw background */
	top = Sbar_ColorForMap(player->topcolor);
	bottom = Sbar_ColorForMap(player->bottomcolor);
	Draw_FillAlpha(x, y + 1, 40, 3, top, scr_sbaralpha.value);
	Draw_FillAlpha(x, y + 4, 40, 4, bottom, scr_sbaralpha.value);

	/* draw frags */
	char frags[4];
	qsnprintf(frags, sizeof(frags), "%3d", player->frags);
	Draw_CharacterAlpha(x + 8, y, frags[0], scr_sbaralpha.value);
	Draw_CharacterAlpha(x + 16, y, frags[1], scr_sbaralpha.value);
	Draw_CharacterAlpha(x + 24, y, frags[2], scr_sbaralpha.value);
	if (playernum == cl.viewentity - 1) {
	    Draw_CharacterAlpha(x, y, 16, scr_sbaralpha.value);
	    Draw_CharacterAlpha(x + 32, y, 17, scr_sbaralpha.value);
	}

	/* draw name */
	char name[17];
	qsnprintf(name, sizeof(name), "%-16s", player->name);
	Draw_StringAlpha(x + 48, y, name, scr_sbaralpha.value);

	y += 8;
    }
}

/*
==================
Sbar_IntermissionOverlay

==================
*/
void
Sbar_IntermissionOverlay(void)
{
    const qpic8_t *pic;
    int dig;
    int num;

    scr_copyeverything = 1;
    scr_fullupdate = 0;

    if (cl.gametype == GAME_DEATHMATCH) {
	Sbar_DeathmatchOverlay();
	return;
    }

    pic = Draw_CachePic("gfx/complete.lmp");
    Draw_Pic(64, 24, pic);

    pic = Draw_CachePic("gfx/inter.lmp");
    Draw_TransPic(0, 56, pic, TRANSPARENT_COLOR);

// time
    dig = cl.completed_time / 60;
    Sbar_IntermissionNumber(160, 64, dig, 3, 0);
    num = cl.completed_time - dig * 60;
    Draw_TransPic(234, 64, sb_colon, TRANSPARENT_COLOR);
    Draw_TransPic(246, 64, sb_nums[0][num / 10], TRANSPARENT_COLOR);
    Draw_TransPic(266, 64, sb_nums[0][num % 10], TRANSPARENT_COLOR);

    Sbar_IntermissionNumber(160, 104, cl.stats[STAT_SECRETS], 3, 0);
    Draw_TransPic(232, 104, sb_slash, TRANSPARENT_COLOR);
    Sbar_IntermissionNumber(240, 104, cl.stats[STAT_TOTALSECRETS], 3, 0);

    Sbar_IntermissionNumber(160, 144, cl.stats[STAT_MONSTERS], 3, 0);
    Draw_TransPic(232, 144, sb_slash, TRANSPARENT_COLOR);
    Sbar_IntermissionNumber(240, 144, cl.stats[STAT_TOTALMONSTERS], 3, 0);

}


/*
==================
Sbar_FinaleOverlay

==================
*/
void
Sbar_FinaleOverlay(void)
{
    const qpic8_t *pic;

    scr_copyeverything = 1;

    pic = Draw_CachePic("gfx/finale.lmp");
    Draw_TransPic((scr_scaled_width - pic->width) / 2, 16, pic, TRANSPARENT_COLOR);
}
