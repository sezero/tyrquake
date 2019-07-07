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
// r_light.c

#include <math.h>

#include "bspfile.h"
#include "client.h"
#include "quakedef.h"

#ifdef GLQUAKE
#include "glquake.h"
#include "view.h"
#else
#include "r_local.h"
#endif

int r_dlightframecount;

/*
==================
R_AnimateLight
==================
*/
void
R_AnimateLight(void)
{
    int i, j, k;

//
// light animations
// 'm' is normal light, 'a' is no light, 'z' is double bright
    i = (int)(cl.time * 10);
    for (j = 0; j < MAX_LIGHTSTYLES; j++) {
	if (!cl_lightstyle[j].length) {
	    d_lightstylevalue[j] = 256;
	    continue;
	}
	k = i % cl_lightstyle[j].length;
	k = cl_lightstyle[j].map[k] - 'a';
	k = k * 22;
	d_lightstylevalue[j] = k;
    }
}

/* --------------------------------------------------------------------------*/
/* Dynamic Lights                                                            */
/* --------------------------------------------------------------------------*/

/*
=============
R_MarkLights
=============
*/
void
R_MarkLights(dlight_t *light, int bit, mnode_t *node)
{
    mplane_t *splitplane;
    float dist;
    msurface_t *surf;
    int i;

    if (node->contents < 0)
	return;

    splitplane = node->plane;
    dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;

    if (dist > light->radius) {
	R_MarkLights(light, bit, node->children[0]);
	return;
    }
    if (dist < -light->radius) {
	R_MarkLights(light, bit, node->children[1]);
	return;
    }
// mark the polygons
    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
	if (surf->dlightframe != r_dlightframecount) {
	    surf->dlightbits = 0;
	    surf->dlightframe = r_dlightframecount;
	}
	surf->dlightbits |= bit;
    }

    R_MarkLights(light, bit, node->children[0]);
    R_MarkLights(light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void
R_PushDlights(void)
{
    int i;
    dlight_t *l;

    r_dlightframecount = r_framecount + 1;	// because the count hasn't
    //  advanced yet for this frame
    l = cl_dlights;

    for (i = 0; i < MAX_DLIGHTS; i++, l++) {
	if (l->die < cl.time || !l->radius)
	    continue;
	R_MarkLights(l, 1 << i, cl.worldmodel->nodes);
    }
}

/* --------------------------------------------------------------------------*/
/* Light Sampling                                                            */
/* --------------------------------------------------------------------------*/

#ifdef GLQUAKE
vec3_t lightspot;
#endif

__attribute__((noinline))
static int
R_LightSurfPoint(const mnode_t *node, const vec3_t surfpoint)
{
    const msurface_t *surf;
    int i, maps;

    /* check for impact on this node */
    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
	const mtexinfo_t *tex;
	const byte *lightmap;
	int s, t, ds, dt, lightlevel;

	if (surf->flags & SURF_DRAWTILED)
	    continue; /* no lightmaps */

	tex = surf->texinfo;
	s = DotProduct(surfpoint, tex->vecs[0]) + tex->vecs[0][3];
	t = DotProduct(surfpoint, tex->vecs[1]) + tex->vecs[1][3];
	if (s < surf->texturemins[0] || t < surf->texturemins[1])
	    continue;

	ds = s - surf->texturemins[0];
	dt = t - surf->texturemins[1];
	if (ds > surf->extents[0] || dt > surf->extents[1])
	    continue;

	if (!surf->samples)
	    return 0;

	ds >>= 4;
	dt >>= 4;

	/* FIXME: does this account properly for dynamic lights? e.g. rocket */
	lightlevel = 0;
	lightmap = surf->samples + dt * ((surf->extents[0] >> 4) + 1) + ds;
	foreach_surf_lightstyle(surf, maps) {
	    const short *size = surf->extents;
	    const int surfbytes = ((size[0] >> 4) + 1) * ((size[1] >> 4) + 1);

	    lightlevel += *lightmap * d_lightstylevalue[surf->styles[maps]];
	    lightmap += surfbytes;
	}
	return lightlevel >> 8;
    }

    return -1;
}

static int
RecursiveLightPoint(const mnode_t *node, const vec3_t start, const vec3_t end)
{
    const mplane_t *plane;
    float front, back, frac;
    vec3_t surfpoint;
    int side, lightlevel;

 restart:
    if (node->contents < 0)
	return -1; /* didn't hit anything */

    /* calculate surface intersection point */
    plane = node->plane;
    switch (plane->type) {
    case PLANE_X:
    case PLANE_Y:
    case PLANE_Z:
	front = start[plane->type - PLANE_X] - plane->dist;
	back = end[plane->type - PLANE_X] - plane->dist;
	break;
    default:
	front = DotProduct(start, plane->normal) - plane->dist;
	back = DotProduct(end, plane->normal) - plane->dist;
	break;
    }
    side = front < 0;

    if ((back < 0) == side) {
	/* Completely on one side - tail recursion optimization */
	node = node->children[side];
	goto restart;
    }

    frac = front / (front - back);
    surfpoint[0] = start[0] + (end[0] - start[0]) * frac;
    surfpoint[1] = start[1] + (end[1] - start[1]) * frac;
    surfpoint[2] = start[2] + (end[2] - start[2]) * frac;

    /* go down front side */
    lightlevel = RecursiveLightPoint(node->children[side], start, surfpoint);
    if (lightlevel >= 0)
	return lightlevel; /* hit something */

    if ((back < 0) == side)
	return -1; /* didn't hit anything */

#ifdef GLQUAKE
    VectorCopy(surfpoint, lightspot);
#endif

    lightlevel = R_LightSurfPoint(node, surfpoint);
    if (lightlevel >= 0)
	return lightlevel;

    /* Go down back side */
    return RecursiveLightPoint(node->children[!side], surfpoint, end);
}

/*
 * FIXME - check what the callers do, but I don't think this will check the
 * light value of a bmodel below the point. Models could easily be standing on
 * a func_plat or similar...
 */
int
R_LightPoint(const vec3_t point)
{
    vec3_t end;
    int lightlevel;

    if (!cl.worldmodel->lightdata)
	return 255;

    end[0] = point[0];
    end[1] = point[1];
    end[2] = point[2] - (8192 + 2); /* Max distance + error margin */

    lightlevel = RecursiveLightPoint(cl.worldmodel->nodes, point, end);

    if (lightlevel == -1)
	lightlevel = 0;

#ifndef GLQUAKE
    if (lightlevel < r_refdef.ambientlight)
	lightlevel = r_refdef.ambientlight;
#endif

    return lightlevel;
}
