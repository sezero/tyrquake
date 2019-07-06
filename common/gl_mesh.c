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
// gl_mesh.c: triangle model functions

#include "buildinfo.h"
#include "common.h"
#include "console.h"
#include "glquake.h"
#include "model.h"
#include "quakedef.h"
#include "sys.h"

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

/*
 * Worst case, each triangle would be it's own command list, which is
 * 1 int for the count, then 6 floats for the vertex coordinates - 7
 * total.  Just as a sanity check we'll only allow models of 1 million
 * triangles or less.  This should protect us from integer
 * overflow/underflow issues as well.
 */
#define MAX_MESH_COMMANDS  (8 * 1024 * 1024)
#define MAX_MESH_VERTICIES (3 * 1024 * 1024)

static int *used;

/*
 * The command buffer holds counts (int32_t) and s/t values (float32)
 * that are valid for every frame.  A command is a 'count', 's' or 't'
 * value.  So, numcommands is the number of 4-byte slots filled in the
 * commands buffer.
 */
static int *commands;
static int numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
static int *vertexorder;
static int numorder;

static int allverts, alltris;

static int *stripverts;
static int *striptris;
static int stripcount;

/*
================
StripLength
================
*/
static int
StripLength(aliashdr_t *hdr, int starttri, int startv, const mtriangle_t *tris)
{
    const mtriangle_t *last, *check;
    int m1, m2;
    int j;
    int k;

    used[starttri] = 2;

    last = &tris[starttri];

    stripverts[0] = last->vertindex[(startv) % 3];
    stripverts[1] = last->vertindex[(startv + 1) % 3];
    stripverts[2] = last->vertindex[(startv + 2) % 3];

    striptris[0] = starttri;
    stripcount = 1;

    m1 = last->vertindex[(startv + 2) % 3];
    m2 = last->vertindex[(startv + 1) % 3];

    // look for a matching triangle
  nexttri:
    for (j = starttri + 1, check = &tris[starttri + 1];
	 j < hdr->numtris; j++, check++) {
	if (check->facesfront != last->facesfront)
	    continue;
	for (k = 0; k < 3; k++) {
	    if (check->vertindex[k] != m1)
		continue;
	    if (check->vertindex[(k + 1) % 3] != m2)
		continue;

	    // this is the next part of the fan

	    // if we can't use this triangle, this tristrip is done
	    if (used[j])
		goto done;

	    // the new edge
	    if (stripcount & 1)
		m2 = check->vertindex[(k + 2) % 3];
	    else
		m1 = check->vertindex[(k + 2) % 3];

	    stripverts[stripcount + 2] = check->vertindex[(k + 2) % 3];
	    striptris[stripcount] = j;
	    stripcount++;

	    used[j] = 2;
	    goto nexttri;
	}
    }
  done:

    // clear the temp used flags
    for (j = starttri + 1; j < hdr->numtris; j++)
	if (used[j] == 2)
	    used[j] = 0;

    return stripcount;
}

/*
===========
FanLength
===========
*/
static int
FanLength(aliashdr_t *hdr, int starttri, int startv, const mtriangle_t *tris)
{
    const mtriangle_t *last, *check;
    int m1, m2;
    int j;
    int k;

    used[starttri] = 2;

    last = &tris[starttri];

    stripverts[0] = last->vertindex[(startv) % 3];
    stripverts[1] = last->vertindex[(startv + 1) % 3];
    stripverts[2] = last->vertindex[(startv + 2) % 3];

    striptris[0] = starttri;
    stripcount = 1;

    m1 = last->vertindex[(startv + 0) % 3];
    m2 = last->vertindex[(startv + 2) % 3];

    // look for a matching triangle
  nexttri:
    for (j = starttri + 1, check = &tris[starttri + 1];
	 j < hdr->numtris; j++, check++) {
	if (check->facesfront != last->facesfront)
	    continue;
	for (k = 0; k < 3; k++) {
	    if (check->vertindex[k] != m1)
		continue;
	    if (check->vertindex[(k + 1) % 3] != m2)
		continue;

	    // this is the next part of the fan

	    // if we can't use this triangle, this tristrip is done
	    if (used[j])
		goto done;

	    // the new edge
	    m2 = check->vertindex[(k + 2) % 3];

	    stripverts[stripcount + 2] = m2;
	    striptris[stripcount] = j;
	    stripcount++;

	    used[j] = 2;
	    goto nexttri;
	}
    }
  done:

    // clear the temp used flags
    for (j = starttri + 1; j < hdr->numtris; j++)
	if (used[j] == 2)
	    used[j] = 0;

    return stripcount;
}


