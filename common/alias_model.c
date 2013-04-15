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

#include <string.h>

#include "common.h"
#include "crc.h"
#include "model.h"
#include "sys.h"

#ifdef GLQUAKE
#include "glquake.h"
#else
#include "r_local.h"
#endif

/* FIXME - get rid of these static limits by doing two passes? */

static stvert_t stverts[MAXALIASVERTS];
static mtriangle_t triangles[MAXALIASTRIS];

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
static const trivertx_t *poseverts[MAXALIASFRAMES];
static float poseintervals[MAXALIASFRAMES];
static int posenum;

#define MAXALIASSKINS 256

// a skin may be an animating set 1 or more textures
static float skinintervals[MAXALIASSKINS];
static byte *skindata[MAXALIASSKINS];
static int skinnum;

/*
=================
Mod_LoadAliasFrame
=================
*/
static void
Mod_LoadAliasFrame(const daliasframe_t *in, maliasframedesc_t *frame)
{
    int i;

    strncpy(frame->name, in->name, sizeof(frame->name));
    frame->name[sizeof(frame->name) - 1] = 0;
    frame->firstpose = posenum;
    frame->numposes = 1;

    for (i = 0; i < 3; i++) {
	// these are byte values, so we don't have to worry about
	// endianness
	frame->bboxmin.v[i] = in->bboxmin.v[i];
	frame->bboxmax.v[i] = in->bboxmax.v[i];
    }

    poseverts[posenum] = in->verts;
    poseintervals[posenum] = 999.0f; /* unused, but make problems obvious */
    posenum++;
}


/*
=================
Mod_LoadAliasGroup

returns a pointer to the memory location following this frame group
=================
*/
static daliasframetype_t *
Mod_LoadAliasGroup(const aliashdr_t *aliashdr, const daliasgroup_t *in,
		   maliasframedesc_t *frame)
{
    int i, numframes;
    daliasframe_t *dframe;

    numframes = LittleLong(in->numframes);
    frame->firstpose = posenum;
    frame->numposes = numframes;

    for (i = 0; i < 3; i++) {
	// these are byte values, so we don't have to worry about endianness
	frame->bboxmin.v[i] = in->bboxmin.v[i];
	frame->bboxmax.v[i] = in->bboxmax.v[i];
    }

    dframe = (daliasframe_t *)&in->intervals[numframes];
    strncpy(frame->name, dframe->name, sizeof(frame->name));
    frame->name[sizeof(frame->name) - 1] = 0;
    for (i = 0; i < numframes; i++) {
	poseverts[posenum] = dframe->verts;
	poseintervals[posenum] = LittleFloat(in->intervals[i].interval);
	if (poseintervals[posenum] <= 0)
	    Sys_Error("%s: interval <= 0", __func__);
	posenum++;
	dframe = (daliasframe_t *)&dframe->verts[aliashdr->numverts];
    }

    return (daliasframetype_t *)dframe;
}


/*
=================
Mod_LoadAliasSkinGroup
=================
*/
static void *
Mod_LoadAliasSkinGroup(void *pin, maliasskindesc_t *pskindesc, int skinsize)
{
    daliasskingroup_t *pinskingroup;
    daliasskininterval_t *pinskinintervals;
    byte *pdata;
    int i;

    pinskingroup = pin;
    pskindesc->firstframe = skinnum;
    pskindesc->numframes = LittleLong(pinskingroup->numskins);
    pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

    for (i = 0; i < pskindesc->numframes; i++) {
	skinintervals[skinnum] = LittleFloat(pinskinintervals->interval);
	if (skinintervals[skinnum] <= 0)
	    Sys_Error("%s: interval <= 0", __func__);
	skinnum++;
	pinskinintervals++;
    }

    pdata = (byte *)pinskinintervals;
    for (i = 0; i < pskindesc->numframes; i++) {
	skindata[pskindesc->firstframe + i] = pdata;
	pdata += skinsize;
    }

    return pdata;
}

/*
===============
Mod_LoadAllSkins
===============
*/
static void *
Mod_LoadAllSkins(aliashdr_t *aliashdr, const model_loader_t *loader,
		 const model_t *model, int numskins,
		 daliasskintype_t *pskintype)
{
    int i, skinsize;
    maliasskindesc_t *pskindesc;
    float *pskinintervals;
    byte *pskindata;

    if (numskins < 1
#if defined(GLQUAKE) && defined(NQ_HACK)
	|| numskins > MAX_SKINS
#endif
	)
	Sys_Error("%s: Invalid # of skins: %d", __func__, numskins);
    if (aliashdr->skinwidth & 0x03)
	Sys_Error("%s: skinwidth not multiple of 4", __func__);

    skinsize = aliashdr->skinwidth * aliashdr->skinheight;
    pskindesc = Mod_AllocName(numskins * sizeof(maliasskindesc_t), model->name);
    aliashdr->skindesc = (byte *)pskindesc - (byte *)aliashdr;

    skinnum = 0;
    for (i = 0; i < numskins; i++) {
	aliasskintype_t skintype = LittleLong(pskintype->type);
	if (skintype == ALIAS_SKIN_SINGLE) {
	    pskindesc[i].firstframe = skinnum;
	    pskindesc[i].numframes = 1;
	    skindata[skinnum] = (byte *)(pskintype + 1);
	    skinintervals[skinnum] = 999.0f;
	    skinnum++;
	    pskintype = (daliasskintype_t *)((byte *)(pskintype + 1) + skinsize);
	} else {
	    pskintype = Mod_LoadAliasSkinGroup(pskintype + 1, pskindesc + i,
					       skinsize);
	}
    }

    pskinintervals = Mod_AllocName(skinnum * sizeof(float), model->name);
    aliashdr->skinintervals = (byte *)pskinintervals - (byte *)aliashdr;
    memcpy(pskinintervals, skinintervals, skinnum * sizeof(float));

    /* Hand off saving the skin data to the loader */
    pskindata = loader->LoadSkinData(model->name, aliashdr, skinnum, skindata);
    aliashdr->skindata = (byte *)pskindata - (byte *)aliashdr;

    return pskintype;
}

