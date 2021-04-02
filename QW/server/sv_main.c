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

#include "buildinfo.h"
#include "cmd.h"
#include "console.h"
#include "model.h"
#include "net.h"
#include "pmove.h"
#include "qwsvdef.h"
#include "server.h"
#include "sys.h"
#include "zone.h"

quakeparms_t host_parms;

qboolean host_initialized;	// true if into command execution (compatability)

double host_frametime;
double realtime;		// without any filtering or bounding

int host_hunklevel;

netadr_t master_adr[MAX_MASTERS];	// address of group servers

cvar_t sv_mintic = { "sv_mintic", "0.03" };	// bound the size of the
cvar_t sv_maxtic = { "sv_maxtic", "0.1" };	// physics time tic

cvar_t developer = { "developer", "0" };	// show extra messages

static cvar_t timeout = { "timeout", "65" };	// seconds without any message
static cvar_t zombietime = { "zombietime", "2" }; // seconds to sink messages
						// after disconnect

static cvar_t rcon_password = { "rcon_password", "" };	// for remote commands
static cvar_t password = { "password", "" };	// for entering the game
static cvar_t spectator_password = { "spectator_password", "" }; // for entering as a spectator

cvar_t allow_download = { "allow_download", "1" };
cvar_t allow_download_skins = { "allow_download_skins", "1" };
cvar_t allow_download_models = { "allow_download_models", "1" };
cvar_t allow_download_sounds = { "allow_download_sounds", "1" };
cvar_t allow_download_maps = { "allow_download_maps", "1" };

cvar_t sv_highchars = { "sv_highchars", "1" };
cvar_t sv_phs = { "sv_phs", "1" };
cvar_t pausable = { "pausable", "1" };

//
// game rules mirrored in svs.info
//
cvar_t fraglimit = { "fraglimit", "0", .info = true };
cvar_t timelimit = { "timelimit", "0", .info = true };
cvar_t teamplay = { "teamplay", "0", .info = true };
cvar_t samelevel = { "samelevel", "0", .info = true };
cvar_t maxclients = { "maxclients", "8", .info = true };
cvar_t maxspectators = { "maxspectators", "8", .info = true };
cvar_t deathmatch = { "deathmatch", "1", .info = true };	// 0, 1, or 2
cvar_t spawn = { "spawn", "0", .info = true };
cvar_t watervis = { "watervis", "0", .info = true };
cvar_t hostname = { "hostname", "unnamed", .info = true };

FILE *sv_logfile;
FILE *sv_fraglogfile;

static void Master_Heartbeat(void);
static void Master_Shutdown(void);

//============================================================================

qboolean
ServerPaused(void)
{
    return sv.paused;
}

/*
================
SV_Shutdown

Quake calls this before calling Sys_Quit or Sys_Error
================
*/
void
SV_Shutdown(void)
{
    Master_Shutdown();
    if (sv_logfile) {
	fclose(sv_logfile);
	sv_logfile = NULL;
    }
    if (sv_fraglogfile) {
	fclose(sv_fraglogfile);
	sv_logfile = NULL;
    }
    NET_Shutdown();
}

/*
================
SV_Error

Sends a datagram to all the clients informing them of the server crash,
then exits
================
*/
void
SV_Error(const char *fmt, ...)
{
    va_list argptr;
    static char string[MAX_PRINTMSG];
    static qboolean inerror = false;

    if (inerror)
	Sys_Error("%s: recursively entered (%s)", __func__, string);

    inerror = true;

    va_start(argptr, fmt);
    qvsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    Con_Printf("%s: %s\n", __func__, string);

    SV_FinalMessage(va("server crashed: %s\n", string));

    SV_Shutdown();

    Sys_Error("%s: %s", __func__, string);
}

/*
==================
SV_FinalMessage

Used by SV_Error and SV_Quit_f to send a final message to all connected
clients before the server goes down.  The messages are sent immediately,
not just stuck on the outgoing message list, because the server is going
to totally exit after returning from this function.
==================
*/
void
SV_FinalMessage(const char *message)
{
    int i;
    client_t *cl;

    SZ_Clear(&net_message);
    MSG_WriteByte(&net_message, svc_print);
    MSG_WriteByte(&net_message, PRINT_HIGH);
    MSG_WriteString(&net_message, message);
    MSG_WriteByte(&net_message, svc_disconnect);

    for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++)
	if (cl->state >= cs_spawned)
	    Netchan_Transmit(&cl->netchan, net_message.cursize,
			     net_message.data);
}



/*
=====================
SV_DropClient

Called when the player is totally leaving the server, either willingly
or unwillingly.  This is NOT called if the entire server is quiting
or crashing.
=====================
*/
void
SV_DropClient(client_t *drop)
{
    // add the disconnect
    MSG_WriteByte(&drop->netchan.message, svc_disconnect);

    if (drop->state == cs_spawned) {
	if (!drop->spectator) {
	    // call the prog function for removing a client
	    // this will set the body to a dead frame, among other things
	    pr_global_struct->self = EDICT_TO_PROG(drop->edict);
	    PR_ExecuteProgram(pr_global_struct->ClientDisconnect);
	} else if (SpectatorDisconnect) {
	    // call the prog function for removing a client
	    // this will set the body to a dead frame, among other things
	    pr_global_struct->self = EDICT_TO_PROG(drop->edict);
	    PR_ExecuteProgram(SpectatorDisconnect);
	}
    }

    if (drop->spectator)
	Con_Printf("Spectator %s removed\n", drop->name);
    else
	Con_Printf("Client %s removed\n", drop->name);

    if (drop->download) {
	fclose(drop->download);
	drop->download = NULL;
    }
    if (drop->upload) {
	fclose(drop->upload);
	drop->upload = NULL;
    }
    *drop->uploadfn = 0;

    drop->state = cs_zombie;	// become free in a few seconds
    drop->connection_started = realtime;	// for zombie timeout

    drop->old_frags = 0;
    drop->edict->v.frags = 0;
    drop->name[0] = 0;
    memset(drop->userinfo, 0, sizeof(drop->userinfo));

// send notification to all remaining clients
    SV_FullClientUpdate(drop, &sv.reliable_datagram);
}


