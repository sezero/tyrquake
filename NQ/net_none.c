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

#include "net.h"
#include "net_loop.h"

net_driver_t net_drivers[] = {
    {
	.name				= "Loopback",
	.initialized			= false,
        .AddCommands                    = Loop_AddCommands,
        .RegisterVariables              = Loop_RegisterVariables,
	.Init				= Loop_Init,
	.Listen				= Loop_Listen,
	.SearchForHosts			= Loop_SearchForHosts,
	.Connect			= Loop_Connect,
	.CheckNewConnections		= Loop_CheckNewConnections,
	.QGetMessage			= Loop_GetMessage,
	.QSendMessage			= Loop_SendMessage,
	.SendUnreliableMessage		= Loop_SendUnreliableMessage,
	.CanSendMessage			= Loop_CanSendMessage,
	.CanSendUnreliableMessage	= Loop_CanSendUnreliableMessage,
	.Close				= Loop_Close,
	.Shutdown			= Loop_Shutdown
    }
};
int net_numdrivers = 1;

net_landriver_t net_landrivers[0];
int net_numlandrivers = 0;