/*
================
BuildTris

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
static void
BuildTris(aliashdr_t *hdr, const mtriangle_t *tris, const stvert_t *stverts)
{
    aliasmeshcmd_t *command;
    int i, j, k;
    int startv;
    float s, t;
    int len, bestlen, besttype;
    int *besttris, *bestverts;
    int type;

    /*
     * Worst case for number of commands is each triangle is it's own
     * command list with a count and six vertex coordinates.  Plus a
     * zero count at the end to terminate the list.  The vertex order
     * max size would be three indexes per triangle.
     */
    numcommands = hdr->numtris * 7 + 1;

    /* NOTE: Temp mark is already active and holds the model file */
    commands = Hunk_TempAllocExtend(numcommands * sizeof(commands[0]));
    vertexorder = Hunk_TempAllocExtend(hdr->numtris * 3 * sizeof(vertexorder[0]));
    used = Hunk_TempAllocExtend(hdr->numtris * sizeof(used[0]));
    striptris = Hunk_TempAllocExtend(hdr->numtris * sizeof(striptris[0]));
    stripverts = Hunk_TempAllocExtend((hdr->numtris + 2) * sizeof(stripverts[0]));
    besttris = Hunk_TempAllocExtend(hdr->numtris * sizeof(striptris[0]));
    bestverts = Hunk_TempAllocExtend((hdr->numtris + 2) * sizeof(stripverts[0]));

    numcommands = 0;
    command = (aliasmeshcmd_t *)commands;
    numorder = 0;
    memset(used, 0, hdr->numtris * sizeof(used[0]));

    //
    // build triangle strips/fans
    //
    for (i = 0; i < hdr->numtris; i++) {
	// pick an unused triangle and start the trifan
	if (used[i])
	    continue;

	bestlen = 0;
	besttype = 0;
	for (type = 0; type < 2; type++) {
	    for (startv = 0; startv < 3; startv++) {
		if (type == 1)
		    len = StripLength(hdr, i, startv, tris);
		else
		    len = FanLength(hdr, i, startv, tris);
		if (len > bestlen) {
		    besttype = type;
		    bestlen = len;
		    for (j = 0; j < bestlen + 2; j++)
			bestverts[j] = stripverts[j];
		    for (j = 0; j < bestlen; j++)
			besttris[j] = striptris[j];
		}
	    }
	}

	// mark the tris on the best strip as used
	for (j = 0; j < bestlen; j++)
	    used[besttris[j]] = 1;

	command->count = (besttype == 1) ? (bestlen + 2) : -(bestlen + 2);
        numcommands++;

        float *coords = command->coords;
        for (j = 0; j < bestlen + 2; j++) {
            /* Emit a vertex into the reorder buffer */
            k = bestverts[j];
            vertexorder[numorder++] = k;

            /*
             * Emit s/t coords into the commands stream.  We fudge the
             * coordinates by a fraction of a pixel to reduce
             * artefacts on the model seams.
             */
            s = stverts[k].s;
            t = stverts[k].t;
            if (!tris[besttris[0]].facesfront && stverts[k].onseam) {
                /*
                 * Rear skin is on the RHS of the texture. Fudge +1 to
                 * match appearance in SW renderer as closely as possible.
                 */
                s += (hdr->skinwidth / 2) + 1;
            }
            /*
             * Fudge width/height +2 here to slightly stretch the
             * texture to push the background fill slightly further
             * from the seams.
             */
            s = (s + 0.5f) / (hdr->skinwidth + 2);
            t = (t + 0.5f) / (hdr->skinheight + 2);

            *coords++ = s;
            *coords++ = t;
        }

        /* Advance the command pointer */
        numcommands += (bestlen + 2) * 2;
        command = (aliasmeshcmd_t *)coords;
    }

    command->count = 0; // end of list marker
    numcommands++;

    Con_DPrintf("%3i tri %3i vert %3i cmd\n", hdr->numtris, numorder,
		numcommands);

    allverts += numorder;
    alltris += hdr->numtris;
}

static void
GL_MeshSwapCommands(void)
{
    int i;

    for (i = 0; i < numcommands; i++)
	commands[i] = LittleLong(commands[i]);
    for (i = 0; i < numorder; i++)
	vertexorder[i] = LittleLong(vertexorder[i]);
}

/*
 * Do minimal checks on any cached data loaded to at least ensure we
 * don't crash when trying to render the model.
 */
