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

#ifndef D_IFACE_H
#define D_IFACE_H

#ifdef GLQUAKE
#error "Don't use this file with GLQuake"
#endif

#include "cvar.h"
#include "mathlib.h"
#include "model.h"
#include "qtypes.h"
#include "vid.h"

// d_iface.h: interface header file for rasterization driver modules

// FIXME - was NQ=480, QW=200 - does it matter?
#define MAX_LBM_HEIGHT	480

typedef struct {
    float u, v;
    float s, t;
    float zi;
} emitpoint_t;

typedef enum {
    pt_static, pt_grav, pt_slowgrav, pt_fire, pt_explode, pt_explode2,
    pt_blob, pt_blob2
} ptype_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct particle_s {
// driver-usable fields
    vec3_t org;
    float color;
// drivers never touch the following fields
    struct particle_s *next;
    vec3_t vel;
    float ramp;
    float die;
    ptype_t type;
} particle_t;

#define PARTICLE_Z_CLIP	8.0

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct finalvert_s {
    int v[6];			// u, v, s, t, l, 1/z
    int flags;
    float reserved;
} finalvert_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct {
    void *pskin;
    int skinwidth;
    int skinheight;
    mtriangle_t *ptriangles;
    finalvert_t *pfinalverts;
    int numtriangles;
    int drawtype;
    int seamfixupX16;
} affinetridesc_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct {
    float u, v, zi, color;
} screenpart_t;

typedef struct {
    int nump;
    emitpoint_t *pverts;
    // there's room for an extra element at [nump],
    //  if the driver wants to duplicate element [0] at
    //  element [nump] to avoid dealing with wrapping
    const mspriteframe_t *pspriteframe;
    vec3_t vup, vright, vpn;	// in worldspace
    float nearzi;
} spritedesc_t;

extern cvar_t r_drawflat;
extern int r_framecount;	// sequence # of current frame since Quake started
extern qboolean r_recursiveaffinetriangles;	// true if a driver wants to use

					    //  recursive triangular subdivison
					    //  and vertex drawing via
					    //  D_PolysetDrawFinalVerts() past
					    //  a certain distance (normally
					    //  only used by the software
					    //  driver)
extern float r_aliasuvscale;	// scale-up factor for screen u and v

				//  on Alias vertices passed to driver
extern int r_pixbytes;
extern qboolean r_dowarp;

extern affinetridesc_t r_affinetridesc;
extern spritedesc_t r_spritedesc;

extern int d_con_indirect;	// if 0, Quake will draw console directly
				//  to vid.buffer; if 1, Quake will
				//  draw console via D_DrawRect. Must be
				//  defined by driver

extern vec3_t r_pright, r_pup, r_ppn;

void D_RegisterVariables();
void D_Init();

void D_Aff8Patch(void *pcolormap);
void D_BeginDirectRect(int x, int y, const byte *pbitmap, int width,
		       int height);
void D_DisableBackBufferAccess(void);
void D_EndDirectRect(int x, int y, int width, int height);
void D_PolysetDraw();
void D_DrawSubdiv();
void D_DrawNonSubdiv();
void D_PolysetDrawFinalVerts(finalvert_t *fv, int numverts);
void D_PolysetDrawFinalVerts_Translucent(finalvert_t *fv, int numverts);
void D_PolysetDrawFinalVerts_Fence(finalvert_t *fv, int numverts);
void D_PolysetDrawFinalVerts_Fence_Translucent(finalvert_t *fv, int numverts);
void D_DrawParticle(particle_t *pparticle);
void D_DrawSprite(void);
void D_DrawSurfaces(qboolean sort_submodels);
void D_EnableBackBufferAccess(void);
void D_EndParticles(void);
void D_ViewChanged(void);
void D_SetupFrame(void);
void D_StartParticles(void);
void D_TurnZOn(void);
void D_WarpScreen(void);

void D_FillRect(vrect_t *vrect, int color);
void D_DrawRect(void);
void D_UpdateRects(vrect_t *prect);

// currently for internal use only, and should be a do-nothing function in
// hardware drivers
// FIXME: this should go away
void D_PolysetUpdateTables(void);

// these are currently for internal use only, and should not be used by drivers
extern int r_skydirect;
extern byte *r_skysource;

// transparency types for D_DrawRect ()
#define DR_SOLID		0
#define DR_TRANSPARENT	1

// !!! must be kept the same as in quakeasm.h !!!
#define TRANSPARENT_COLOR	0xFF

extern void *acolormap;		// FIXME: should go away

//=======================================================================//

// callbacks to Quake

typedef struct {
    pixel_t *surfdat;		// destination for generated surface
    int rowbytes;		// destination logical width in bytes
    msurface_t *surf;		// description for surface to generate
    fixed8_t lightadj[MAXLIGHTMAPS];

    // adjust for lightmap levels for dynamic lighting
    texture_t *texture;		// corrected for animating textures
    int surfmip;		// mipmapped ratio surface texels/world pixels
    int surfwidth;		// in mipmapped texels
    int surfheight;		// in mipmapped texels
} drawsurf_t;

extern drawsurf_t r_drawsurf;

void R_DrawSurface(void);


// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define TURB_TEX_SIZE   64 // base turbulent texture size
#define TURB_TEX_SHIFT   6

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define	TURB_CYCLE	128	// turbulent cycle size

#define TILE_SIZE	128	// R_GenTurbTile{16}, R_GenSkyTile16

#define SKYSHIFT	7
#define	SKYSIZE		(1 << SKYSHIFT)
#define SKYMASK		(SKYSIZE - 1)

extern float skyspeed, skyspeed2;
extern float skytime;

extern int c_surf;

extern byte *r_warpbuffer;

#endif /* D_IFACE_H */
