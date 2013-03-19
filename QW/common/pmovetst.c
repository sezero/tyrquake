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
PM_PointContents(vec3_t point)
{
    const hull_t *hull;
    int num;

    hull = &pmove.physents[0].model->hulls[0];
    num = hull->firstclipnode;

    return Mod_HullPointContents(hull, num, point);
}

/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/

// 1/32 epsilon to keep floating point happy
#define	DIST_EPSILON	(0.03125)

/*
==================
PM_RecursiveHullCheck

==================
*/
qboolean
PM_RecursiveHullCheck(const hull_t *hull, int num, float p1f, float p2f,
		      vec3_t p1, vec3_t p2, pmtrace_t * trace)
{
    const mclipnode_t *node;
    const mplane_t *plane;
    float t1, t2;
    float frac;
    int child, i;
    vec3_t mid;
    int side;
    float midf;

// check for empty
    if (num < 0) {
	if (num != CONTENTS_SOLID) {
	    trace->allsolid = false;
	    if (num == CONTENTS_EMPTY)
		trace->inopen = true;
	    else
		trace->inwater = true;
	} else
	    trace->startsolid = true;
	return true;		// empty
    }

    if (num < hull->firstclipnode || num > hull->lastclipnode)
	Sys_Error("PM_RecursiveHullCheck: bad node number");

//
// find the point distances
//
    node = hull->clipnodes + num;
    plane = hull->planes + node->planenum;

    if (plane->type < 3) {
	t1 = p1[plane->type] - plane->dist;
	t2 = p2[plane->type] - plane->dist;
    } else {
	t1 = DotProduct(plane->normal, p1) - plane->dist;
	t2 = DotProduct(plane->normal, p2) - plane->dist;
    }

#if 1
    if (t1 >= 0 && t2 >= 0) {
	child = node->children[0];
	return PM_RecursiveHullCheck(hull, child, p1f, p2f, p1, p2, trace);
    }
    if (t1 < 0 && t2 < 0) {
	child = node->children[1];
	return PM_RecursiveHullCheck(hull, child, p1f, p2f, p1, p2, trace);
    }
#else
    if ((t1 >= DIST_EPSILON && t2 >= DIST_EPSILON) || (t2 > t1 && t1 >= 0)) {
	child = node->children[0];
	return PM_RecursiveHullCheck(hull, child, p1f, p2f, p1, p2, trace);
    }
    if ((t1 <= -DIST_EPSILON && t2 <= -DIST_EPSILON) || (t2 < t1 && t1 <= 0)) {
	child = node->children[1];
	return PM_RecursiveHullCheck(hull, child, p1f, p2f, p1, p2, trace);
    }
#endif

// put the crosspoint DIST_EPSILON pixels on the near side
    if (t1 < 0)
	frac = (t1 + DIST_EPSILON) / (t1 - t2);
    else
	frac = (t1 - DIST_EPSILON) / (t1 - t2);
    if (frac < 0)
	frac = 0;
    if (frac > 1)
	frac = 1;

    midf = p1f + (p2f - p1f) * frac;
    for (i = 0; i < 3; i++)
	mid[i] = p1[i] + frac * (p2[i] - p1[i]);

    side = (t1 < 0);

// move up to the node
    child = node->children[side];
    if (!PM_RecursiveHullCheck(hull, child, p1f, midf, p1, mid, trace))
	return false;

#ifdef PARANOID
    if (Mod_HullPointContents(pm_hullmodel, child, mid) == CONTENTS_SOLID) {
	Con_Printf("mid PointInHullSolid\n");
	return false;
    }
#endif

    child = node->children[side ^ 1];
    if (Mod_HullPointContents(hull, child, mid) != CONTENTS_SOLID)
	/* go past the node */
	return PM_RecursiveHullCheck(hull, child, midf, p2f, mid, p2, trace);

    if (trace->allsolid)
	return false;		// never got out of the solid area

//==================
// the other side of the node is solid, this is the impact point
//==================
    if (!side) {
	VectorCopy(plane->normal, trace->plane.normal);
	trace->plane.dist = plane->dist;
    } else {
	VectorSubtract(vec3_origin, plane->normal, trace->plane.normal);
	trace->plane.dist = -plane->dist;
    }

    /* shouldn't really happen, but does occasionally */
    while (Mod_HullPointContents(hull, hull->firstclipnode, mid) == CONTENTS_SOLID) {
	frac -= 0.1;
	if (frac < 0) {
	    trace->fraction = midf;
	    VectorCopy(mid, trace->endpos);
	    Con_DPrintf("backup past 0\n");
	    return false;
	}
	midf = p1f + (p2f - p1f) * frac;
	for (i = 0; i < 3; i++)
	    mid[i] = p1[i] + frac * (p2[i] - p1[i]);
    }

    trace->fraction = midf;
    VectorCopy(mid, trace->endpos);

    return false;
}


/*
================
PM_TestPlayerPosition

Returns false if the given player position is not valid (in solid)
================
*/
qboolean
PM_TestPlayerPosition(vec3_t pos)
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
pmtrace_t
PM_PlayerMove(vec3_t start, vec3_t end)
{
    pmtrace_t trace, total;
    vec3_t offset;
    vec3_t start_l, end_l;
    boxhull_t boxhull;
    const hull_t *hull;
    int i;
    physent_t *pe;
    vec3_t mins, maxs;

// fill in a default trace
    memset(&total, 0, sizeof(pmtrace_t));
    total.fraction = 1;
    total.ent = -1;
    VectorCopy(end, total.endpos);

    for (i = 0; i < pmove.numphysent; i++) {
	pe = &pmove.physents[i];
	// get the clipping hull
	if (pe->model)
	    hull = &pmove.physents[i].model->hulls[1];
	else {
	    VectorSubtract(pe->mins, player_maxs, mins);
	    VectorSubtract(pe->maxs, player_mins, maxs);
	    Mod_CreateBoxhull(mins, maxs, &boxhull);
	    hull = &boxhull.hull;
	}

	// PM_HullForEntity (ent, mins, maxs, offset);
	VectorCopy(pe->origin, offset);

	VectorSubtract(start, offset, start_l);
	VectorSubtract(end, offset, end_l);

	// fill in a default trace
	memset(&trace, 0, sizeof(pmtrace_t));
	trace.fraction = 1;
	trace.allsolid = true;
//              trace.startsolid = true;
	VectorCopy(end, trace.endpos);

	// trace a line through the apropriate clipping hull
	PM_RecursiveHullCheck(hull, hull->firstclipnode, 0, 1, start_l,
			      end_l, &trace);

	if (trace.allsolid)
	    trace.startsolid = true;
	if (trace.startsolid)
	    trace.fraction = 0;

	// did we clip the move?
	if (trace.fraction < total.fraction) {
	    // fix trace up by the offset
	    VectorAdd(trace.endpos, offset, trace.endpos);
	    total = trace;
	    total.ent = i;
	}

    }

    return total;
}
