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
// sv_user.c -- server code for moving users

#include "client.h"
#include "cmd.h"
#include "console.h"
#include "host.h"
#include "keys.h"
#include "net.h"
#include "progs.h"
#include "protocol.h"
#include "quakedef.h"
#include "server.h"
#include "sys.h"
#include "view.h"
#include "world.h"

edict_t *sv_player;

cvar_t sv_idealpitchscale = { "sv_idealpitchscale", "0.8" };
cvar_t sv_edgefriction = { "edgefriction", "2" };

/*
===============
SV_SetIdealPitch
===============
*/
#define	MAX_FORWARD	6
void
SV_SetIdealPitch(void)
{
    float angleval, sinval, cosval;
    trace_t trace;
    vec3_t top, bottom;
    float z[MAX_FORWARD];
    int i, j;
    int step, dir, steps;

    if (!((int)sv_player->v.flags & FL_ONGROUND))
	return;

    angleval = sv_player->v.angles[YAW] * M_PI * 2 / 360;
    sinval = sin(angleval);
    cosval = cos(angleval);

    for (i = 0; i < MAX_FORWARD; i++) {
	top[0] = sv_player->v.origin[0] + cosval * (i + 3) * 12;
	top[1] = sv_player->v.origin[1] + sinval * (i + 3) * 12;
	top[2] = sv_player->v.origin[2] + sv_player->v.view_ofs[2];

	bottom[0] = top[0];
	bottom[1] = top[1];
	bottom[2] = top[2] - 160;

	SV_TraceLine(top, bottom, MOVE_NOMONSTERS, sv_player, &trace);
	if (trace.allsolid)
	    return;		// looking at a wall, leave ideal the way is was

	if (trace.fraction == 1)
	    return;		// near a dropoff

	z[i] = top[2] + trace.fraction * (bottom[2] - top[2]);
    }

    dir = 0;
    steps = 0;
    for (j = 1; j < i; j++) {
	step = z[j] - z[j - 1];
	if (step > -ON_EPSILON && step < ON_EPSILON)
	    continue;

	if (dir && (step - dir > ON_EPSILON || step - dir < -ON_EPSILON))
	    return;		// mixed changes

	steps++;
	dir = step;
    }

    if (!dir) {
	sv_player->v.idealpitch = 0;
	return;
    }

    if (steps < 2)
	return;
    sv_player->v.idealpitch = -dir * sv_idealpitchscale.value;
}


/*
==================
SV_UserFriction

==================
*/
static void
SV_UserFriction(edict_t *player)
{
    const vec_t *velocity = player->v.velocity;
    const vec_t *origin = player->v.origin;
    vec_t speed, newspeed, control, friction;
    vec3_t start, stop;
    trace_t trace;

    speed = sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    if (!speed)
	return;

    /* if the leading edge is over a dropoff, increase friction */
    start[0] = stop[0] = origin[0] + velocity[0] / speed * 16;
    start[1] = stop[1] = origin[1] + velocity[1] / speed * 16;
    start[2] = origin[2] + player->v.mins[2];
    stop[2] = start[2] - 34;

    SV_TraceLine(start, stop, MOVE_NOMONSTERS, player, &trace);
    if (trace.fraction == 1.0)
	friction = sv_friction.value * sv_edgefriction.value;
    else
	friction = sv_friction.value;

    /* apply friction */
    control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
    newspeed = speed - host_frametime * control * friction;

    if (newspeed < 0)
	newspeed = 0;
    newspeed /= speed;

    VectorScale(velocity, newspeed, player->v.velocity);
}

/*
==============
SV_Accelerate
==============
*/
cvar_t sv_maxspeed = { "sv_maxspeed", "320", false, true };
cvar_t sv_accelerate = { "sv_accelerate", "10" };

static void
SV_Accelerate(const vec3_t wishdir, const vec_t wishspeed, vec3_t velocity)
{
    int i;
    float addspeed, accelspeed, currentspeed;

    currentspeed = DotProduct(velocity, wishdir);
    addspeed = wishspeed - currentspeed;
    if (addspeed <= 0)
	return;
    accelspeed = sv_accelerate.value * host_frametime * wishspeed;
    if (accelspeed > addspeed)
	accelspeed = addspeed;

    for (i = 0; i < 3; i++)
	velocity[i] += accelspeed * wishdir[i];
}