static void
Mod_AliasCRC(const model_t *model, const byte *buffer, int bufferlen)
{
#ifdef QW_HACK
    unsigned short crc;
    const char *crcmodel = NULL;

    if (!strcmp(model->name, "progs/player.mdl"))
	crcmodel = "pmodel";
    if (!strcmp(model->name, "progs/eyes.mdl"))
	crcmodel = "emodel";

    if (crcmodel) {
	crc = CRC_Block(buffer, bufferlen);
	Info_SetValueForKey(cls.userinfo, crcmodel, va("%d", (int)crc),
			    MAX_INFO_STRING);
	if (cls.state >= ca_connected) {
	    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	    MSG_WriteStringf(&cls.netchan.message, "setinfo %s %d", crcmodel,
			     (int)crc);
	}
    }
#endif
}

/*
=================
Mod_LoadAliasModel
=================
*/
void
Mod_LoadAliasModel(const model_loader_t *loader, model_t *model, void *buffer)
{
    byte *membase;
    int i, j, pad;
    mdl_t *inmodel;
    stvert_t *pinstverts;
    dtriangle_t *pintriangles;
    int version, numframes;
    int start, end, memsize;
    daliasframetype_t *pframetype;
    daliasframe_t *frame;
    daliasgroup_t *group;
    daliasskintype_t *pskintype;
    float *intervals;
    aliashdr_t *aliashdr;

    /* QW sets CRC info for player/eyes models */
    Mod_AliasCRC(model, buffer, com_filesize);

    start = Hunk_LowMark();

    inmodel = (mdl_t *)buffer;
    model->type = mod_alias;
    model->flags = LittleLong(inmodel->flags);
    model->synctype = LittleLong(inmodel->synctype);
    model->numframes = LittleLong(inmodel->numframes);

    version = LittleLong(inmodel->version);
    if (version != ALIAS_VERSION)
	Sys_Error("%s has wrong version number (%i should be %i)",
		  model->name, version, ALIAS_VERSION);

    /*
     * Allocate space for the alias header, plus frame data.
     * Leave pad bytes above the header for driver specific data.
     */
    pad = loader->Aliashdr_Padding();
    memsize = pad + sizeof(aliashdr_t);
    memsize += LittleLong(inmodel->numframes) * sizeof(aliashdr->frames[0]);
    membase = Mod_AllocName(memsize, model->name);
    aliashdr = (aliashdr_t *)(membase + pad);

    /* Endian-adjust the header data */
    aliashdr->numskins = LittleLong(inmodel->numskins);
    aliashdr->skinwidth = LittleLong(inmodel->skinwidth);
    aliashdr->skinheight = LittleLong(inmodel->skinheight);
    aliashdr->numverts = LittleLong(inmodel->numverts);
    aliashdr->numtris = LittleLong(inmodel->numtris);
    aliashdr->numframes = LittleLong(inmodel->numframes);
    aliashdr->size = LittleFloat(inmodel->size) * ALIAS_BASE_SIZE_RATIO;
    for (i = 0; i < 3; i++) {
	aliashdr->scale[i] = LittleFloat(inmodel->scale[i]);
	aliashdr->scale_origin[i] = LittleFloat(inmodel->scale_origin[i]);
    }

    /* Some sanity checks */
    if (aliashdr->skinheight > MAX_LBM_HEIGHT)
	Sys_Error("model %s has a skin taller than %d", model->name,
		  MAX_LBM_HEIGHT);
    if (aliashdr->numverts <= 0)
	Sys_Error("model %s has no vertices", model->name);
    if (aliashdr->numverts > MAXALIASVERTS)
	Sys_Error("model %s has too many vertices", model->name);
    if (aliashdr->numtris <= 0)
	Sys_Error("model %s has no triangles", model->name);

//
// load the skins
//
    pskintype = (daliasskintype_t *)&inmodel[1];
    pskintype = Mod_LoadAllSkins(aliashdr, loader, model, aliashdr->numskins, pskintype);

//
// set base s and t vertices
//
    pinstverts = (stvert_t *)pskintype;
    for (i = 0; i < aliashdr->numverts; i++) {
	stverts[i].onseam = LittleLong(pinstverts[i].onseam);
	stverts[i].s = LittleLong(pinstverts[i].s);
	stverts[i].t = LittleLong(pinstverts[i].t);
    }

//
// set up the triangles
//
    pintriangles = (dtriangle_t *)&pinstverts[aliashdr->numverts];
    for (i = 0; i < aliashdr->numtris; i++) {
	triangles[i].facesfront = LittleLong(pintriangles[i].facesfront);
	for (j = 0; j < 3; j++) {
	    triangles[i].vertindex[j] = LittleLong(pintriangles[i].vertindex[j]);
	    if (triangles[i].vertindex[j] < 0 ||
		triangles[i].vertindex[j] >= aliashdr->numverts)
		Sys_Error("%s: invalid vertex index (%d of %d) in %s\n",
			  __func__, triangles[i].vertindex[j],
			  aliashdr->numverts, model->name);
	}
    }

//
// load the frames
//
    numframes = aliashdr->numframes;
    if (numframes < 1)
	Sys_Error("%s: Invalid # of frames: %d", __func__, numframes);

    posenum = 0;
    pframetype = (daliasframetype_t *)&pintriangles[aliashdr->numtris];

    for (i = 0; i < numframes; i++) {
	if (LittleLong(pframetype->type) == ALIAS_SINGLE) {
	    frame = (daliasframe_t *)(pframetype + 1);
	    Mod_LoadAliasFrame(frame, &aliashdr->frames[i]);
	    pframetype = (daliasframetype_t *)&frame->verts[aliashdr->numverts];
	} else {
	    group = (daliasgroup_t *)(pframetype + 1);
	    pframetype = Mod_LoadAliasGroup(aliashdr, group,
					    &aliashdr->frames[i]);
	}
    }
    aliashdr->numposes = posenum;

// FIXME: do this right
    model->mins[0] = model->mins[1] = model->mins[2] = -16;
    model->maxs[0] = model->maxs[1] = model->maxs[2] = 16;

    /*
     * Save the frame intervals
     */
    intervals = Mod_AllocName(aliashdr->numposes * sizeof(float), model->name);
    aliashdr->poseintervals = (byte *)intervals - (byte *)aliashdr;
    for (i = 0; i < aliashdr->numposes; i++)
	intervals[i] = poseintervals[i];

    /*
     * Save the mesh data (verts, stverts, triangles)
     */
    loader->LoadMeshData(model, aliashdr, triangles, stverts, poseverts);

//
// move the complete, relocatable alias model to the cache
//
    end = Hunk_LowMark();
    memsize = end - start;

    Cache_AllocPadded(&model->cache, pad, memsize - pad, model->name);
    if (!model->cache.data)
	return;

    memcpy((byte *)model->cache.data - pad, membase, memsize);

    Hunk_FreeToLowMark(start);
}

