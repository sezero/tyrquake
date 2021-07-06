/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2013 Kevin Shanahan

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

#include "console.h"
#include "cvar.h"
#include "glquake.h"
#include "qtypes.h"

qboolean gl_npotable;

static qboolean
GL_ExtensionCheck(const char *extension)
{
    int length = strlen(extension);
    const char *search = (const char *)glGetString(GL_EXTENSIONS);

    while ((search = strstr(search, extension))) {
	if (!search[length] || search[length] == ' ')
	    return true;
	search += length;
    }

    return false;
}

void
GL_ExtensionCheck_NPoT(void)
{
    gl_npotable = false;
    if (COM_CheckParm("-nonpot"))
	return;
    if (!GL_ExtensionCheck("GL_ARB_texture_non_power_of_two"))
	return;

    Con_DPrintf("Non-power-of-two textures available.\n");
    gl_npotable = true;
}

void
GL_ExtensionCheck_MultiTexture()
{
    gl_mtexable = false;
    if (COM_CheckParm("-nomtex"))
        return;
    if (!GL_ExtensionCheck("GL_ARB_multitexture"))
        return;

    Con_Printf("ARB multitexture extensions found.\n");

    /* Check how many texture units there actually are */
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &gl_num_texture_units);
    if (gl_num_texture_units < 2) {
        Con_Printf("Only %i texture units, multitexture disabled.\n", gl_num_texture_units);
        return;
    }

    /* Retrieve function pointers for multitexture methods */
    qglMultiTexCoord2fARB = (lpMultiTexFUNC)GL_GetProcAddress("glMultiTexCoord2fARB");
    qglActiveTextureARB = (lpActiveTextureFUNC)GL_GetProcAddress("glActiveTextureARB");
    qglClientActiveTexture = (lpClientStateFUNC)GL_GetProcAddress("glClientActiveTexture");

    if (!qglMultiTexCoord2fARB || !qglActiveTextureARB || !qglClientActiveTexture) {
        Con_Printf("ARB Multitexture symbols not found, disabled.\n");
        return;
    }

    Con_Printf("Multitexture enabled.  %i texture units available.\n",
	       gl_num_texture_units);

    gl_mtexable = true;
}

static qboolean gl_version_es;
static int gl_version_major;
static int gl_version_minor;

void
GL_ParseVersionString(const char *version)
{
    char *gl_version, *token;

    if (!version)
        return;

    /* Get a writeable copy we can parse */
    gl_version = Z_StrDup(mainzone, version);

    /* OpenGL ES has a specific prefix, if found strip it off */
    if (!strncmp(gl_version, "OpenGL ES", 9)) {
        gl_version_es = true;
        token = strtok(gl_version, " .");
        token = strtok(NULL, " .");
        token = strtok(NULL, " .");
        gl_version_major = atoi(token);
        token = strtok(NULL, " .");
        gl_version_minor = atoi(token);
    } else {
        token = strtok(gl_version, " .");
        gl_version_major = atoi(token);
        token = strtok(NULL, " .");
        gl_version_minor = atoi(token);
    }

    Z_Free(mainzone, gl_version);
}

/*
 * Try to find a working version of automatic mipmap generation for
 * the special case of procedural textures (water/warp).
 */
lpGenerateMipmapFUNC qglGenerateMipmap;
void (*qglTexParameterGenerateMipmap)(GLboolean auto_mipmap);

static void APIENTRY qglGenerateMipmap_null(GLenum target) { };
static void qglTexParameterGenerateMipmap_null(GLboolean auto_mipmap) { };

static void
qglTexParameterGenerateMipmap_f(GLboolean auto_mipmap)
{
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, auto_mipmap);
}

#ifndef GL_GENERATE_MIPMAP_SGIS
#define GL_GENERATE_MIPMAP_SGIS 0x8191
#endif

static void
qglTexParameterGenerateMipmap_SGIS_f(GLboolean auto_mipmap)
{
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, auto_mipmap);
}

void
GL_ExtensionCheck_GenerateMipmaps()
{
    qglGenerateMipmap = NULL;
    qglTexParameterGenerateMipmap = NULL;

    if (gl_version_major >= 3 || GL_ExtensionCheck("GL_ARB_framebuffer_object")) {
        /* New enough to use the recommended glGenerateMipmap function */
        qglGenerateMipmap = GL_GetProcAddress("glGenerateMipmap");
    } else if ((gl_version_major == 1 && gl_version_minor >= 4) || gl_version_major > 1) {
        /* Fall back to legacy GL_GENERATE_MIPMAP texture property */
        qglTexParameterGenerateMipmap = qglTexParameterGenerateMipmap_f;
    } else if (GL_ExtensionCheck("GL_SGIS_generate_mipmap")) {
        /* Fall back top legacy-legacy extension for older than GL 1.4 */
        qglTexParameterGenerateMipmap = qglTexParameterGenerateMipmap_SGIS_f;
    }

    /*
     * This is a bit of a hack.  Should be that we flag auto-mipmap
     * generation on the texture properties and use that to
     * conditionally enable mipmaps but this will do for now.
     */
    if (qglGenerateMipmap || qglTexParameterGenerateMipmap)
        texture_properties[TEXTURE_TYPE_TURB].mipmap = true;

    if (!qglGenerateMipmap)
        qglGenerateMipmap = qglGenerateMipmap_null;
    if (!qglTexParameterGenerateMipmap)
        qglTexParameterGenerateMipmap = qglTexParameterGenerateMipmap_null;
}
