/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the included (GNU.txt) GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
/* ZOID
 *
 * Player camera tracking in Spectator mode
 *
 * This takes over player controls for spectator automatic camera.
 * Player moves as a spectator, but the camera tracks and enemy player
 */

#include "client.h"
#include "console.h"
#include "mathlib.h"
#include "pmove.h"
#include "quakedef.h"
#include "sbar.h"

#define	PM_SPECTATORMAXSPEED	500
#define	PM_STOPSPEED		100
#define	PM_MAXSPEED		320
#define BUTTON_JUMP 		  2
#define BUTTON_ATTACK		  1
#define MAX_ANGLE_TURN		 10

static vec3_t desired_position;	// where the camera wants to be
static qboolean locked = false;
static int oldbuttons;

// track high fragger
static cvar_t cl_hightrack = { "cl_hightrack", "0" };
static cvar_t cl_chasecam = { "cl_chasecam", "0" };

//cvar_t cl_camera_maxpitch = {"cl_camera_maxpitch", "10" };
//cvar_t cl_camera_maxyaw = {"cl_camera_maxyaw", "30" };

static qboolean cam_forceview;
//static vec3_t cam_viewangles;
static double cam_lastviewtime;

int spec_track = 0;		// player# of who we are tracking
int autocam = CAM_NONE;

static void
vectoangles(vec3_t vec, vec3_t ang)
{
    float forward;
    float yaw, pitch;

    if (vec[1] == 0 && vec[0] == 0) {
	yaw = 0;
	if (vec[2] > 0)
	    pitch = 90;
	else
	    pitch = 270;
    } else {
	yaw = (int)(atan2(vec[1], vec[0]) * 180 / M_PI);
	if (yaw < 0)
	    yaw += 360;

	forward = sqrtf(vec[0] * vec[0] + vec[1] * vec[1]);
	pitch = (int)(atan2(vec[2], forward) * 180 / M_PI);
	if (pitch < 0)
	    pitch += 360;
    }

    ang[0] = pitch;
    ang[1] = yaw;
    ang[2] = 0;
}

// returns true if weapon model should be drawn in camera mode
qboolean
Cam_DrawViewModel(void)
{
    if (!cl.spectator)
	return true;

    if (autocam && locked && cl_chasecam.value)
	return true;
    return false;
}

// returns true if we should draw this player, we don't if we are chase camming
qboolean
Cam_DrawPlayer(int playernum)
{
    if (cl.spectator && autocam && locked && cl_chasecam.value &&
	spec_track == playernum)
	return false;
    return true;
}

static void
Cam_Unlock(void)
{
    if (autocam) {
	MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	MSG_WriteString(&cls.netchan.message, "ptrack");
	autocam = CAM_NONE;
	locked = false;
	Sbar_Changed();
    }
}

static void
Cam_Lock(int playernum)
{
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteStringf(&cls.netchan.message, "ptrack %i", playernum);
    spec_track = playernum;
    cam_forceview = true;
    locked = false;
    Sbar_Changed();
}

static inline void
Cam_DoTrace(const vec3_t start, const vec3_t end,
	    const physent_stack_t *pestack, trace_t *trace)
{
    /* Fake a player move */
    PM_PlayerMove(start, end, pestack, trace);
}

// Returns distance or 9999 if invalid for some reason
static float
Cam_TryFlyby(const player_state_t *self, const player_state_t *player,
	     const vec3_t viewvec, const physent_stack_t *pestack,
	     qboolean checkvis)
{
    vec3_t viewnormal, endpos, camvec;
    vec_t dist;
    trace_t trace;

    /* Set up a viewpoint 800 units away and see where it gets clipped */
    VectorCopy(viewvec, viewnormal);
    VectorNormalize(viewnormal);
    VectorMA(player->origin, 800, viewnormal, endpos);

    Cam_DoTrace(player->origin, endpos, pestack, &trace);
    if ( /*trace.inopen || */ trace.inwater)
	return 9999;

    VectorSubtract(trace.endpos, player->origin, camvec);
    dist = Length(camvec);
    if (dist < 32 || dist > 800)
	return 9999;

    if (checkvis) {
	VectorSubtract(trace.endpos, self->origin, endpos);
	Cam_DoTrace(self->origin, endpos, pestack, &trace);
	if (trace.fraction != 1 || trace.inwater)
	    return 9999;
    }

    return dist;
}

// Is player visible?
static qboolean
Cam_IsVisible(const player_state_t *player, const vec3_t viewpoint,
	      const physent_stack_t *pestack)
{
    trace_t trace;
    vec3_t viewvec;
    float dist;

    Cam_DoTrace(player->origin, viewpoint, pestack, &trace);
    if (trace.fraction != 1 || /*trace.inopen || */ trace.inwater)
	return false;

    /* check distance, don't let the player get too far away or too close */
    VectorSubtract(player->origin, viewpoint, viewvec);
    dist = Length(viewvec);
    if (dist < 16)
	return false;

    return true;
}