//====================================================================

/*
===================
SV_CalcPing

===================
*/
int
SV_CalcPing(client_t *cl)
{
    float ping;
    int i;
    int count;
    register client_frame_t *frame;

    ping = 0;
    count = 0;
    for (frame = cl->frames, i = 0; i < UPDATE_BACKUP; i++, frame++) {
	if (frame->ping_time > 0) {
	    ping += frame->ping_time;
	    count++;
	}
    }
    if (!count)
	return 9999;
    ping /= count;

    return ping * 1000;
}

/*
===================
SV_FullClientUpdate

Writes all update values to a sizebuf
===================
*/
void
SV_FullClientUpdate(client_t *client, sizebuf_t *buf)
{
    int i;
    char info[MAX_INFO_STRING];

    i = client - svs.clients;

//Sys_Printf("SV_FullClientUpdate:  Updated frags for client %d\n", i);

    MSG_WriteByte(buf, svc_updatefrags);
    MSG_WriteByte(buf, i);
    MSG_WriteShort(buf, client->old_frags);

    MSG_WriteByte(buf, svc_updateping);
    MSG_WriteByte(buf, i);
    MSG_WriteShort(buf, SV_CalcPing(client));

    MSG_WriteByte(buf, svc_updatepl);
    MSG_WriteByte(buf, i);
    MSG_WriteByte(buf, client->lossage);

    MSG_WriteByte(buf, svc_updateentertime);
    MSG_WriteByte(buf, i);
    MSG_WriteFloat(buf, realtime - client->connection_started);

    /*
     * FIXME - this is safe becuase client->userinfo is also MAX_INFO_STRING
     * bytes, but this is just ugly. Maintain the prefixed keys in a separate
     * buffer?
     */
    strcpy(info, client->userinfo);
    Info_RemovePrefixedKeys(info, '_');	// server passwords, etc

    MSG_WriteByte(buf, svc_updateuserinfo);
    MSG_WriteByte(buf, i);
    MSG_WriteLong(buf, client->userid);
    MSG_WriteString(buf, info);
}

/*
===================
SV_FullClientUpdateToClient

Writes all update values to a client's reliable stream
===================
*/
void
SV_FullClientUpdateToClient(client_t *client, client_t *cl)
{
    ClientReliableCheckBlock(cl, 24 + strlen(client->userinfo));
    if (cl->num_backbuf) {
	SV_FullClientUpdate(client, &cl->backbuf);
	ClientReliable_FinishWrite(cl);
    } else
	SV_FullClientUpdate(client, &cl->netchan.message);
}


/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see
This message can be up to around 5k with worst case string lengths.
================
*/
static void
SVC_Status(void)
{
    int i;
    client_t *cl;
    int ping;
    int top, bottom;

    Cmd_TokenizeString("status");
    SV_BeginRedirect(RD_PACKET, NULL);
    Con_Printf("%s\n", svs.info);
    for (i = 0; i < MAX_CLIENTS; i++) {
	cl = &svs.clients[i];
	if ((cl->state == cs_connected || cl->state == cs_spawned)
	    && !cl->spectator) {
	    top = atoi(Info_ValueForKey(cl->userinfo, "topcolor"));
	    bottom = atoi(Info_ValueForKey(cl->userinfo, "bottomcolor"));
	    top = (top < 0) ? 0 : ((top > 13) ? 13 : top);
	    bottom = (bottom < 0) ? 0 : ((bottom > 13) ? 13 : bottom);
	    ping = SV_CalcPing(cl);
	    Con_Printf("%i %i %i %i \"%s\" \"%s\" %i %i\n", cl->userid,
		       cl->old_frags,
		       (int)(realtime - cl->connection_started) / 60, ping,
		       cl->name, Info_ValueForKey(cl->userinfo, "skin"),
		       top, bottom);
	}
    }
    SV_EndRedirect();
}

/*
===================
SV_CheckLog

===================
*/
#define LOG_HIGHWATER 4096
#define LOG_FLUSH 10*60
static void
SV_CheckLog(void)
{
    sizebuf_t *sz;

    sz = &svs.log[svs.logsequence & 1];

    // bump sequence if almost full, or ten minutes have passed and
    // there is something still sitting there
    if (sz->cursize > LOG_HIGHWATER
	|| (realtime - svs.logtime > LOG_FLUSH && sz->cursize)) {
	// swap buffers and bump sequence
	svs.logtime = realtime;
	svs.logsequence++;
	sz = &svs.log[svs.logsequence & 1];
	sz->cursize = 0;
	Con_Printf("beginning fraglog sequence %i\n", svs.logsequence);
    }

}

/*
================
SVC_Log

Responds with all the logged frags for ranking programs.
If a sequence number is passed as a parameter and it is
the same as the current sequence, an A2A_NACK will be returned
instead of the data.
================
*/
static void
SVC_Log(void)
{
    int seq;
    char data[MAX_DATAGRAM + 64];

    if (Cmd_Argc() == 2)
	seq = atoi(Cmd_Argv(1));
    else
	seq = -1;

    /* they already have this data, or we aren't logging frags */
    if (seq == svs.logsequence - 1 || !sv_fraglogfile) {
	data[0] = A2A_NACK;
	NET_SendPacket(1, data, net_from);
	return;
    }

    Con_DPrintf("sending log %i to %s\n", svs.logsequence - 1,
		NET_AdrToString(net_from));

    qsnprintf(data, sizeof(data), "stdlog %i\n", svs.logsequence - 1);
    strcat(data, (char *)svs.log_buf[((svs.logsequence - 1) & 1)]);

    NET_SendPacket(strlen(data) + 1, data, net_from);
}

