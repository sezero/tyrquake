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
// in_null.c -- for systems without a mouse

#include "quakedef.h"

#ifdef NQ_HACK
#include "client.h"
#endif
#ifdef QW_HACK
#include "protocol.h"
#endif

cvar_t _windowed_mouse;

void
IN_Init(void)
{
}

void
IN_Shutdown(void)
{
}

void
IN_Commands(void)
{
}

void
IN_Move(usercmd_t *cmd)
{
}

void IN_Accumulate(void)
{
}

/*
===========
IN_ModeChanged
===========
*/
void
IN_ModeChanged(void)
{
}

void IN_AddCommands() {}

void IN_RegisterVariables()
{
    Cvar_RegisterVariable(&_windowed_mouse);
}