static void
SV_AirAccelerate(vec3_t wishveloc, const vec_t wishspeed, vec3_t velocity)
{
    int i;
    float addspeed, wishspd, accelspeed, currentspeed;

    wishspd = VectorNormalize(wishveloc);
    if (wishspd > 30)
	wishspd = 30;
    currentspeed = DotProduct(velocity, wishveloc);
    addspeed = wishspd - currentspeed;
    if (addspeed <= 0)
	return;
    accelspeed = sv_accelerate.value * wishspeed * host_frametime;
    if (accelspeed > addspeed)
	accelspeed = addspeed;

    for (i = 0; i < 3; i++)
	velocity[i] += accelspeed * wishveloc[i];
}


static void
DropPunchAngle(vec3_t punchangle)
{
    vec_t length;

    length = VectorNormalize(punchangle);
    length -= 10 * host_frametime;
    if (length < 0)
	length = 0;
    VectorScale(punchangle, length, punchangle);
}

/*
===================
SV_WaterMove

===================
*/
static void
SV_WaterMove(const usercmd_t *cmd, edict_t *player)
{
    int i;
    vec3_t wishvel, forward, right, up;
    float speed, newspeed, wishspeed, addspeed, accelspeed;

    /* user intentions */
    AngleVectors(player->v.v_angle, forward, right, up);
    for (i = 0; i < 3; i++)
	wishvel[i] = forward[i] * cmd->forwardmove + right[i] * cmd->sidemove;

    if (!cmd->forwardmove && !cmd->sidemove && !cmd->upmove)
	wishvel[2] -= 60;	/* drift towards bottom */
    else
	wishvel[2] += cmd->upmove;

    wishspeed = Length(wishvel);
    if (wishspeed > sv_maxspeed.value) {
	VectorScale(wishvel, sv_maxspeed.value / wishspeed, wishvel);
	wishspeed = sv_maxspeed.value;
    }
    wishspeed *= 0.7;

    /* water friction */
    speed = Length(player->v.velocity);
    if (speed) {
	newspeed = speed - host_frametime * speed * sv_friction.value;
	if (newspeed < 0)
	    newspeed = 0;
	VectorScale(player->v.velocity, newspeed / speed, player->v.velocity);
    } else
	newspeed = 0;

    /* water acceleration */
    if (!wishspeed)
	return;

    addspeed = wishspeed - newspeed;
    if (addspeed <= 0)
	return;

    VectorNormalize(wishvel);
    accelspeed = sv_accelerate.value * wishspeed * host_frametime;
    if (accelspeed > addspeed)
	accelspeed = addspeed;

    VectorMA(player->v.velocity, accelspeed, wishvel, player->v.velocity);
}

static void
SV_WaterJump(edict_t *player)
{
    if (sv.time > player->v.teleport_time || !player->v.waterlevel) {
	player->v.flags = (int)player->v.flags & ~FL_WATERJUMP;
	player->v.teleport_time = 0;
    }
    player->v.velocity[0] = player->v.movedir[0];
    player->v.velocity[1] = player->v.movedir[1];
}