/*
================
SVC_Ping

Just responds with an acknowledgement
================
*/
static void
SVC_Ping(void)
{
    char data;

    data = A2A_ACK;

    NET_SendPacket(1, &data, net_from);
}

/*
=================
SVC_GetChallenge

Returns a challenge number that can be used
in a subsequent client_connect command.
We do this to prevent denial of service attacks that
flood the server with invalid connection IPs.  With a
challenge, they must give a valid IP address.
=================
*/
static void
SVC_GetChallenge(void)
{
    int i;
    int oldest;
    int oldestTime;

    oldest = 0;
    oldestTime = 0x7fffffff;

    // see if we already have a challenge for this ip
    for (i = 0; i < MAX_CHALLENGES; i++) {
	if (NET_CompareBaseAdr(net_from, svs.challenges[i].adr))
	    break;
	if (svs.challenges[i].time < oldestTime) {
	    oldestTime = svs.challenges[i].time;
	    oldest = i;
	}
    }

    if (i == MAX_CHALLENGES) {
	// overwrite the oldest
	svs.challenges[oldest].challenge = (rand() << 16) ^ rand();
	svs.challenges[oldest].adr = net_from;
	svs.challenges[oldest].time = realtime;
	i = oldest;
    }
    // send it back
    Netchan_OutOfBandPrint(net_from, "%c%i", S2C_CHALLENGE,
			   svs.challenges[i].challenge);
}

/*
==================
SVC_DirectConnect

A connection request that did not come from the master
==================
*/
static void
SVC_DirectConnect(void)
{
    static int userid;
    netadr_t adr;
    int i;
    client_t *cl, *newcl, temp;
    edict_t *ent;
    int edictnum;
    char *s;
    int clients, spectators;
    qboolean spectator;
    int qport;
    int version;
    int challenge;
    char userinfo[sizeof(newcl->userinfo)];

    version = atoi(Cmd_Argv(1));
    if (version != PROTOCOL_VERSION) {
	Netchan_OutOfBandPrint(net_from, "%c\nServer is protocol version %i\n",
			       A2C_PRINT, PROTOCOL_VERSION);
	Con_Printf("* rejected connect from version %i\n", version);
	return;
    }

    qport = atoi(Cmd_Argv(2));
    challenge = atoi(Cmd_Argv(3));
    qstrncpy(userinfo, Cmd_Argv(4), sizeof(userinfo));

    // see if the challenge is valid
    for (i = 0; i < MAX_CHALLENGES; i++) {
	if (NET_CompareBaseAdr(net_from, svs.challenges[i].adr)) {
	    if (challenge == svs.challenges[i].challenge)
		break;		// good
	    Netchan_OutOfBandPrint(net_from, "%c\nBad challenge.\n",
				   A2C_PRINT);
	    return;
	}
    }
    if (i == MAX_CHALLENGES) {
	Netchan_OutOfBandPrint(net_from, "%c\nNo challenge for address.\n",
			       A2C_PRINT);
	return;
    }
    // check for password or spectator_password
    s = Info_ValueForKey(userinfo, "spectator");
    if (s[0] && strcmp(s, "0")) {
	if (spectator_password.string[0] &&
	    strcasecmp(spectator_password.string, "none") &&
	    strcmp(spectator_password.string, s)) {	// failed
	    Con_Printf("%s:spectator password failed\n",
		       NET_AdrToString(net_from));
	    Netchan_OutOfBandPrint(net_from,
				   "%c\nrequires a spectator password\n\n",
				   A2C_PRINT);
	    return;
	}
	Info_RemoveKey(userinfo, "spectator");	// remove passwd
	Info_SetValueForStarKey(userinfo, "*spectator", "1", MAX_INFO_STRING);
	spectator = true;
    } else {
	s = Info_ValueForKey(userinfo, "password");
	if (password.string[0] &&
	    strcasecmp(password.string, "none") &&
	    strcmp(password.string, s)) {
	    Con_Printf("%s:password failed\n", NET_AdrToString(net_from));
	    Netchan_OutOfBandPrint(net_from,
				   "%c\nserver requires a password\n\n",
				   A2C_PRINT);
	    return;
	}
	spectator = false;
	Info_RemoveKey(userinfo, "password");	// remove passwd
    }

    adr = net_from;
    userid++;			// so every client gets a unique id

    newcl = &temp;
    memset(newcl, 0, sizeof(client_t));

    newcl->userid = userid;

    if (!sv_highchars.value) {
        /* Custom strcpy which drops any chars outside ascii range */
        byte *src = (byte *)userinfo;
        byte *dst = (byte *)newcl->userinfo;
        for (i = 0; *src && i < sizeof(newcl->userinfo) - 1; src++, i++) {
            if (*src > 31 && *src <= 127) {
                *dst++ = *src;
            }
        }
        *dst = 0;
    } else {
        qstrncpy(newcl->userinfo, userinfo, sizeof(newcl->userinfo));
    }

    // if there is already a slot for this ip, drop it
    for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
	if (cl->state == cs_free)
	    continue;
	if (NET_CompareBaseAdr(adr, cl->netchan.remote_address)
	    && (cl->netchan.qport == qport
		|| adr.port == cl->netchan.remote_address.port)) {
	    if (cl->state == cs_connected) {
		Con_Printf("%s:dup connect\n", NET_AdrToString(adr));
		userid--;
		return;
	    }

	    Con_Printf("%s:reconnect\n", NET_AdrToString(adr));
	    SV_DropClient(cl);
	    break;
	}
    }

    // count up the clients and spectators
    clients = 0;
    spectators = 0;
    for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
	if (cl->state == cs_free)
	    continue;
	if (cl->spectator)
	    spectators++;
	else
	    clients++;
    }

    // if at server limits, refuse connection
    if (maxclients.value > MAX_CLIENTS)
	Cvar_SetValue("maxclients", MAX_CLIENTS);
    if (maxspectators.value > MAX_CLIENTS)
	Cvar_SetValue("maxspectators", MAX_CLIENTS);
    if (maxspectators.value + maxclients.value > MAX_CLIENTS)
	Cvar_SetValue("maxspectators",
		      MAX_CLIENTS - maxspectators.value + maxclients.value);
    if ((spectator && spectators >= (int)maxspectators.value)
	|| (!spectator && clients >= (int)maxclients.value)) {
	Con_Printf("%s:full connect\n", NET_AdrToString(adr));
	Netchan_OutOfBandPrint(adr, "%c\nserver is full\n\n", A2C_PRINT);
	return;
    }
    // find a client slot
    newcl = NULL;
    for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
	if (cl->state == cs_free) {
	    newcl = cl;
	    break;
	}
    }
    if (!newcl) {
	Con_Printf("WARNING: miscounted available clients\n");
	return;
    }
    // build a new connection
    // accept the new client
    // this is the only place a client_t is ever initialized
    *newcl = temp;

    Netchan_OutOfBandPrint(adr, "%c", S2C_CONNECTION);

    edictnum = (newcl - svs.clients) + 1;

    Netchan_Setup(&newcl->netchan, adr, qport);

    newcl->state = cs_connected;

    newcl->datagram.allowoverflow = true;
    newcl->datagram.data = newcl->datagram_buf;
    newcl->datagram.maxsize = sizeof(newcl->datagram_buf);

    // spectator mode can ONLY be set at join time
    newcl->spectator = spectator;

    ent = EDICT_NUM(edictnum);
    newcl->edict = ent;

    // parse some info from the info strings
    SV_ExtractFromUserinfo(newcl);

    // JACK: Init the floodprot stuff.
    for (i = 0; i < 10; i++)
	newcl->whensaid[i] = 0.0;
    newcl->whensaidhead = 0;
    newcl->lockedtill = 0;

    // call the progs to get default spawn parms for the new client
    PR_ExecuteProgram(pr_global_struct->SetNewParms);
    for (i = 0; i < NUM_SPAWN_PARMS; i++)
	newcl->spawn_parms[i] = (&pr_global_struct->parm1)[i];

    if (newcl->spectator)
	Con_Printf("Spectator %s connected\n", newcl->name);
    else
	Con_DPrintf("Client %s connected\n", newcl->name);
    newcl->sendinfo = true;
}

