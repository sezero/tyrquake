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

#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H

#include "bothdefs.h"
#include "model.h"
#include "net.h"
#include "progs.h"

// server.h

#define	QW_SERVER

#define	MAX_MASTERS	8	// max recipients for heartbeat packets

#define	MAX_SIGNON_BUFFERS	8

typedef enum {
    ss_dead,			// no map loaded
    ss_loading,			// spawning level edicts
    ss_active			// actively running
} server_state_t;

// some qc commands are only valid before the server has finished
// initializing (precache commands, static sounds / objects, etc)

typedef struct {
    qboolean active;		// false when server is going down
    server_state_t state;	// precache commands are only valid during load

    double time;

    int lastcheck;		// used by PF_checkclient
    double lastchecktime;	// for monster ai
    const mleaf_t *checkleaf;

    qboolean paused;		// are we paused?

    //check player/eyes models for hacks
    unsigned model_player_checksum;
    unsigned eyes_player_checksum;

    char name[64];		// map name
    char modelname[MAX_QPATH];	// maps/<name>.bsp, for model_precache[0]
    brushmodel_t *worldmodel;
    const char *model_precache[MAX_MODELS];	// NULL terminated
    const char *sound_precache[MAX_SOUNDS];	// NULL terminated
    const char *lightstyles[MAX_LIGHTSTYLES];
    model_t *models[MAX_MODELS];

    int num_edicts;		// increases towards MAX_EDICTS
    edict_t *edicts;		// can NOT be array indexed, because
    // edict_t is variable sized, but can
    // be used to reference the world ent

    leafbits_t **pvs, **phs;	// fully expanded and decompressed

    // added to every client's unreliable buffer each frame, then cleared
    sizebuf_t datagram;
    byte datagram_buf[MAX_DATAGRAM];

    // added to every client's reliable buffer each frame, then cleared
    sizebuf_t reliable_datagram;
    byte reliable_datagram_buf[MAX_MSGLEN];

    // the multicast buffer is used to send a message to a set of clients
    sizebuf_t multicast;
    byte multicast_buf[MAX_MSGLEN];

    // the master buffer is used for building log packets
    sizebuf_t master;
    byte master_buf[MAX_DATAGRAM];

    // the signon buffer will be sent to each client as they connect
    // includes the entity baselines, the static entities, etc
    // large levels will have >MAX_DATAGRAM sized signons, so
    // multiple signon messages are kept
    sizebuf_t signon;
    int num_signon_buffers;
    int signon_buffer_size[MAX_SIGNON_BUFFERS];
    byte signon_buffers[MAX_SIGNON_BUFFERS][MAX_DATAGRAM];
} server_t;


#define	NUM_SPAWN_PARMS			16

typedef enum {
    cs_free,			// can be reused for a new connection
    cs_zombie,			// client has been disconnected, but don't reuse
    // connection for a couple seconds
    cs_connected,		// has been assigned to a client_t, but not in game yet
    cs_spawned			// client is fully in game
} client_state_t;

typedef struct {
    // received from client

    // reply
    double senttime;
    float ping_time;
    packet_entities_t entities;
} client_frame_t;

#define MAX_BACK_BUFFERS 4

typedef struct client_s {
    client_state_t state;

    int spectator;		// non-interactive

    qboolean sendinfo;		// at end of frame, send info to all
    // this prevents malicious multiple broadcasts
    float lastnametime;		// time of last name change
    int lastnamecount;		// time of last name change
    unsigned checksum;		// checksum for calcs
    qboolean drop;		// lose this guy next opportunity
    int lossage;		// loss percentage

    int userid;			// identifying number
    char userinfo[MAX_INFO_STRING];	// infostring

    usercmd_t lastcmd;		// for filling in big drops and partial predictions
    double localtime;		// of last message
    int oldbuttons;

    float maxspeed;		// localized maxspeed
    float entgravity;		// localized ent gravity

    edict_t *edict;		// EDICT_NUM(clientnum+1)
    char name[32];		// for printing to other people
    // extracted from userinfo
    int messagelevel;		// for filtering printed messages

    // the datagram is written to after every frame, but only cleared
    // when it is sent out to the client.  overflow is tolerated.
    sizebuf_t datagram;
    byte datagram_buf[MAX_DATAGRAM];

    // back buffers for client reliable data
    sizebuf_t backbuf;
    int num_backbuf;
    int backbuf_size[MAX_BACK_BUFFERS];
    byte backbuf_data[MAX_BACK_BUFFERS][MAX_MSGLEN];

    double connection_started;	// or time of disconnect for zombies
    qboolean send_message;	// set on frames a datagram arived on

// spawn parms are carried from level to level
    float spawn_parms[NUM_SPAWN_PARMS];

// client known data for deltas
    int old_frags;

    int stats[MAX_CL_STATS];


    client_frame_t frames[UPDATE_BACKUP];	// updates can be deltad from here

    FILE *download;		// file being downloaded
    int downloadsize;		// total bytes
    int downloadcount;		// bytes sent

    int spec_track;		// entnum of player tracking

    double whensaid[10];	// JACK: For floodprots
    int whensaidhead;		// Head value for floodprots
    double lockedtill;

    qboolean upgradewarn;	// did we warn him?

    FILE *upload;
    char uploadfn[MAX_QPATH];
    netadr_t snap_from;
    qboolean remote_snap;

//===== NETWORK ============
    int chokecount;
    int delta_sequence;		// -1 = no compression
    netchan_t netchan;
} client_t;