/*
===================
SV_AirMove

===================
*/
static void
SV_AirMove(const usercmd_t *cmd, edict_t *player)
{
    qboolean onground = (int)player->v.flags & FL_ONGROUND;

    vec3_t wishvel, wishdir, forward, right, up;
    vec_t wishspeed, fmove, smove;
    int i;

    AngleVectors(player->v.angles, forward, right, up);

    fmove = cmd->forwardmove;
    smove = cmd->sidemove;

    /* hack to not let you back into teleporter */
    if (sv.time < player->v.teleport_time && fmove < 0)
	fmove = 0;

    for (i = 0; i < 3; i++)
	wishvel[i] = forward[i] * fmove + right[i] * smove;

    if ((int)player->v.movetype != MOVETYPE_WALK)
	wishvel[2] = cmd->upmove;
    else
	wishvel[2] = 0;

    VectorCopy(wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir);
    if (wishspeed > sv_maxspeed.value) {
	VectorScale(wishvel, sv_maxspeed.value / wishspeed, wishvel);
	wishspeed = sv_maxspeed.value;
    }

    if (player->v.movetype == MOVETYPE_NOCLIP) {
	VectorCopy(wishvel, player->v.velocity);
    } else if (onground) {
	SV_UserFriction(player);
	SV_Accelerate(wishdir, wishspeed, player->v.velocity);
    } else {
	/* not on ground, so little effect on velocity */
	SV_AirAccelerate(wishvel, wishspeed, player->v.velocity);
    }
}

/*
===================
SV_ClientThink

the move fields specify an intended velocity in pix/sec
the angle fields specify an exact angular motion in degrees
===================
*/
static void
SV_ClientThink(client_t *client)
{
    edict_t *player = client->edict;
    qboolean onground;
    vec3_t v_angle;

    if (player->v.movetype == MOVETYPE_NONE)
	return;

    onground = (int)player->v.flags & FL_ONGROUND;
    DropPunchAngle(player->v.punchangle);

    /* if dead, behave differently */
    if (player->v.health <= 0)
	return;

    /* angles - show 1/3 the pitch angle and all the roll angle */
    VectorAdd(player->v.v_angle, player->v.punchangle, v_angle);
    player->v.angles[ROLL] = V_CalcRoll(player->v.angles, player->v.velocity) * 4;
    if (!player->v.fixangle) {
	player->v.angles[PITCH] = -v_angle[PITCH] / 3;
	player->v.angles[YAW] = v_angle[YAW];
    }

    if ((int)player->v.flags & FL_WATERJUMP) {
	SV_WaterJump(player);
	return;
    }

    /* walk */
    if (player->v.waterlevel >= 2 && player->v.movetype != MOVETYPE_NOCLIP) {
	SV_WaterMove(&client->cmd, player);
	return;
    }

    SV_AirMove(&client->cmd, player);
}


/*
===================
SV_ReadClientMove
===================
*/
static void
SV_ReadClientMove(client_t *client, usercmd_t *move)
{
    edict_t *player = client->edict;
    int i, ping, buttonbits, impulse;

    /* read ping time */
    ping = client->num_pings % NUM_PING_TIMES;
    client->ping_times[ping] = sv.time - MSG_ReadFloat();
    client->num_pings++;

    /* read current angles */
    for (i = 0; i < 3; i++) {
	if (sv.protocol == PROTOCOL_VERSION_FITZ)
	    player->v.v_angle[i] = MSG_ReadAngle16();
	else
	    player->v.v_angle[i] = MSG_ReadAngle();
    }

    /* read movement */
    move->forwardmove = MSG_ReadShort();
    move->sidemove = MSG_ReadShort();
    move->upmove = MSG_ReadShort();

    /* read buttons */
    buttonbits = MSG_ReadByte();
    player->v.button0 = buttonbits & 1;
    player->v.button2 = (buttonbits & 2) >> 1;

    impulse = MSG_ReadByte();
    if (impulse)
	player->v.impulse = impulse;
}

/*
 * ------------------------------------------------------------------------
 * CLIENT COMMANDS
 * ------------------------------------------------------------------------
 */

/*
======================
SV_Name_f
======================
*/
static void
SV_Name_f(client_t *client)
{
    char new_name[16];
    const char *arg;

    if (Cmd_Argc() == 1)
	return;

    /* See if the name has changed */
    arg = (Cmd_Argc() == 2) ? Cmd_Argv(1) : Cmd_Args();
    snprintf(new_name, sizeof(new_name), "%s", arg);
    if (!strcmp(client->name, new_name))
	return;

    if (client->name[0] && strcmp(client->name, "unconnected"))
	Con_Printf("%s renamed to %s\n", client->name, new_name);
    strcpy(client->name, new_name);
    client->edict->v.netname = PR_SetString(client->name);

    /* send notification to all clients */
    MSG_WriteByte(&sv.reliable_datagram, svc_updatename);
    MSG_WriteByte(&sv.reliable_datagram, client - svs.clients);
    MSG_WriteString(&sv.reliable_datagram, client->name);
}

