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

#include "console.h"
#include "model.h"
#include "pmove.h"
#include "sys.h"

#ifdef SERVERONLY
#include "qwsvdef.h"
#else
#include "quakedef.h"
#endif

#ifndef GLQUAKE
#include "d_iface.h"
#endif

// FIXME - header hacks
extern vec3_t player_mins;
extern vec3_t player_maxs;

/*
==================
PM_PointContents

==================
*/
int
PM_PointContents(const vec3_t point)
{
    const hull_t *hull;
    int num;

    hull = &pmove.physents[0].model->hulls[0];
    num = hull->firstclipnode;

    return Mod_HullPointContents(hull, num, point);
}

/*
================
PM_TestPlayerPosition

Returns false if the given player position is not valid (in solid)
================
*/
qboolean
PM_TestPlayerPosition(const vec3_t pos)
{
    int i;
    physent_t *pe;
    vec3_t mins, maxs, test;
    boxhull_t boxhull;
    const hull_t *hull;

    for (i = 0; i < pmove.numphysent; i++) {
	pe = &pmove.physents[i];

	/* get the clipping hull */
	if (pe->model)
	    hull = &pmove.physents[i].model->hulls[1];
	else {
	    VectorSubtract(pe->mins, player_maxs, mins);
	    VectorSubtract(pe->maxs, player_mins, maxs);
	    Mod_CreateBoxhull(mins, maxs, &boxhull);
	    hull = &boxhull.hull;
	}

	VectorSubtract(pos, pe->origin, test);
	if (Mod_HullPointContents(hull, hull->firstclipnode, test) ==
	    CONTENTS_SOLID)
	    return false;
    }

    return true;
}

/*
================
PM_PlayerMove
================
*/
int
PM_PlayerMove(const vec3_t start, const vec3_t end, trace_t *trace)
{
    trace_t stacktrace;
    boxhull_t boxhull;
    vec3_t mins, maxs, start_l, end_l, offset;
    const physent_t *physent;
    const hull_t *hull;
    int i, clipentity;

    /* fill in a default trace */
    memset(trace, 0, sizeof(*trace));
    trace->fraction = 1;
    VectorCopy(end, trace->endpos);
    clipentity = -1;

    for (i = 0; i < pmove.numphysent; i++) {
	/* get the clipping hull */
	physent = &pmove.physents[i];
	if (physent->model)
	    hull = &pmove.physents[i].model->hulls[1];
	else {
	    VectorSubtract(physent->mins, player_maxs, mins);
	    VectorSubtract(physent->maxs, player_mins, maxs);
	    Mod_CreateBoxhull(mins, maxs, &boxhull);
	    hull = &boxhull.hull;
	}

	VectorCopy(physent->origin, offset);
	VectorSubtract(start, offset, start_l);
	VectorSubtract(end, offset, end_l);

	/* fill in a default trace */
	memset(&stacktrace, 0, sizeof(stacktrace));
	stacktrace.fraction = 1;
	stacktrace.allsolid = true;
	VectorCopy(end, stacktrace.endpos);

	/* trace a line through the apropriate clipping hull */
	Mod_TraceHull(hull, hull->firstclipnode, start_l, end_l, &stacktrace);

	if (stacktrace.allsolid)
	    stacktrace.startsolid = true;
	if (stacktrace.startsolid)
	    stacktrace.fraction = 0;

	/* did we clip the move? */
	if (stacktrace.fraction < trace->fraction) {
	    /* fix trace up by the offset */
	    VectorAdd(stacktrace.endpos, offset, stacktrace.endpos);
	    *trace = stacktrace;
	    clipentity = i;
	}
    }

    return clipentity;
}