/* Alias model cache */
static struct {
    model_t free;
    model_t used;
    model_t overflow;
} mcache;

void
Mod_InitAliasCache(void)
{
#define MAX_MCACHE 512 /* TODO: cvar controlled */
    int i;
    model_t *model;

    /*
     * To be allocated below host_hunklevel, so as to persist across
     * level loads. If it fills up, put extras on the overflow list...
     */
    mcache.used.next = mcache.overflow.next = NULL;
    mcache.free.next = Hunk_AllocName(MAX_MCACHE * sizeof(model_t), "mcache");

    model = mcache.free.next;
    for (i = 0; i < MAX_MCACHE - 1; i++, model++)
	model->next = model + 1;
    model->next = NULL;
}

model_t *
Mod_FindAliasName(const char *name)
{
    model_t *model;

    for (model = mcache.used.next; model; model = model->next)
	if (!strcmp(model->name, name))
	    return model;

    for (model = mcache.overflow.next; model; model = model->next)
	if (!strcmp(model->name, name))
	    return model;

    return model;
}

model_t *
Mod_NewAliasModel(void)
{
    model_t *model;

    model = mcache.free.next;
    if (model) {
	mcache.free.next = model->next;
	model->next = mcache.used.next;
	mcache.used.next = model;
    } else {
	/* TODO: warn on overflow; maybe automatically resize somehow? */
	model = Hunk_AllocName(sizeof(*model), "mcache+");
	model->next = mcache.overflow.next;
	mcache.overflow.next = model;
    }

    return model;
}

void
Mod_ClearAlias(void)
{
    model_t *model;

    /*
     * For now, only need to worry about overflow above the host
     * hunklevel which will disappear.
     */
    for (model = mcache.overflow.next; model; model = model->next)
	if (model->cache.data)
	    Cache_Free(&model->cache);
    mcache.overflow.next = NULL;
}

const model_t *
Mod_AliasCache(void)
{
    return mcache.used.next;
}

const model_t *
Mod_AliasOverflow(void)
{
    return mcache.overflow.next;
}