/*
==================
SV_Status_f
==================
*/
static void
SV_Status_f(client_t *client)
{
    client_t *other;
    int seconds;
    int minutes;
    int hours = 0;
    int i;

    /* FIXME - pass into client printf */
    host_client = client;

    SV_ClientPrintf("host:    %s\n", Cvar_VariableString("hostname"));
    SV_ClientPrintf("version: TyrQuake-%s\n", stringify(TYR_VERSION));
    if (tcpipAvailable)
	SV_ClientPrintf("tcp/ip:  %s\n", my_tcpip_address);
    SV_ClientPrintf("map:     %s\n", sv.name);
    SV_ClientPrintf("players: %i active (%i max)\n\n", net_activeconnections,
		    svs.maxclients);

    other = svs.clients;
    for (i = 0; i < svs.maxclients; i++, other++) {
	if (!other->active)
	    continue;

	seconds = (int)(net_time - other->netconnection->connecttime);
	minutes = seconds / 60;
	seconds -= (minutes * 60);
	hours = minutes / 60;
	minutes -= (hours * 60);
	SV_ClientPrintf("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n",
			i + 1, client->name, (int)client->edict->v.frags,
			hours, minutes, seconds);
	SV_ClientPrintf("   %s\n", client->netconnection->address);
    }
}

/*
==================
SV_God_f

Sets client to godmode
==================
*/
static void
SV_God_f(client_t *client)
{
    edict_t *player;

    if (pr_global_struct->deathmatch)
	return;

    player = client->edict;
    player->v.flags = (int)player->v.flags ^ FL_GODMODE;
    if (!((int)player->v.flags & FL_GODMODE))
	SV_ClientPrintf("godmode OFF\n");
    else
	SV_ClientPrintf("godmode ON\n");
}

/*
==================
SV_Fly_f

Sets client to flymode
==================
*/
static void
SV_Fly_f(client_t *client)
{
    edict_t *player;

    if (pr_global_struct->deathmatch)
	return;

    player = client->edict;
    if (player->v.movetype != MOVETYPE_FLY) {
	player->v.movetype = MOVETYPE_FLY;
	SV_ClientPrintf("flymode ON\n");
    } else {
	player->v.movetype = MOVETYPE_WALK;
	SV_ClientPrintf("flymode OFF\n");
    }
}

/*
==================
SV_Noclip_f

Sets client to noclip mode
==================
*/
static void
SV_Noclip_f(client_t *client)
{
    edict_t *player;

    if (pr_global_struct->deathmatch)
	return;

    player = client->edict;
    if (player->v.movetype != MOVETYPE_NOCLIP) {
	noclip_anglehack = true;
	player->v.movetype = MOVETYPE_NOCLIP;
	SV_ClientPrintf("noclip ON\n");
    } else {
	noclip_anglehack = false;
	player->v.movetype = MOVETYPE_WALK;
	SV_ClientPrintf("noclip OFF\n");
    }
}

/*
==================
SV_Notarget_f

Sets client to notarget mode
==================
*/
static void
SV_Notarget_f(client_t *client)
{
    edict_t *player;

    if (pr_global_struct->deathmatch)
	return;

    player = client->edict;
    player->v.flags = (int)player->v.flags ^ FL_NOTARGET;
    if (!((int)player->v.flags & FL_NOTARGET))
	SV_ClientPrintf("notarget OFF\n");
    else
	SV_ClientPrintf("notarget ON\n");
}

typedef struct {
    const char *name;
    void (*func)(client_t *client);
} client_command_t;

static client_command_t client_commands[] = {
    { "name", SV_Name_f },
    { "status", SV_Status_f },
    { "god", SV_God_f },
    { "fly", SV_Fly_f },
    { "noclip", SV_Noclip_f },
    { "notarget", SV_Notarget_f },
    { NULL, NULL },
};