static qboolean
InitFlyby(const player_state_t *self, const player_state_t *player,
	  const physent_stack_t *pestack, int checkvis)
{
    int i;
    float flydist, max;
    vec3_t vec, vec2;
    vec3_t forward, right, up;

    VectorCopy(player->viewangles, vec);
    vec[0] = 0;
    AngleVectors(vec, forward, right, up);
//      for (i = 0; i < 3; i++)
//              forward[i] *= 3;

    max = 1000;
    for (i = 0; i < 8; i++) {
	VectorAdd(forward, up, vec2);
	VectorAdd(vec2, right, vec2);
	flydist = Cam_TryFlyby(self, player, vec2, pestack, checkvis);
	if (flydist < max) {
	    max = flydist;
	    VectorCopy(vec2, vec);
	}
    }

    /* invert */
    VectorSubtract(vec3_origin, forward, vec2);
    flydist = Cam_TryFlyby(self, player, vec2, pestack, checkvis);
    if (flydist < max) {
	max = flydist;
	VectorCopy(vec2, vec);
    }
    VectorCopy(forward, vec2);
    flydist = Cam_TryFlyby(self, player, vec2, pestack, checkvis);
    if (flydist < max) {
	max = flydist;
	VectorCopy(vec2, vec);
    }

    /* invert */
    VectorSubtract(vec3_origin, right, vec2);
    flydist = Cam_TryFlyby(self, player, vec2, pestack, checkvis);
    if (flydist < max) {
	max = flydist;
	VectorCopy(vec2, vec);
    }
    VectorCopy(right, vec2);
    flydist = Cam_TryFlyby(self, player, vec2, pestack, checkvis);
    if (flydist < max) {
	max = flydist;
	VectorCopy(vec2, vec);
    }

    if (max >= 1000) {
	/* ack, can't find him! */
	return false;
    }
    locked = true;
    VectorCopy(vec, desired_position);

    return true;
}

static void
Cam_CheckHighTarget(void)
{
    int i, j, max;
    player_info_t *s;

    j = -1;
    for (i = 0, max = -9999; i < MAX_CLIENTS; i++) {
	s = &cl.players[i];
	if (s->name[0] && !s->spectator && s->frags > max) {
	    max = s->frags;
	    j = i;
	}
    }
    if (j >= 0) {
	if (!locked || cl.players[j].frags > cl.players[spec_track].frags)
	    Cam_Lock(j);
    } else
	Cam_Unlock();
}

/*
 * Take over the user controls and track a player.
 * We find a nice position to watch the player and move there
 */
void
Cam_Track(usercmd_t *cmd, const physent_stack_t *pestack)
{
    player_state_t *player, *self;
    frame_t *frame;
    vec3_t vec;
    float len;

    if (!cl.spectator)
	return;

    if (cl_hightrack.value && !locked)
	Cam_CheckHighTarget();

    if (!autocam || cls.state != ca_active)
	return;

    if (locked && (!cl.players[spec_track].name[0]
		   || cl.players[spec_track].spectator)) {
	locked = false;
	if (cl_hightrack.value)
	    Cam_CheckHighTarget();
	else
	    Cam_Unlock();
	return;
    }

    frame = &cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK];
    player = frame->playerstate + spec_track;
    self = frame->playerstate + cl.playernum;

    if (!locked || !Cam_IsVisible(player, desired_position, pestack)) {
	if (!locked || realtime - cam_lastviewtime > 0.1) {
	    if (!InitFlyby(self, player, pestack, true))
		InitFlyby(self, player, pestack, false);
	    cam_lastviewtime = realtime;
	}
    } else
	cam_lastviewtime = realtime;

    // couldn't track for some reason
    if (!locked || !autocam)
	return;

    if (cl_chasecam.value) {
	cmd->forwardmove = cmd->sidemove = cmd->upmove = 0;

	VectorCopy(player->viewangles, cl.viewangles);
	VectorCopy(player->origin, desired_position);
	if (memcmp
	    (&desired_position, &self->origin,
	     sizeof(desired_position)) != 0) {
	    MSG_WriteByte(&cls.netchan.message, clc_tmove);
	    MSG_WriteCoord(&cls.netchan.message, desired_position[0]);
	    MSG_WriteCoord(&cls.netchan.message, desired_position[1]);
	    MSG_WriteCoord(&cls.netchan.message, desired_position[2]);
	    // move there locally immediately
	    VectorCopy(desired_position, self->origin);
	}
	self->weaponframe = player->weaponframe;

    } else {
	// Ok, move to our desired position and set our angles to view
	// the player
	VectorSubtract(desired_position, self->origin, vec);
	len = Length(vec);
	cmd->forwardmove = cmd->sidemove = cmd->upmove = 0;
	if (len > 16) {		// close enough?
	    MSG_WriteByte(&cls.netchan.message, clc_tmove);
	    MSG_WriteCoord(&cls.netchan.message, desired_position[0]);
	    MSG_WriteCoord(&cls.netchan.message, desired_position[1]);
	    MSG_WriteCoord(&cls.netchan.message, desired_position[2]);
	}
	// move there locally immediately
	VectorCopy(desired_position, self->origin);

	VectorSubtract(player->origin, desired_position, vec);
	vectoangles(vec, cl.viewangles);
	cl.viewangles[0] = -cl.viewangles[0];
    }
}

