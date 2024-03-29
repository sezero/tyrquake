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

#ifndef R_SHARED_H
#define R_SHARED_H

#ifndef GLQUAKE
// r_shared.h: general refresh-related stuff shared between the refresh and the
// driver

#include "d_iface.h"
#include "mathlib.h"
#include "render.h"

// FIXME: clean up and move into d_iface.h

#define	MAXVERTS	16	// max points in a surface polygon
#define MAXWORKINGVERTS	(MAXVERTS+4)	// max points in an intermediate
					//  polygon (while processing)

/* the 12.20 fixed point math used overflows at width 2048 */
#define	MAXWIDTH	2040

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define	MAXHEIGHT	1200

#define MINWIDTH  320
#define MINHEIGHT 200

#define INFINITE_DISTANCE	0x10000	// distance that's always guaranteed to
					//  be farther away than anything in
					//  the scene

//===================================================================

extern int cachewidth;
extern int cacheheight;
extern pixel_t *cacheblock;
extern int screenwidth;

extern float pixelAspect;

extern int r_drawnpolycount;

extern cvar_t r_clearcolor;

#define TURB_TABLE_SIZE (2*TURB_CYCLE)
extern int sintable[TURB_TABLE_SIZE];
extern int intsintable[TURB_TABLE_SIZE];

extern vec3_t vup, base_vup;
extern vec3_t vpn, base_vpn;
extern vec3_t vright, base_vright;

/*
 * Allocates space on the high hunk for surfaces/edges when the
 * required space is too large to fit onto the stack.
 */
void R_AllocSurfEdges(qboolean nostack);

/*
 * Min edges/surfaces are just a reasonable number to play the
 * original id/hipnotic/rouge maps.  Surfaces we want to reference
 * using shorts to pack the edge_t struct into cache lines, so the
 * upper limit on those is 16 bits.  Edges is 32 bits, so not worried
 * about overflowing that.
 */
#define MIN_SURFACES    768
#define MIN_EDGES      2304
#define MAX_SURFACES   UINT16_MAX

/*
 * Maxs for stack allocated edges/surfs here are based on having at
 * least 1MB of available stack size on a 32-bit platform.  This could
 * be tweaked per-target, but for now this will do.  Alias model
 * rendering still needs to use the stack on top of this, so don't go
 * crazy.
 *
 * Automated formula would be something like 1:3 surf:edge ratio
 *
 * edge_t  = 32 bytes * 15360 =   491520
 * surf_t  = 64 bytes *  6400 =   409600
 * espan_t = 16 bytes *  3000 =    48000
 *                            =>  949120
 */
#define MAXSTACKEDGES    15360
#define MAXSTACKSURFACES  6400
#define MAXSPANS          3000

/*
 * Bump the max by this amount if we overflow.  Shouldn't be too many
 * frames before we converge on the right limit.
 */
#define MAX_SURFACES_INCREMENT   5120
#define MAX_EDGES_INCREMENT     10240

extern int r_numsurfaces;
extern int r_numedges;

/*
 * Edges and vertices generated when clipping bmodels against the
 * world are stored on the stack.  Some level designers will create
 * huge brushmodels that cover large areas (e.g. a water brush that
 * flood fills a complex room) which can generate many, many extra
 * verts and edges when clipped to the geometry.
 *
 * We keep the stack size relatively small for the common case, but
 * bump up the size if we encounter the need.
 */
#define MIN_STACK_BMODEL_VERTS  256 // vert = 12b     => 3k
#define MIN_STACK_BMODEL_EDGES  512 // edge = 28b/32b => 14k/16k
#define MAX_STACK_BMODEL_VERTS 2048 // vert = 12b     => 24k
#define MAX_STACK_BMODEL_EDGES 6144 // edge = 28b/32b => 172k/196k
#define STACK_BMODEL_VERTS_INCREMENT 128
#define STACK_BMODEL_EDGES_INCREMENT 256

extern int r_numbclipverts; // Number of verts allocated
extern int r_numbclipedges; // Number of edges allocated

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct espan_s {
    int u, v, count;
    struct espan_s *pnext;
} espan_t;

// FIXME: compress, make a union if that will help
// insubmodel is only 1, flags is fewer than 32, spanstate could be a byte
typedef struct surf_s {
    struct surf_s *next;	// active surface stack in r_edge.c
    struct surf_s *prev;	// used in r_edge.c for active surf stack
    struct espan_s *spans;	// pointer to linked list of spans to draw
    int key;			// sorting key (BSP order)
    int last_u;			// set during tracing
    int spanstate;		// 0 = not in span
    // 1 = in span
    // -1 = in inverted span (end before
    //  start)
    int flags;			// currentface flags
    void *data;			// associated data like msurface_t
    const entity_t *entity;
    float nearzi;		// nearest 1/z on surface, for mipmapping
    qboolean insubmodel;
    float d_ziorigin, d_zistepu, d_zistepv;

    const byte *alphatable;     // For entity alpha
    int pad[1];                 // to 64 bytes
} surf_t;

extern surf_t *surfaces, *surface_p, *surf_max, *bmodel_surfaces;

// surfaces are generated in back to front order by the bsp, so if a surf
// pointer is greater than another one, it should be drawn in front
// surfaces[1] is the background, and is used as the active surface stack.
// surfaces[0] is a dummy, because index 0 is used to indicate no surface
//  attached to an edge_t

//===================================================================

extern vec3_t sxformaxis[4];	// s axis transformed into viewspace
extern vec3_t txformaxis[4];	// t axis transformed into viewspac

extern vec3_t modelorg, base_modelorg;

extern float xcenter, ycenter;
extern float xscale, yscale;
extern float xscaleinv, yscaleinv;
extern float xscaleshrink, yscaleshrink;

extern int d_lightstylevalue[256];	// 8.8 frac of base light value

extern void TransformVector(vec3_t in, vec3_t out);
extern void SetUpForLineScan(fixed8_t startvertu, fixed8_t startvertv,
			     fixed8_t endvertu, fixed8_t endvertv);

extern int r_skymade;
extern void R_MakeSky(void);

extern int ubasestep, errorterm, erroradjustup, erroradjustdown;

// flags in finalvert_t.flags
#define ALIAS_LEFT_CLIP				0x0001
#define ALIAS_TOP_CLIP				0x0002
#define ALIAS_RIGHT_CLIP			0x0004
#define ALIAS_BOTTOM_CLIP			0x0008
#define ALIAS_Z_CLIP				0x0010
// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define ALIAS_ONSEAM				0x0020	// also defined in modelgen.h;
											//  must be kept in sync
#define ALIAS_XY_CLIP_MASK			0x000F

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct edge_s {
    fixed16_t u;
    fixed16_t u_step;
    struct edge_s *prev, *next;
    uint16_t surfs[2];
    struct edge_s *nextremove;
    float nearzi;
    medge_t *owner;
} edge_t;

#endif // GLQUAKE

#endif /* R_SHARED_H */