/* FIXME - Won't need to return result after all commands have been
   properly converted. Can enable error message then too. */
static qboolean
SV_ExecuteClientCommand(const char *command_string, client_t *client)
{
    client_command_t *command;

    // TODO: begin/end redirect like QW?
    Cmd_TokenizeString(command_string);
    for (command = client_commands; command->name; command++) {
	if (!strcmp(Cmd_Argv(0), command->name)) {
	    command->func(client);
	    return true;
	}
    }
    //Con_Printf("Bad client command: %s\n", Cmd_Argv(0));
    return false;
}

/*
===================
SV_ReadClientMessage

Returns false if the client should be killed
===================
*/
static qboolean
SV_ReadClientMessage(client_t *client)
{
    const char *message;
    int ret, cmd;
    qboolean found;

    do {
      nextmsg:
	ret = NET_GetMessage(client->netconnection);
	if (ret == -1) {
	    Sys_Printf("%s: NET_GetMessage failed\n", __func__);
	    return false;
	}
	if (!ret)
	    return true;

	MSG_BeginReading();

	while (1) {
	    if (!client->active)
		return false;	// a command caused an error

	    if (msg_badread) {
		Sys_Printf("%s: badread\n", __func__);
		return false;
	    }

	    cmd = MSG_ReadChar();

	    switch (cmd) {
	    case -1:
		goto nextmsg;	// end of message

	    default:
		Sys_Printf("%s: unknown command char\n", __func__);
		return false;

	    case clc_nop:
		//Sys_Printf ("clc_nop\n");
		break;

	    case clc_stringcmd:
		message = MSG_ReadString();
		found = SV_ExecuteClientCommand(message, client);
		if (found)
		    break;

		ret = 0;
		if (strncasecmp(message, "say", 3) == 0)
		    ret = 1;
		else if (strncasecmp(message, "say_team", 8) == 0)
		    ret = 1;
		else if (strncasecmp(message, "tell", 4) == 0)
		    ret = 1;
		else if (strncasecmp(message, "color", 5) == 0)
		    ret = 1;
		else if (strncasecmp(message, "kill", 4) == 0)
		    ret = 1;
		else if (strncasecmp(message, "pause", 5) == 0)
		    ret = 1;
		else if (strncasecmp(message, "spawn", 5) == 0)
		    ret = 1;
		else if (strncasecmp(message, "begin", 5) == 0)
		    ret = 1;
		else if (strncasecmp(message, "prespawn", 8) == 0)
		    ret = 1;
		else if (strncasecmp(message, "kick", 4) == 0)
		    ret = 1;
		else if (strncasecmp(message, "ping", 4) == 0)
		    ret = 1;
		else if (strncasecmp(message, "give", 4) == 0)
		    ret = 1;
		else if (strncasecmp(message, "ban", 3) == 0)
		    ret = 1;

		if (ret == 1)
		    Cmd_ExecuteString(message, src_client);
		else
		    Con_DPrintf("%s tried to %s\n", client->name, message);
		break;

	    case clc_disconnect:
		//Sys_Printf ("%s: client disconnected\n", __func__);
		return false;

	    case clc_move:
		SV_ReadClientMove(client, &client->cmd);
		break;
	    }
	}
    } while (ret == 1);

    return true;
}


/*
==================
SV_RunClients
==================
*/
void
SV_RunClients(void)
{
    client_t *client;
    int i;

    for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++) {

	/* FIXME - remove host_client later */
	host_client = client;

	if (!client->active)
	    continue;

	/* FIXME - remove sv_player later... */
	sv_player = client->edict;

	if (!SV_ReadClientMessage(client)) {
	    /* client misbehaved... */
	    SV_DropClient(false);
	    continue;
	}

	if (!client->spawned) {
	    /* clear client movement until a new packet is received */
	    memset(&client->cmd, 0, sizeof(client->cmd));
	    continue;
	}

	/* always pause in single player if in console or menus */
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
	    SV_ClientThink(client);
    }
}