// a client can leave the server in one of four ways:
// dropping properly by quiting or disconnecting
// timing out if no valid messages are received for timeout.value seconds
// getting kicked off by the server operator
// a program error, like an overflowed reliable buffer

//=============================================================================


#define	STATFRAMES	100
typedef struct {
    double active;
    double idle;
    int count;
    int packets;

    double latched_active;
    double latched_idle;
    int latched_packets;
} svstats_t;

// MAX_CHALLENGES is made large to prevent a denial
// of service attack that could cycle all of them
// out before legitimate users connected
#define	MAX_CHALLENGES	1024

typedef struct {
    netadr_t adr;
    int challenge;
    int time;
} challenge_t;

typedef struct {
    int spawncount;		// number of servers spawned since start,
    // used to check late spawns
    client_t clients[MAX_CLIENTS];
    int serverflags;		// episode completion information

    double last_heartbeat;
    int heartbeat_sequence;
    svstats_t stats;

    char info[MAX_SERVERINFO_STRING];

    // log messages are used so that fraglog processes can get stats
    int logsequence;		// the message currently being filled
    double logtime;		// time of last swap
    sizebuf_t log[2];
    byte log_buf[2][MAX_DATAGRAM];

    challenge_t challenges[MAX_CHALLENGES];	// to prevent invalid IPs from connecting
    char next_gamedir[MAX_QPATH];               // set if the gamedir was changed via SV_Gamedir_f ("gamedir" command)
} server_static_t;

//=============================================================================

// edict->movetype values
#define	MOVETYPE_NONE			0	// never moves
#define	MOVETYPE_ANGLENOCLIP	1
#define	MOVETYPE_ANGLECLIP		2
#define	MOVETYPE_WALK			3	// gravity
#define	MOVETYPE_STEP			4	// gravity, special edge handling
#define	MOVETYPE_FLY			5
#define	MOVETYPE_TOSS			6	// gravity
#define	MOVETYPE_PUSH			7	// no clip to world, push and crush
#define	MOVETYPE_NOCLIP			8
#define	MOVETYPE_FLYMISSILE		9	// extra size to monsters
#define	MOVETYPE_BOUNCE			10

// edict->solid values
#define	SOLID_NOT				0	// no interaction with other objects
#define	SOLID_TRIGGER			1	// touch on edge, but not blocking
#define	SOLID_BBOX				2	// touch on edge, block
#define	SOLID_SLIDEBOX			3	// touch on edge, but not an onground
#define	SOLID_BSP				4	// bsp clip, touch on edge, block

// edict->deadflag values
#define	DEAD_NO					0
#define	DEAD_DYING				1
#define	DEAD_DEAD				2

#define	DAMAGE_NO				0
#define	DAMAGE_YES				1
#define	DAMAGE_AIM				2

// edict->flags
#define	FL_FLY					1
#define	FL_SWIM					2
#define	FL_GLIMPSE				4
#define	FL_CLIENT				8
#define	FL_INWATER				16
#define	FL_MONSTER				32
#define	FL_GODMODE				64
#define	FL_NOTARGET				128
#define	FL_ITEM					256
#define	FL_ONGROUND				512
#define	FL_PARTIALGROUND		1024	// not all corners are valid
#define	FL_WATERJUMP			2048	// player jumping out of water

// entity effects

//define        EF_BRIGHTFIELD                  1
//define        EF_MUZZLEFLASH                  2
#define	EF_BRIGHTLIGHT 			4
#define	EF_DIMLIGHT 			8


#define	SPAWNFLAG_NOT_EASY			256
#define	SPAWNFLAG_NOT_MEDIUM		512
#define	SPAWNFLAG_NOT_HARD			1024
#define	SPAWNFLAG_NOT_DEATHMATCH	2048

#define	MULTICAST_ALL			0
#define	MULTICAST_PHS			1
#define	MULTICAST_PVS			2

#define	MULTICAST_ALL_R			3
#define	MULTICAST_PHS_R			4
#define	MULTICAST_PVS_R			5

//============================================================================