int
Rcon_Validate(void)
{
    if (!strlen(rcon_password.string))
	return 0;

    if (strcmp(Cmd_Argv(1), rcon_password.string))
	return 0;

    return 1;
}

/*
===============
SVC_RemoteCommand

A client issued an rcon command.
Shift down the remaining args
Redirect all printfs
===============
*/
static void
SVC_RemoteCommand(void)
{
    int i;
    char remaining[1024];

    if (!Rcon_Validate()) {
	Con_Printf("Bad rcon from %s:\n%s\n", NET_AdrToString(net_from),
		   net_message.data + 4);
	SV_BeginRedirect(RD_PACKET, NULL);
	Con_Printf("Bad rcon_password.\n");
    } else {
	Con_Printf("Rcon from %s:\n%s\n", NET_AdrToString(net_from),
		   net_message.data + 4);
	SV_BeginRedirect(RD_PACKET, NULL);
	remaining[0] = 0;

	for (i = 2; i < Cmd_Argc(); i++) {
	    strcat(remaining, Cmd_Argv(i));
	    strcat(remaining, " ");
	}
	Cmd_ExecuteString(remaining);
    }
    SV_EndRedirect();
}


/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void
SV_ConnectionlessPacket(void)
{
    const char *cmdstring;
    const char *cmd;

    MSG_BeginReading();
    MSG_ReadLong();		// skip the -1 marker

    cmdstring = MSG_ReadStringLine();
    Cmd_TokenizeString(cmdstring);
    cmd = Cmd_Argv(0);

    if (!strcmp(cmd, "ping"))
	SVC_Ping();
    else if (cmd[0] == A2A_PING && (cmd[1] == 0 || cmd[1] == '\n'))
	SVC_Ping();
    else if (cmd[0] == A2A_ACK && (cmd[1] == 0 || cmd[1] == '\n'))
	Con_Printf("A2A_ACK from %s\n", NET_AdrToString(net_from));
    else if (!strcmp(cmd, "status"))
	SVC_Status();
    else if (!strcmp(cmd, "log"))
	SVC_Log();
    else if (!strcmp(cmd, "connect"))
	SVC_DirectConnect();
    else if (!strcmp(cmd, "getchallenge"))
	SVC_GetChallenge();
    else if (!strcmp(cmd, "rcon"))
	SVC_RemoteCommand();
    else
	Con_Printf("bad connectionless packet from %s:\n%s\n",
		   NET_AdrToString(net_from), cmdstring);
}