static qboolean
GL_MeshVerifyCommands(const aliashdr_t *hdr, const model_t *model)
{
    int i, length, verts;

    if (numcommands < 0 || numcommands >= MAX_MESH_COMMANDS)
	return false;
    if (numorder < 0 || numorder >= MAX_MESH_VERTICIES)
	return false;

    for (i = 0; i < numorder; i++)
	if (vertexorder[i] < 0 || vertexorder[i] >= hdr->numverts)
	    return false;

    i = 0, verts = 0;
    while (i < numcommands) {
        aliasmeshcmd_t *command = (aliasmeshcmd_t *)(commands + i);
	length = command->count;
	if (length < 0)
	    length = -length;
	verts += length;
	i += 1 + length * 2;
    }
    if (i != numcommands || verts != numorder)
	return false;

    return true;
}

/*
================
GL_MakeAliasModelDisplayLists
================
*/
void
GL_LoadMeshData(const model_t *model, aliashdr_t *hdr,
		const alias_meshdata_t *meshdata,
		const alias_posedata_t *posedata)
{
    int i, j, tmp, err;
    int *cmds;
    trivertx_t *verts;
    char cache[MAX_OSPATH];
    FILE *f;
    qboolean cached = true;
    const char *name;

    /* look for a cached version */
    name = COM_SkipPath(model->name);
    qsnprintf(cache, sizeof(cache), "%s/glquake/%s", com_gamedir, name);
    err = COM_DefaultExtension(cache, ".ms2", cache, sizeof(cache));
    if (err)
	Sys_Error("%s: model pathname too long (%s)", __func__, model->name);

    /* If engine build is newer than cache files, force rebuild */
    int64_t fileTime = Sys_FileTime(cache);
    if (fileTime == -1)
        cached = false;
    if (fileTime < build_version_timestamp)
        cached = false;

    if (cached) {
        f = fopen(cache, "rb");
        if (f) {
            fread(&numcommands, 4, 1, f);
            fread(&numorder, 4, 1, f);
            numcommands = LittleLong(numcommands);
            numorder = LittleLong(numorder);

            if (numcommands < 0 || numcommands > MAX_MESH_COMMANDS)
                cached = false;
            if (numorder < 0 || numorder > MAX_MESH_COMMANDS)
                cached = false;
            if (cached) {
                /* NOTE: Temp mark is active and holds the model file */
                commands = Hunk_TempAllocExtend(numcommands * sizeof(*commands));
                vertexorder = Hunk_TempAllocExtend(numorder * sizeof(*vertexorder));
                fread(commands, numcommands * sizeof(commands[0]), 1, f);
                fread(vertexorder, numorder * sizeof(vertexorder[0]), 1, f);
                GL_MeshSwapCommands();
                cached = GL_MeshVerifyCommands(hdr, model);
            }
            fclose(f);
            if (!cached)
                Con_DPrintf("bad cached commands for mesh %s\n", model->name);
        }
    }

    if (!cached) {
	/* build it from scratch */
	Con_DPrintf("meshing %s...\n", model->name);
	BuildTris(hdr, meshdata->triangles, meshdata->stverts);

	/* save out the cached version */
	f = fopen(cache, "wb");
	if (!f) {
	    char gldir[MAX_OSPATH];

	    /* Maybe the directory wasn't present, try again */
	    qsnprintf(gldir, sizeof(gldir), "%s/glquake", com_gamedir);
	    Sys_mkdir(gldir);
	    f = fopen(cache, "wb");
	}

	if (f) {
	    tmp = LittleLong(numcommands);
	    fwrite(&tmp, 4, 1, f);
	    tmp = LittleLong(numorder);
	    fwrite(&tmp, 4, 1, f);
	    GL_MeshSwapCommands();
	    fwrite(commands, numcommands * sizeof(commands[0]), 1, f);
	    fwrite(vertexorder, numorder * sizeof(vertexorder[0]), 1, f);
	    GL_MeshSwapCommands();
	    fclose(f);
	}
    }

    /* save the data out to the in-memory model */
    hdr->numverts = numorder;

    cmds = Hunk_AllocName(numcommands * sizeof(*commands), "glmesh");
    GL_Aliashdr(hdr)->commands = (byte *)cmds - (byte *)hdr;
    memcpy(cmds, commands, numcommands * sizeof(*commands));

    verts = Hunk_AllocName(hdr->numposes * hdr->numverts * sizeof(trivertx_t), "glmesh");
    hdr->posedata = (byte *)verts - (byte *)hdr;
    for (i = 0; i < hdr->numposes; i++)
	for (j = 0; j < numorder; j++)
	    *verts++ = posedata->verts[i][vertexorder[j]];
}