extern cvar_t sv_maxvelocity;
extern cvar_t sv_gravity;
extern cvar_t sv_aim;
extern cvar_t sv_stopspeed;
extern cvar_t sv_spectatormaxspeed;
extern cvar_t sv_accelerate;
extern cvar_t sv_airaccelerate;
extern cvar_t sv_wateraccelerate;
extern cvar_t sv_friction;
extern cvar_t sv_waterfriction;

extern cvar_t sv_mintic, sv_maxtic;
extern cvar_t sv_maxspeed;

extern netadr_t master_adr[MAX_MASTERS];	// address of the master server

extern cvar_t spawn;
extern cvar_t teamplay;
extern cvar_t deathmatch;
extern cvar_t fraglimit;
extern cvar_t timelimit;
extern cvar_t pausable;

extern cvar_t allow_download;
extern cvar_t allow_download_skins;
extern cvar_t allow_download_models;
extern cvar_t allow_download_sounds;
extern cvar_t allow_download_maps;

extern cvar_t sv_highchars;
extern cvar_t sv_phs;

extern server_static_t svs;	// persistant server info
extern server_t sv;		// local server

extern char localinfo[MAX_LOCALINFO_STRING + 1];

extern int host_hunklevel;
extern FILE *sv_logfile;
extern FILE *sv_fraglogfile;

extern int sv_nailmodel;
extern int sv_supernailmodel;
extern int sv_playermodel;

extern int fp_messages, fp_persecond, fp_secondsdead;
extern char fp_msg[];

//===========================================================

//
// sv_main.c
//
void SV_Shutdown(void);
void SV_Frame(float time);
void SV_FinalMessage(const char *message);
void SV_DropClient(client_t *drop);
void SV_FullClientUpdateToClient(client_t *client, client_t *cl);

int SV_CalcPing(client_t *cl);
void SV_FullClientUpdate(client_t *client, sizebuf_t *buf);

int SV_ModelIndex(const char *name);

qboolean SV_CheckBottom(edict_t *ent);
qboolean SV_movestep(edict_t *ent, vec3_t move, qboolean relink);

void SV_WriteClientdataToMessage(client_t *client, sizebuf_t *msg);
void SV_MoveToGoal(void);
void SV_AddOperatorCommands(void);
void SV_SendServerinfo(client_t *client);
void SV_SendServerInfoChange(const char *key, const char *value);
void SV_ExtractFromUserinfo(client_t *cl);

//
// sv_init.c
//
void SV_ModelInit(void);
void SV_SpawnServer(const char *server);
void SV_FlushSignon(void);


//
// sv_phys.c
//
qboolean SV_RunThink(edict_t *ent);
void SV_ProgStartFrame(void);
void SV_Physics(void);
void SV_RunNewmis(void);
void SV_SetMoveVars(void);

//
// svonly.c
//
typedef enum { RD_NONE, RD_CLIENT, RD_PACKET } redirect_t;
void SV_BeginRedirect(redirect_t rd, client_t *client);
void SV_EndRedirect(void);

//
// sv_send.c
//
extern redirect_t sv_redirected;

void SV_SendClientMessages(void);

void SV_Multicast(vec3_t origin, int to);
void SV_StartSound(edict_t *entity, int channel, const char *sample,
		   int volume, float attenuation);
void SV_ClientPrintf(client_t *cl, int level, const char *fmt, ...)
    __attribute__((format(printf,3,4)));
void SV_BroadcastPrintf(int level, const char *fmt, ...)
    __attribute__((format(printf,2,3)));
void SV_BroadcastCommand(const char *fmt, ...)
    __attribute__((format(printf,1,2)));
void SV_SendMessagesToAll(void);
void SV_FindModelNumbers(void);

//
// sv_user.c
//
void SV_ExecuteClientMessage(client_t *cl);
void SV_UserInit(void);
void SV_TogglePause(const char *msg);

//
// sv_ents.c
//
void SV_WriteEntitiesToClient(client_t *client, sizebuf_t *msg);

//
// sv_nchan.c
//

void ClientReliableCheckBlock(client_t *cl, int maxsize);
void ClientReliable_FinishWrite(client_t *cl);
void ClientReliableWrite_Begin(client_t *cl, int c, int maxsize);
void ClientReliableWrite_Angle(client_t *cl, float f);
void ClientReliableWrite_Angle16(client_t *cl, float f);
void ClientReliableWrite_Byte(client_t *cl, int c);
void ClientReliableWrite_Char(client_t *cl, int c);
void ClientReliableWrite_Float(client_t *cl, float f);
void ClientReliableWrite_Coord(client_t *cl, float f);
void ClientReliableWrite_Long(client_t *cl, int c);
void ClientReliableWrite_Short(client_t *cl, int c);
void ClientReliableWrite_String(client_t *cl, const char *s);
void ClientReliableWrite_SZ(client_t *cl, void *data, int len);

#endif /* SERVER_SERVER_H */