/*
==============================================================================

PACKET FILTERING

You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and any unspecified digits will
match any value, so you can specify an entire class C network with "addip
192.246.40".

Removeip will only remove an address specified exactly the same way.  You
cannot addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

writeip
Dumps "addip <ip>" commands to listip.cfg so it can be execed at a later date.
The filter lists are not saved and restored by default, because I beleive it
would cause too much confusion.

filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be
prohibited from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you
easily set up a private game, or a game that only allows players from your
local network.

==============================================================================
*/

typedef struct {
    union {
	unsigned l;
	byte b[4];
    } mask;
    union {
	unsigned l;
	byte b[4];
    } addr;
} ipfilter_t;

#define	MAX_IPFILTERS	1024

static ipfilter_t ipfilters[MAX_IPFILTERS];
static int numipfilters;

static cvar_t filterban = { "filterban", "1" };

/*
=================
StringToFilter
=================
*/
static qboolean
StringToFilter(const char *in, ipfilter_t *f)
{
    char num[4];
    int i, j, b;
    const char *s = in;

    for (i = 0; i < 4; i++) {
	f->addr.b[i] = 0;
	f->mask.b[i] = 0;
    }

    for (i = 0; i < 4; i++) {
	if (*s < '0' || *s > '9')
	    goto badaddr;

	j = 0;
	while (*s >= '0' && *s <= '9' && j < 4)
	    num[j++] = *s++;
	num[j] = 0;
	b = atoi(num);
	if (b > 0xff) {
	    s -= j;
	    goto badaddr;
	}
	f->addr.b[i] = b;
	f->mask.b[i] = 0xff;

	if (!*s || *s == '/')
	    break;
	if (i < 3) {
	    if (*s != '.')
		goto badaddr;
	    s++;
	    if (!*s)
		break;
	}
    }

    /* If the IP or prefix looks ok, user can specify a mask */
    if (*s) {
	if (*s != '/')
	    goto badaddr;
	s++;
	j = 0;
	while (*s >= '0' && *s <= '9' && j < 3)
	    num[j++] = *s++;
	num[j] = 0;
	b = atoi(num);
	if (b > 32 || *s)
	    goto badaddr;
	f->mask.l = BigLong(0xffffffff << (32 - b));
    }
    return true;

 badaddr:
    Con_Printf("Bad filter address: %s\n", in);
    Con_Printf("                    %*s\n", (int)(s - in) + 1, "^");
    return false;
}

/*
=================
SV_AddIP_f
=================
*/
static void
SV_AddIP_f(void)
{
    int i;

    for (i = 0; i < numipfilters; i++)
	if (ipfilters[i].addr.l == 0xffffffff)
	    break;		// free spot
    if (i == numipfilters) {
	if (numipfilters == MAX_IPFILTERS) {
	    Con_Printf("IP filter list is full\n");
	    return;
	}
	numipfilters++;
    }

    if (!StringToFilter(Cmd_Argv(1), &ipfilters[i]))
	ipfilters[i].addr.l = 0xffffffff;
}

/*
=================
SV_RemoveIP_f
=================
*/
static void
SV_RemoveIP_f(void)
{
    ipfilter_t f;
    int i, j;

    if (!StringToFilter(Cmd_Argv(1), &f))
	return;
    for (i = 0; i < numipfilters; i++) {
	const ipfilter_t *tmp = &ipfilters[i];
	if (tmp->mask.l == f.mask.l && tmp->addr.l == f.addr.l) {
	    for (j = i + 1; j < numipfilters; j++)
		ipfilters[j - 1] = ipfilters[j];
	    numipfilters--;
	    Con_Printf("Removed.\n");
	    return;
	}
    }
    Con_Printf("Didn't find %s.\n", Cmd_Argv(1));
}

/*
=================
SV_ListIP_f
=================
*/
static void
SV_ListIP_f(void)
{
    int i, j, mask;

    Con_Printf("Filter list:\n");
    for (i = 0; i < numipfilters; i++) {
	const byte *b = ipfilters[i].addr.b;
	const unsigned m = ipfilters[i].mask.l;
	mask = 0;
	for (j = 0; j < 32; j++) {
	    if ((m >> j) & 0x1)
		mask++;
	}
	Con_Printf("%3i.%3i.%3i.%3i / %2i\n", b[0], b[1], b[2], b[3], mask);
    }
}

/*
=================
SV_WriteIP_f
=================
*/
static void
SV_WriteIP_f(void)
{
    FILE *f;
    char name[MAX_OSPATH];
    int i;

    qsnprintf(name, sizeof(name), "%s/listip.cfg", com_gamedir);

    Con_Printf("Writing %s.\n", name);

    f = fopen(name, "wb");
    if (!f) {
	Con_Printf("Couldn't open %s\n", name);
	return;
    }

    for (i = 0; i < numipfilters; i++) {
	const byte *b = ipfilters[i].addr.b;
	fprintf(f, "addip %i.%i.%i.%i\n", b[0], b[1], b[2], b[3]);
    }

    fclose(f);
}

/*
=================
SV_SendBan
=================
*/
static void
SV_SendBan(void)
{
    char data[128];

    data[0] = data[1] = data[2] = data[3] = 0xff;
    data[4] = A2C_PRINT;
    data[5] = 0;
    strcat(data, "\nbanned.\n");

    NET_SendPacket(strlen(data), data, net_from);
}

/*
=================
SV_FilterPacket
=================
*/
static qboolean
SV_FilterPacket(void)
{
    int i;
    unsigned in;

    /* FIXME - be smarter about this */
    in  = net_from.ip.b[0] << 24;
    in |= net_from.ip.b[1] << 16;
    in |= net_from.ip.b[2] << 8;
    in |= net_from.ip.b[3];
    in = BigLong(in);

    for (i = 0; i < numipfilters; i++)
	if ((in & ipfilters[i].mask.l) == ipfilters[i].addr.l)
	    return filterban.value;

    return !filterban.value;
}

