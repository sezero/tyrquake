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

#ifndef NET_DGRM_H
#define NET_DGRM_H

#include "net.h"

// net_dgrm.h

void Datagram_AddCommands();
void Datagram_RegisterVariables();
int Datagram_Init();
void Datagram_Listen(qboolean state);
void Datagram_SearchForHosts(qboolean xmit);
qsocket_t *Datagram_Connect(const char *host);
qsocket_t *Datagram_CheckNewConnections(void);
int Datagram_GetMessage(qsocket_t *sock);
int Datagram_SendMessage(qsocket_t *sock, const sizebuf_t *data);
int Datagram_SendUnreliableMessage(qsocket_t *sock, const sizebuf_t *data);
qboolean Datagram_CanSendMessage(qsocket_t *sock);
qboolean Datagram_CanSendUnreliableMessage(qsocket_t *sock);
void Datagram_Close(qsocket_t *sock);
void Datagram_Shutdown(void);

#endif /* NET_DGRM_H */