#if 0
static float
adjustang(float current, float ideal, float speed)
{
    float move;

    current = anglemod(current);
    ideal = anglemod(ideal);

    if (current == ideal)
	return current;

    move = ideal - current;
    if (ideal > current) {
	if (move >= 180)
	    move = move - 360;
    } else {
	if (move < -180)
	    move = move + 360;
    }
    if (move > 0) {
	if (move > speed)
	    move = speed;
    } else {
	if (move < -speed)
	    move = -speed;
    }

//Con_Printf("c/i: %4.2f/%4.2f move: %4.2f\n", current, ideal, move);
    return anglemod(current + move);
}
#endif

#if 0
static void
Cam_SetView(void)
{
    return;
    player_state_t *player, *self;
    frame_t *frame;
    vec3_t vec, vec2;

    if (cls.state != ca_active || !cl.spectator || !autocam || !locked)
	return;

    frame = &cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK];
    player = frame->playerstate + spec_track;
    self = frame->playerstate + cl.playernum;

    VectorSubtract(player->origin, cl.simorg, vec);
    if (cam_forceview) {
	cam_forceview = false;
	vectoangles(vec, cam_viewangles);
	cam_viewangles[0] = -cam_viewangles[0];
    } else {
	vectoangles(vec, vec2);
	vec2[PITCH] = -vec2[PITCH];

	cam_viewangles[PITCH] =
	    adjustang(cam_viewangles[PITCH], vec2[PITCH],
		      cl_camera_maxpitch.value);
	cam_viewangles[YAW] =
	    adjustang(cam_viewangles[YAW], vec2[YAW], cl_camera_maxyaw.value);
    }
    VectorCopy(cam_viewangles, cl.viewangles);
    VectorCopy(cl.viewangles, cl.simangles);
}
#endif

void
Cam_FinishMove(usercmd_t *cmd)
{
    int i;
    player_info_t *s;
    int end;

    if (cls.state != ca_active)
	return;

    if (!cl.spectator)		// only in spectator mode
	return;

#if 0
    if (autocam && locked) {
	frame = &cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK];
	player = frame->playerstate + spec_track;
	self = frame->playerstate + cl.playernum;

	VectorSubtract(player->origin, self->origin, vec);
	if (cam_forceview) {
	    cam_forceview = false;
	    vectoangles(vec, cam_viewangles);
	    cam_viewangles[0] = -cam_viewangles[0];
	} else {
	    vectoangles(vec, vec2);
	    vec2[PITCH] = -vec2[PITCH];

	    cam_viewangles[PITCH] =
		adjustang(cam_viewangles[PITCH], vec2[PITCH],
			  cl_camera_maxpitch.value);
	    cam_viewangles[YAW] =
		adjustang(cam_viewangles[YAW], vec2[YAW],
			  cl_camera_maxyaw.value);
	}
	VectorCopy(cam_viewangles, cl.viewangles);
    }
#endif

    if (cmd->buttons & BUTTON_ATTACK) {
	if (!(oldbuttons & BUTTON_ATTACK)) {

	    oldbuttons |= BUTTON_ATTACK;
	    autocam++;

	    if (autocam > CAM_TRACK) {
		Cam_Unlock();
		VectorCopy(cl.viewangles, cmd->angles);
		return;
	    }
	} else
	    return;
    } else {
	oldbuttons &= ~BUTTON_ATTACK;
	if (!autocam)
	    return;
    }

    if (autocam && cl_hightrack.value) {
	Cam_CheckHighTarget();
	return;
    }

    if (locked) {
	if ((cmd->buttons & BUTTON_JUMP) && (oldbuttons & BUTTON_JUMP))
	    return;		// don't pogo stick

	if (!(cmd->buttons & BUTTON_JUMP)) {
	    oldbuttons &= ~BUTTON_JUMP;
	    return;
	}
	oldbuttons |= BUTTON_JUMP;	// don't jump again until released
    }
//      Con_Printf("Selecting track target...\n");

    if (locked && autocam)
	end = (spec_track + 1) % MAX_CLIENTS;
    else
	end = spec_track;
    i = end;
    do {
	s = &cl.players[i];
	if (s->name[0] && !s->spectator) {
	    Cam_Lock(i);
	    return;
	}
	i = (i + 1) % MAX_CLIENTS;
    } while (i != end);
    // stay on same guy?
    i = spec_track;
    s = &cl.players[i];
    if (s->name[0] && !s->spectator) {
	Cam_Lock(i);
	return;
    }
    Con_Printf("No target found ...\n");
    autocam = locked = false;
}

void
Cam_Reset(void)
{
    autocam = CAM_NONE;
    spec_track = 0;
}

void
CL_Cam_RegisterVariables()
{
    Cvar_RegisterVariable(&cl_hightrack);
    Cvar_RegisterVariable(&cl_chasecam);
//      Cvar_RegisterVariable (&cl_camera_maxpitch);
//      Cvar_RegisterVariable (&cl_camera_maxyaw);
}