//============================================================================

/*
=================
SV_ReadPackets
=================
*/
static void
SV_ReadPackets(void)
{
    int i;
    client_t *cl;
    int qport;

    while (NET_GetPacket()) {
	if (SV_FilterPacket()) {
	    SV_SendBan();	// tell them we aren't listening...
	    continue;
	}
	// check for connectionless packet (0xffffffff) first
	if (*(int *)net_message.data == -1) {
	    SV_ConnectionlessPacket();
	    continue;
	}
	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReading();
	MSG_ReadLong();		// sequence number
	MSG_ReadLong();		// sequence number
	qport = MSG_ReadShort() & 0xffff;

	// check for packets from connected clients
	for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
	    if (cl->state == cs_free)
		continue;
	    if (!NET_CompareBaseAdr(net_from, cl->netchan.remote_address))
		continue;
	    if (cl->netchan.qport != qport)
		continue;
	    if (cl->netchan.remote_address.port != net_from.port) {
		Con_DPrintf("SV_ReadPackets: fixing up a translated port\n");
		cl->netchan.remote_address.port = net_from.port;
	    }
	    if (Netchan_Process(&cl->netchan)) {
		/* this is a valid, sequenced packet, so process it */
		svs.stats.packets++;
		cl->send_message = true;	/* reply at end of frame */
		if (cl->state != cs_zombie)
		    SV_ExecuteClientMessage(cl);
	    }
	    break;
	}

	if (i != MAX_CLIENTS)
	    continue;

	// packet is not from a known client
	//      Con_Printf ("%s:sequenced packet without connection\n"
	// ,NET_AdrToString(net_from));
    }
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client in timeout.value
seconds, drop the conneciton.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
static void
SV_CheckTimeouts(void)
{
    int i;
    client_t *cl;
    float droptime;
    int nclients;

    droptime = realtime - timeout.value;
    nclients = 0;

    for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
	if (cl->state == cs_connected || cl->state == cs_spawned) {
	    if (!cl->spectator)
		nclients++;
	    if (cl->netchan.last_received < droptime) {
		SV_BroadcastPrintf(PRINT_HIGH, "%s timed out\n", cl->name);
		SV_DropClient(cl);
		cl->state = cs_free;	// don't bother with zombie state
	    }
	}
	if (cl->state == cs_zombie &&
	    realtime - cl->connection_started > zombietime.value) {
	    cl->state = cs_free;	// can now be reused
	}
    }
    if (sv.paused && !nclients) {
	// nobody left, unpause the server
	SV_TogglePause("Pause released since no players are left.\n");
    }
}

/*
===================
SV_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
static void
SV_GetConsoleCommands(void)
{
    char *cmd;

    while (1) {
	cmd = Sys_ConsoleInput();
	if (!cmd)
	    break;
	Cbuf_AddText("%s", cmd);
    }
}

/*
===================
SV_CheckVars

===================
*/
static void
SV_CheckVars(void)
{
    static const char *pw, *spw;
    int v;

    if (password.string == pw && spectator_password.string == spw)
	return;
    pw = password.string;
    spw = spectator_password.string;

    v = 0;
    if (pw && pw[0] && strcmp(pw, "none"))
	v |= 1;
    if (spw && spw[0] && strcmp(spw, "none"))
	v |= 2;

    Con_Printf("Updated needpass.\n");
    if (!v)
	Info_SetValueForKey(svs.info, "needpass", "", MAX_SERVERINFO_STRING);
    else
	Info_SetValueForKey(svs.info, "needpass", va("%i", v),
			    MAX_SERVERINFO_STRING);
}

/*
==================
SV_Frame

==================
*/
void
SV_Frame(float time)
{
    static double start, end;

    start = Sys_DoubleTime();
    svs.stats.idle += start - end;

// keep the random time dependent
    rand();

// decide the simulation time
    if (!sv.paused) {
	realtime += time;
	sv.time += time;
    }
// check timeouts
    SV_CheckTimeouts();

// toggle the log buffer if full
    SV_CheckLog();

// move autonomous things around if enough time has passed
    if (!sv.paused)
	SV_Physics();

// get packets
    SV_ReadPackets();

// check for commands typed to the host
    SV_GetConsoleCommands();

// process console commands
    Cbuf_Execute();

    SV_CheckVars();

// send messages back to the clients that had packets read this frame
    SV_SendClientMessages();

// send a heartbeat to the master if needed
    Master_Heartbeat();

// collect timing statistics
    end = Sys_DoubleTime();
    svs.stats.active += end - start;
    if (++svs.stats.count == STATFRAMES) {
	svs.stats.latched_active = svs.stats.active;
	svs.stats.latched_idle = svs.stats.idle;
	svs.stats.latched_packets = svs.stats.packets;
	svs.stats.active = 0;
	svs.stats.idle = 0;
	svs.stats.packets = 0;
	svs.stats.count = 0;
    }
}

