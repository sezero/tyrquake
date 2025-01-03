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

#ifndef D_LOCAL_H
#define D_LOCAL_H

// d_local.h:  private rasterization driver defs

#include "bspfile.h"
#include "r_shared.h"

//
// TODO: fine-tune this; it's based on providing some coverage even if there
// is a 2k-wide scan, with subdivision every 8, for 256 spans of 12 bytes each
//
#define SCANBUFFERPAD		0x1000

#define R_SKY_SMASK	0x007F0000
#define R_SKY_TMASK	0x007F0000

#define DS_SPAN_LIST_END	-128

#define SURFCACHE_SIZE_AT_320X200	(320 * 200 * 14)

typedef struct surfcache_s {
    struct surfcache_s *next;
    struct surfcache_s **owner;	// NULL is an empty chunk of memory
    int lightadj[MAXLIGHTMAPS];	// checked for strobe flush
    int dlight;
    int size;			// including header
    unsigned width;
    unsigned height;		// DEBUG only needed for debug
    float mipscale;
    struct texture_s *texture;	// checked for animating textures
    byte data[4];		// width*height elements
} surfcache_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct sspan_s {
    int u, v, count;
} sspan_t;

extern float scale_for_mip;

extern qboolean d_roverwrapped;
extern surfcache_t *sc_rover;
extern surfcache_t *d_initial_rover;

extern float d_sdivzstepu, d_tdivzstepu, d_zistepu;
extern float d_sdivzstepv, d_tdivzstepv, d_zistepv;
extern float d_sdivzorigin, d_tdivzorigin, d_ziorigin;

extern fixed16_t sadjust, tadjust;
extern fixed16_t bbextents, bbextentt;

void D_DrawSpans8(espan_t *pspans);
void D_DrawSpans8_Translucent(espan_t *pspans);
void D_DrawSpans8_Fence(espan_t *pspans);
void D_DrawSpans8_Fence_Translucent(espan_t *pspans);
void D_DrawSpans16(espan_t *pspans);
extern void (*D_DrawSpans)(espan_t *pspan);

void D_DrawZSpans(espan_t *pspans);
void D_SpriteDrawSpans(sspan_t * pspan);

void Turbulent8(espan_t *pspan);
void D_DrawTurbulent8Span();
void D_DrawTurbulent8Span_NonStd();
void D_DrawTurbulentTranslucent8Span();
void D_DrawTurbulentTranslucent8Span_NonStd();

void D_DrawSkyScans8(espan_t *pspan);
void D_DrawSkyScans16(espan_t *pspan);

void R_ShowSubDiv(void);
extern void (*prealspandrawer) (void);
surfcache_t *D_CacheSurface(const entity_t *e, msurface_t *surface,
			    int miplevel);

#ifdef USE_X86_ASM
void D_RasterizeAliasPolySmooth(void);
void D_PolysetScanLeftEdge(int height);
void D_PolysetSetEdgeTable(void);
void D_PolysetCalcGradients(int skinwidth);
void D_PolysetAff8Start(void);
void D_PolysetAff8End(void);
struct spanpackage_s; /* detailed in d_polyse.c */
void D_PolysetDrawSpans8(struct spanpackage_s*);
void D_PolysetDrawSpans8_Fence(struct spanpackage_s*);
void D_PolysetDrawSpans8_Translucent(struct spanpackage_s*);
void D_PolysetDrawSpans8_Fence_Translucent(struct spanpackage_s*);
#endif

extern short *d_pzbuffer;
extern unsigned int d_zrowbytes, d_zwidth;
extern const byte *r_transtable;
extern void (*D_DrawTurbSpanFunc)(void);

extern int *d_pscantable;
extern int d_scantable[MAXHEIGHT];

extern int d_vrectx, d_vrecty, d_vrectright_particle, d_vrectbottom_particle;

extern int d_y_aspect_shift, d_pix_min, d_pix_max, d_pix_shift;

extern pixel_t *d_viewbuffer;

extern short *zspantable[MAXHEIGHT];

extern int d_minmip;
extern float d_scalemip[3];

#endif /* D_LOCAL_H */
