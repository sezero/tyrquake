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

// Shared model functions for renderers

#include "client.h"
#include "mathlib.h"
#include "qtypes.h"
#include "render.h"

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "quakedef.h"
#endif

#ifdef GLQUAKE
// Stubs for GLQuake
void Alpha_Init() { }
#else

static int num_transtables;
int translucent_flags;

void
Alpha_Init()
{
    int i;
    float alphastep;
    byte *tables;

    num_transtables = 4; // TODO: Add cvar to configure
    host_transtables = Hunk_AllocName((sizeof(byte *) + 65536) * num_transtables, "translucency");
    tables = (byte *)&host_transtables[num_transtables];

    alphastep = 1.0f / (num_transtables + 1);
    for (i = 0; i < num_transtables; i++) {
        host_transtables[i] = &tables[i * 65536];
        QPal_CreateTranslucencyTable(host_transtables[i], host_basepal, alphastep * (i + 1));
    }

    translucent_flags = 0;
}

/*
 * Find the correct interval based on time
 * Used for Alias model frame/sprite/skin group animations
 */
int
Mod_FindInterval(const float *intervals, int numintervals, float time)
{
    int i;
    float fullinterval, targettime;

    /*
     * when loading models/skins/sprites, we guaranteed all interval values
     * are positive, so we don't have to worry about division by 0
     */
    fullinterval = intervals[numintervals - 1];
    targettime = time - (int)(time / fullinterval) * fullinterval;
    for (i = 0; i < numintervals - 1; i++)
	if (intervals[i] > targettime)
	    break;

    return i;
}

/* Player colour translation table */
static byte translation_table[256];

void
R_InitTranslationTable()
{
    int i;

    for (i = 0; i < 256; i++)
        translation_table[i] = i;
}

const byte *
R_GetTranslationTable(int topcolor, int bottomcolor)
{
    int i, top, bottom;

    top = qclamp(topcolor, 0, 13) * 16;
    bottom = qclamp(bottomcolor, 0, 13) * 16;
    for (i = 0; i < 16; i++) {
        translation_table[TOP_RANGE + i] = (top < 128) ? top + i : top + 15 - i;
        translation_table[BOTTOM_RANGE + i] = (bottom < 128) ? bottom + i : bottom + 15 -i;
    }

    return translation_table;
}