/*
===============
SV_InitLocal
===============
*/
static void
SV_InitLocal(void)
{
    SV_InitOperatorCommands();
    SV_ModelInit();
    SV_UserInit();

    Cvar_RegisterVariable(&rcon_password);
    Cvar_RegisterVariable(&password);
    Cvar_RegisterVariable(&spectator_password);

    Cvar_RegisterVariable(&sv_mintic);
    Cvar_RegisterVariable(&sv_maxtic);

    Cvar_RegisterVariable(&fraglimit);
    Cvar_RegisterVariable(&timelimit);
    Cvar_RegisterVariable(&teamplay);
    Cvar_RegisterVariable(&samelevel);
    Cvar_RegisterVariable(&maxclients);
    Cvar_RegisterVariable(&maxspectators);
    Cvar_RegisterVariable(&hostname);
    Cvar_RegisterVariable(&deathmatch);
    Cvar_RegisterVariable(&spawn);
    Cvar_RegisterVariable(&watervis);

    Cvar_RegisterVariable(&timeout);
    Cvar_RegisterVariable(&zombietime);

    Cvar_RegisterVariable(&sv_maxvelocity);
    Cvar_RegisterVariable(&sv_gravity);
    Cvar_RegisterVariable(&sv_stopspeed);
    Cvar_RegisterVariable(&sv_maxspeed);
    Cvar_RegisterVariable(&sv_spectatormaxspeed);
    Cvar_RegisterVariable(&sv_accelerate);
    Cvar_RegisterVariable(&sv_airaccelerate);
    Cvar_RegisterVariable(&sv_wateraccelerate);
    Cvar_RegisterVariable(&sv_friction);
    Cvar_RegisterVariable(&sv_waterfriction);

    Cvar_RegisterVariable(&sv_aim);

    Cvar_RegisterVariable(&filterban);

    Cvar_RegisterVariable(&allow_download);
    Cvar_RegisterVariable(&allow_download_skins);
    Cvar_RegisterVariable(&allow_download_models);
    Cvar_RegisterVariable(&allow_download_sounds);
    Cvar_RegisterVariable(&allow_download_maps);

    Cvar_RegisterVariable(&sv_highchars);

    Cvar_RegisterVariable(&sv_phs);

    Cvar_RegisterVariable(&pausable);

    Cvar_RegisterVariable(&developer);
    if (COM_CheckParm("-developer"))
	Cvar_SetValue("developer", 1);

    Cmd_AddCommand("addip", SV_AddIP_f);
    Cmd_AddCommand("removeip", SV_RemoveIP_f);
    Cmd_AddCommand("listip", SV_ListIP_f);
    Cmd_AddCommand("writeip", SV_WriteIP_f);

    Info_SetValueForStarKey(svs.info, "*version",
			    va("TyrQuake-%s", build_version), MAX_SERVERINFO_STRING);

    // init fraglog stuff
    svs.logsequence = 1;
    svs.logtime = realtime;
    svs.log[0].data = svs.log_buf[0];
    svs.log[0].maxsize = sizeof(svs.log_buf[0]);
    svs.log[0].cursize = 0;
    svs.log[0].allowoverflow = true;
    svs.log[1].data = svs.log_buf[1];
    svs.log[1].maxsize = sizeof(svs.log_buf[1]);
    svs.log[1].cursize = 0;
    svs.log[1].allowoverflow = true;
}


//============================================================================

/*
================
Master_Heartbeat

Send a message to the master every few minutes to
let it know we are alive, and log information
================
*/
#define	HEARTBEAT_SECONDS	300
static void
Master_Heartbeat(void)
{
    char string[2048];
    int active;
    int i;

    if (realtime - svs.last_heartbeat < HEARTBEAT_SECONDS)
	return;			// not time to send yet

    svs.last_heartbeat = realtime;

    //
    // count active users
    //
    active = 0;
    for (i = 0; i < MAX_CLIENTS; i++)
	if (svs.clients[i].state == cs_connected ||
	    svs.clients[i].state == cs_spawned)
	    active++;

    svs.heartbeat_sequence++;
    qsnprintf(string, sizeof(string), "%c\n%i\n%i\n", S2M_HEARTBEAT,
	    svs.heartbeat_sequence, active);

    // send to group master
    for (i = 0; i < MAX_MASTERS; i++)
	if (master_adr[i].port) {
	    Con_Printf("Sending heartbeat to %s\n",
		       NET_AdrToString(master_adr[i]));
	    NET_SendPacket(strlen(string), string, master_adr[i]);
	}
}

/*
=================
Master_Shutdown

Informs all masters that this server is going down
=================
*/
static void
Master_Shutdown(void)
{
    char string[2048];
    int i;

    qsnprintf(string, sizeof(string), "%c\n", S2M_SHUTDOWN);

    // send to group master
    for (i = 0; i < MAX_MASTERS; i++)
	if (master_adr[i].port) {
	    Con_Printf("Sending heartbeat to %s\n",
		       NET_AdrToString(master_adr[i]));
	    NET_SendPacket(strlen(string), string, master_adr[i]);
	}
}

