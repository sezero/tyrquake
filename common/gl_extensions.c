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

#include <assert.h>
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
    qglActiveTextureARB = (lpActiveTextureFUNC)GL_GetProcAddress("glActiveTextureARB");
    qglClientActiveTexture = (lpClientStateFUNC)GL_GetProcAddress("glClientActiveTexture");

    if (!qglActiveTextureARB || !qglClientActiveTexture) {
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

static inline qboolean
GL_VersionMinimum(int major, int minor)
{
    return gl_version_major > major || (gl_version_major == major && gl_version_minor >= minor);
}

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

    if (GL_VersionMinimum(3, 0) || GL_ExtensionCheck("GL_ARB_framebuffer_object")) {
        /* New enough to use the recommended glGenerateMipmap function */
        qglGenerateMipmap = GL_GetProcAddress("glGenerateMipmap");
    } else if (GL_VersionMinimum(1, 4)) {
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


qboolean gl_buffer_objects_enabled;

void (APIENTRY *qglBindBuffer)(GLenum target, GLuint buffer);
void (APIENTRY *qglDeleteBuffers)(GLsizei n, const GLuint *buffers);
void (APIENTRY *qglGenBuffers)(GLsizei n, GLuint *buffers);
void (APIENTRY *qglBufferData)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void (APIENTRY *qglBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
void *(APIENTRY *qglMapBuffer)(GLenum target, GLenum access);
GLboolean (APIENTRY *qglUnmapBuffer)(GLenum target);
GLboolean (APIENTRY *qglIsBuffer)(GLuint buffer);

void
GL_ExtensionCheck_BufferObjects()
{
    gl_buffer_objects_enabled = false;
    if (COM_CheckParm("-noglbuffers"))
        return;

    if (GL_VersionMinimum(2, 1)) {
        qglGenBuffers    = GL_GetProcAddress("glGenBuffers");
        qglDeleteBuffers = GL_GetProcAddress("glDeleteBuffers");
        qglBindBuffer    = GL_GetProcAddress("glBindBuffer");
        qglBufferData    = GL_GetProcAddress("glBufferData");
        qglBufferSubData = GL_GetProcAddress("glBufferSubData");
        qglMapBuffer     = GL_GetProcAddress("glMapBuffer");
        qglUnmapBuffer   = GL_GetProcAddress("glUnmapBuffer");
        qglIsBuffer      = GL_GetProcAddress("glIsBuffer");
    } else if (GL_ExtensionCheck("GL_ARB_vertex_buffer_object")) {
        qglGenBuffers    = GL_GetProcAddress("glGenBuffersARB");
        qglDeleteBuffers = GL_GetProcAddress("glDeleteBuffersARB");
        qglBindBuffer    = GL_GetProcAddress("glBindBufferARB");
        qglBufferData    = GL_GetProcAddress("glBufferDataARB");
        qglBufferSubData = GL_GetProcAddress("glBufferSubDataARB");
        qglMapBuffer     = GL_GetProcAddress("glMapBufferARB");
        qglUnmapBuffer   = GL_GetProcAddress("glUnmapBufferARB");
        qglIsBuffer      = GL_GetProcAddress("glIsBufferARB");
    }

    /* Enabled if we got all the function pointers */
    gl_buffer_objects_enabled =
        qglGenBuffers    &&
        qglDeleteBuffers &&
        qglBindBuffer    &&
        qglBufferData    &&
        qglBufferSubData &&
        qglMapBuffer     &&
        qglUnmapBuffer   &&
        qglIsBuffer;

    if (gl_buffer_objects_enabled)
        Con_Printf("***** GL Buffer Objects Enabled!\n");
}

struct vertex_buffers vbo;

void
GL_InitVBOs()
{
    assert(sizeof(vbo.handles) == sizeof(vbo));

    if (!gl_buffer_objects_enabled) {
        memset(&vbo, 0, sizeof(vbo));
        return;
    }
    qglGenBuffers(ARRAY_SIZE(vbo.handles), vbo.handles);
}


qboolean gl_vertex_program_enabled;

void (APIENTRY *qglProgramString)(GLenum target, GLenum format, GLsizei len, const void *string);
void (APIENTRY *qglBindProgram)(GLenum target, GLuint program);
void (APIENTRY *qglDeletePrograms)(GLsizei n, const GLuint *programs);
void (APIENTRY *qglGenPrograms)(GLsizei n, GLuint *programs);
void (APIENTRY *qglProgramEnvParameter4f)(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void (APIENTRY *qglProgramEnvParameter4fv)(GLenum target, GLuint index, const GLfloat *params);
void (APIENTRY *qglProgramLocalParameter4f)(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void (APIENTRY *qglProgramLocalParameter4fv)(GLenum target, GLuint index, const GLfloat *params);
void (APIENTRY *qglGetProgramiv)(GLenum target, GLenum pname, GLint *params);
void (APIENTRY *qglGetProgramString)(GLenum target, GLenum pname, void *string);
void (APIENTRY *qglEnableVertexAttribArray)(GLuint index);
void (APIENTRY *qglDisableVertexAttribArray)(GLuint index);
void (APIENTRY *qglVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
GLboolean (APIENTRY *qglIsProgram)(GLuint program);

void
GL_ExtensionCheck_VertexProgram()
{
    gl_vertex_program_enabled = false;
    if (COM_CheckParm("-noglvertexprogram"))
        return;

    // TODO: Is this available as a non-extension?
    if (GL_ExtensionCheck("GL_ARB_vertex_program")) {
        qglProgramString            = GL_GetProcAddress("glProgramStringARB");
        qglBindProgram              = GL_GetProcAddress("glBindProgramARB");
        qglDeletePrograms           = GL_GetProcAddress("glDeleteProgramsARB");
        qglGenPrograms              = GL_GetProcAddress("glGenProgramsARB");
        qglProgramLocalParameter4f  = GL_GetProcAddress("glProgramLocalParameter4fARB");
        qglProgramLocalParameter4fv = GL_GetProcAddress("glProgramLocalParameter4fvARB");
        qglProgramEnvParameter4f    = GL_GetProcAddress("glProgramEnvParameter4fARB");
        qglProgramEnvParameter4fv   = GL_GetProcAddress("glProgramEnvParameter4fvARB");
        qglEnableVertexAttribArray  = GL_GetProcAddress("glEnableVertexAttribArrayARB");
        qglDisableVertexAttribArray = GL_GetProcAddress("glDisableVertexAttribArrayARB");
        qglVertexAttribPointer      = GL_GetProcAddress("glVertexAttribPointerARB");
        qglIsProgram                = GL_GetProcAddress("glIsProgramARB");
    }

    gl_vertex_program_enabled =
        qglProgramString            &&
        qglBindProgram              &&
        qglDeletePrograms           &&
        qglGenPrograms              &&
        qglProgramLocalParameter4f  &&
        qglProgramLocalParameter4fv &&
        qglProgramEnvParameter4f    &&
        qglProgramEnvParameter4fv   &&
        qglEnableVertexAttribArray  &&
        qglDisableVertexAttribArray &&
        qglVertexAttribPointer      &&
        qglIsProgram;

    if (gl_vertex_program_enabled)
        Con_Printf("***** GL Vertex Program Enabled!\n");
}

// Need to track these program/shader resources and re-init if context goes away
struct vertex_programs vp;

/* Full pass - base texture with color plus fullbright mask texture overlay */
static const char *alias_lerp_full_vp_text =
    "!!ARBvp1.0\n"
    "\n"
    "ATTRIB pos0 = vertex.position;\n"
    "ATTRIB pos1 = vertex.attrib[1];\n"
    "PARAM  mat[4] = { state.matrix.mvp };\n"
    "PARAM  lerp0 = program.local[0];\n"
    "PARAM  lerp1 = program.local[1];\n"
    "TEMP   lerpPos;\n"
    "\n"
    "# Interpolate the vertex positions 0 and 1\n"
    "MUL    lerpPos, pos0, lerp0;\n"
    "MAD    lerpPos, pos1, lerp1, lerpPos;\n"
    "\n"
    "# Transform by concatenation of the MODELVIEW and PROJECTION matrices.\n"
    "TEMP   pos;\n"
    "DP4    pos.x, mat[0], lerpPos;\n"
    "DP4    pos.y, mat[1], lerpPos;\n"
    "DP4    pos.z, mat[2], lerpPos;\n"
    "DP4    pos.w, mat[3], lerpPos;\n"
    "MOV    result.position, pos;\n"
    "MOV    result.fogcoord, pos.z;\n"
    "\n"
    "# Pass two layers of texcoords and primary color unchanged.\n"
    "MOV    result.texcoord, vertex.texcoord;\n"
    "MOV    result.texcoord[1], vertex.texcoord[1];\n"
    "MOV    result.color, vertex.color;\n"
    "END\n";

/* Single texture and color/light */
static const char *alias_lerp_base_vp_text =
    "!!ARBvp1.0\n"
    "\n"
    "ATTRIB pos0 = vertex.position;\n"
    "ATTRIB pos1 = vertex.attrib[1];\n"
    "PARAM  mat[4] = { state.matrix.mvp };\n"
    "PARAM  lerp0 = program.local[0];\n"
    "PARAM  lerp1 = program.local[1];\n"
    "TEMP   lerpPos;\n"
    "\n"
    "# Interpolate the vertex positions 0 and 1\n"
    "MUL    lerpPos, pos0, lerp0;\n"
    "MAD    lerpPos, pos1, lerp1, lerpPos;\n"
    "\n"
    "# Transform by concatenation of the MODELVIEW and PROJECTION matrices.\n"
    "TEMP   pos;\n"
    "DP4    pos.x, mat[0], lerpPos;\n"
    "DP4    pos.y, mat[1], lerpPos;\n"
    "DP4    pos.z, mat[2], lerpPos;\n"
    "DP4    pos.w, mat[3], lerpPos;\n"
    "MOV    result.position, pos;\n"
    "MOV    result.fogcoord, pos.z;\n"
    "\n"
    "# Pass the texcoords and color unchanged.\n"
    "MOV    result.texcoord, vertex.texcoord;\n"
    "MOV    result.color, vertex.color;\n"
    "END\n";

#include "sys.h"

static qboolean
GL_CompileVertexProgram(GLuint handle, const char *text)
{
    qglBindProgram(GL_VERTEX_PROGRAM_ARB, handle);
    qglProgramString(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, strlen(text), text);
    qglBindProgram(GL_VERTEX_PROGRAM_ARB, 0);

    GLuint error = glGetError();
    if (!error) {
        Con_DPrintf("-----------------> Vertex Program Compiled!\n");
        return true;
    }

    Con_DPrintf("Vertex Program Error Code %d (0x%x)\n", error, error);
    const char *error_message = (const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
    int error_position;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &error_position);

    Con_DPrintf("Error Message: %s\n", error_message);
    Con_DPrintf("Error Position: %d\n", error_position);
    Con_DPrintf("Text near error: %.20s\n", text + error_position);

    return false;
}

void
GL_InitVertexPrograms()
{
    if (!gl_vertex_program_enabled)
        return;

    qglGenPrograms(ARRAY_SIZE(vp.handles), vp.handles);

    qboolean success = true;
    success &= GL_CompileVertexProgram(vp.alias_lerp_full, alias_lerp_full_vp_text);
    success &= GL_CompileVertexProgram(vp.alias_lerp_base, alias_lerp_base_vp_text);

    /* If some vertex programs failed to compile, disable the feature */
    if (!success) {
        Con_Printf(
            "WARNING: Some vertex programs failed to compile.\n"
            "         Vertex programs will be disabled.\n"
            "         Enable developer mode for further info.\n"
        );
        gl_vertex_program_enabled = false;
    }
}