/*
=================
SV_ExtractFromUserinfo

Pull specific info from a newly changed userinfo string
into a more C freindly form.
=================
*/
void
SV_ExtractFromUserinfo(client_t *cl)
{
    char *val, *p, *q;
    int i;
    client_t *client;
    int dupc = 1;
    char newname[80];


    // name for C code
    val = Info_ValueForKey(cl->userinfo, "name");

    // trim user name
    qstrncpy(newname, val, sizeof(newname));

    for (p = newname; (*p == ' ' || *p == '\r' || *p == '\n') && *p; p++);

    if (p != newname && !*p) {
	//white space only
	strcpy(newname, "unnamed");
	p = newname;
    }

    if (p != newname && *p) {
	for (q = newname; *p; *q++ = *p++);
	*q = 0;
    }
    for (p = newname + strlen(newname) - 1;
	 p != newname && (*p == ' ' || *p == '\r' || *p == '\n'); p--);
    p[1] = 0;

    if (strcmp(val, newname)) {
	Info_SetValueForKey(cl->userinfo, "name", newname, MAX_INFO_STRING);
	val = Info_ValueForKey(cl->userinfo, "name");
    }

    if (!val[0] || !strcasecmp(val, "console")) {
	Info_SetValueForKey(cl->userinfo, "name", "unnamed", MAX_INFO_STRING);
	val = Info_ValueForKey(cl->userinfo, "name");
    }
    // check to see if another user by the same name exists
    while (1) {
	for (i = 0, client = svs.clients; i < MAX_CLIENTS; i++, client++) {
	    if (client->state != cs_spawned || client == cl)
		continue;
	    if (!strcasecmp(client->name, val))
		break;
	}
	if (i != MAX_CLIENTS) {	// dup name
	    if (strlen(val) > sizeof(cl->name) - 1)
		val[sizeof(cl->name) - 4] = 0;
	    p = val;

	    if (val[0] == '(') {
		if (val[2] == ')')
		    p = val + 3;
		else if (val[3] == ')')
		    p = val + 4;
	    }

	    qsnprintf(newname, sizeof(newname), "(%d)%-.40s", dupc++, p);
	    Info_SetValueForKey(cl->userinfo, "name", newname,
				MAX_INFO_STRING);
	    val = Info_ValueForKey(cl->userinfo, "name");
	} else
	    break;
    }

    if (strncmp(val, cl->name, strlen(cl->name))) {
	if (!sv.paused) {
	    if (!cl->lastnametime || realtime - cl->lastnametime > 5) {
		cl->lastnamecount = 0;
		cl->lastnametime = realtime;
	    } else if (cl->lastnamecount++ > 4) {
		SV_BroadcastPrintf(PRINT_HIGH,
				   "%s was kicked for name spam\n", cl->name);
		SV_ClientPrintf(cl, PRINT_HIGH,
				"You were kicked from the game for name spamming\n");
		SV_DropClient(cl);
		return;
	    }
	}

	if (cl->state >= cs_spawned && !cl->spectator)
	    SV_BroadcastPrintf(PRINT_HIGH, "%s changed name to %s\n",
			       cl->name, val);
    }


    qstrncpy(cl->name, val, sizeof(cl->name));

    // rate command
    val = Info_ValueForKey(cl->userinfo, "rate");
    if (strlen(val)) {
	i = atoi(val);
	if (i < 500)
	    i = 500;
	if (i > 10000)
	    i = 10000;
	cl->netchan.rate = 1.0 / i;
    }
    // msg command
    val = Info_ValueForKey(cl->userinfo, "msg");
    if (strlen(val)) {
	cl->messagelevel = atoi(val);
    }

}


//============================================================================

/*
====================
SV_InitNet
====================
*/
static void
SV_InitNet(void)
{
    int port;
    int p;

    port = PORT_SERVER;
    p = COM_CheckParm("-port");
    if (p && p < com_argc) {
	port = atoi(com_argv[p + 1]);
	Con_Printf("Port: %i\n", port);
    }
    NET_Init(port);

    Netchan_Init();

    // heartbeats will always be sent to the id master
    svs.last_heartbeat = -99999;	// send immediately
//      NET_StringToAdr ("192.246.40.70:27000", &idmaster_adr);
}

/*
 * Model Loader Functions
 */

static alias_loader_t SV_AliasLoader = {
    NULL, /* Does not load alias models */
};

static void
SV_BrushModelLoadLighting(brushmodel_t *brushmodel, dheader_t *header)
{
    brushmodel->lightdata = Mod_LoadBytes(brushmodel, header, LUMP_LIGHTING);
}
static int SV_BrushModelPadding() { return 0; }
static void SV_BrushModelPostProcess(brushmodel_t *brushmodel) {}
static brush_loader_t SV_BrushLoader = {
    .Padding = SV_BrushModelPadding,
    .LoadLighting = SV_BrushModelLoadLighting,
    .PostProcess = SV_BrushModelPostProcess,
    .lightmap_sample_bytes = 1,
};

static texture_t qwsv_notexture_mip;
texture_t *r_notexture_mip = &qwsv_notexture_mip;

//============================================================================

static void
Commands_Init()
{
    Cmd_AddCommands();
    COM_AddCommands();
    Mod_AddCommands();
}

static void
Cvars_Init()
{
    COM_RegisterVariables();
}

/*
====================
SV_Init
====================
*/
void
SV_Init(quakeparms_t *parms)
{
    COM_InitArgv(parms->argc, parms->argv);
    COM_AddParm("-game");
    COM_AddParm("qw");

    if (COM_CheckParm("-minmemory"))
	parms->memsize = MINIMUM_MEMORY;

    host_parms = *parms;

    if (parms->memsize < MINIMUM_MEMORY)
	SV_Error("Only %4.1f megs of memory reported, can't execute game",
		 parms->memsize / (float)0x100000);

    Memory_Init(parms->membase, parms->memsize);
    Cvars_Init();
    Commands_Init();

    Cbuf_Init();
    COM_Init();

    PR_Init();
    Mod_Init(&SV_AliasLoader, &SV_BrushLoader);

    SV_InitNet();

    SV_InitLocal();
    Sys_Init();

    Hunk_AllocName(0, "-HOST_HUNKLEVEL-");
    host_hunklevel = Hunk_LowMark();

    host_initialized = true;

    Con_Printf("Exe: %s\n", Build_DateString());
    Con_Printf("%4.1f megabyte heap\n", parms->memsize / (1024 * 1024.0));
    Con_Printf("\nServer Version TyrQuake-%s\n\n", build_version);
    Con_Printf("======== QuakeWorld Initialized ========\n");

    /*
     * Process command line arguments, then exec server.cfg
     */
    Cbuf_InsertText("stuffcmds\nexec server.cfg\n");
    Cbuf_Execute();

// if a map wasn't specified on the command line, spawn start.map
    if (sv.state == ss_dead)
	Cmd_ExecuteString("map start");
    if (sv.state == ss_dead)
	SV_Error("Couldn't spawn a server");
}
