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
// world.c -- world query functions

#include "quakedef.h"

/*

entities never clip against themselves, or their owner

line of sight checks trace->inopen and trace->inwater, but bullets don't

*/

cvar_t sv_debugmove = {CVAR_NOTIFY, "sv_debugmove", "0"};
cvar_t sv_areagrid_mingridsize = {CVAR_NOTIFY, "sv_areagrid_mingridsize", "64"};

void SV_AreaStats_f(void);

void SV_World_Init(void)
{
	Cvar_RegisterVariable(&sv_debugmove);
	Cvar_RegisterVariable(&sv_areagrid_mingridsize);
	Cmd_AddCommand("sv_areastats", SV_AreaStats_f);
	Collision_Init();
}

typedef struct
{
	// bounding box of entire move area
	vec3_t boxmins, boxmaxs;

	// size of the moving object
	vec3_t mins, maxs;

	// size when clipping against monsters
	vec3_t mins2, maxs2;

	// start and end origin of move
	vec3_t start, end;

	// trace results
	trace_t trace;

	// type of move (like ignoring monsters, or similar)
	int type;

	// the edict that is moving (if any)
	edict_t *passedict;
}
moveclip_t;

//============================================================================

// ClearLink is used for new headnodes
static void ClearLink (link_t *l)
{
	l->entitynumber = 0;
	l->prev = l->next = l;
}

static void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

static void InsertLinkBefore (link_t *l, link_t *before, int entitynumber)
{
	l->entitynumber = entitynumber;
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}


/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/

int sv_areagrid_stats_calls = 0;
int sv_areagrid_stats_nodechecks = 0;
int sv_areagrid_stats_entitychecks = 0;

void SV_AreaStats_f(void)
{
	Con_Printf("areagrid check stats: %d calls %d nodes (%f per call) %d entities (%f per call)\n", sv_areagrid_stats_calls, sv_areagrid_stats_nodechecks, (double) sv_areagrid_stats_nodechecks / (double) sv_areagrid_stats_calls, sv_areagrid_stats_entitychecks, (double) sv_areagrid_stats_entitychecks / (double) sv_areagrid_stats_calls);
	sv_areagrid_stats_calls = 0;
	sv_areagrid_stats_nodechecks = 0;
	sv_areagrid_stats_entitychecks = 0;
}

typedef struct areagrid_s
{
	link_t trigger_edicts;
	link_t solid_edicts;
}
areagrid_t;

#define AREA_GRID 512
#define AREA_GRIDNODES (AREA_GRID * AREA_GRID)

static areagrid_t sv_areagrid[AREA_GRIDNODES], sv_areagrid_outside;
static vec3_t sv_areagrid_bias, sv_areagrid_scale, sv_areagrid_mins, sv_areagrid_maxs, sv_areagrid_size;
static int sv_areagrid_marknumber = 1;

void SV_CreateAreaGrid (vec3_t mins, vec3_t maxs)
{
	int i;
	ClearLink (&sv_areagrid_outside.trigger_edicts);
	ClearLink (&sv_areagrid_outside.solid_edicts);
	// choose either the world box size, or a larger box to ensure the grid isn't too fine
	sv_areagrid_size[0] = max(maxs[0] - mins[0], AREA_GRID * sv_areagrid_mingridsize.value);
	sv_areagrid_size[1] = max(maxs[1] - mins[1], AREA_GRID * sv_areagrid_mingridsize.value);
	sv_areagrid_size[2] = max(maxs[2] - mins[2], AREA_GRID * sv_areagrid_mingridsize.value);
	// figure out the corners of such a box, centered at the center of the world box
	sv_areagrid_mins[0] = (mins[0] + maxs[0] - sv_areagrid_size[0]) * 0.5f;
	sv_areagrid_mins[1] = (mins[1] + maxs[1] - sv_areagrid_size[1]) * 0.5f;
	sv_areagrid_mins[2] = (mins[2] + maxs[2] - sv_areagrid_size[2]) * 0.5f;
	sv_areagrid_maxs[0] = (mins[0] + maxs[0] + sv_areagrid_size[0]) * 0.5f;
	sv_areagrid_maxs[1] = (mins[1] + maxs[1] + sv_areagrid_size[1]) * 0.5f;
	sv_areagrid_maxs[2] = (mins[2] + maxs[2] + sv_areagrid_size[2]) * 0.5f;
	// now calculate the actual useful info from that
	VectorNegate(sv_areagrid_mins, sv_areagrid_bias);
	sv_areagrid_scale[0] = AREA_GRID / sv_areagrid_size[0];
	sv_areagrid_scale[1] = AREA_GRID / sv_areagrid_size[1];
	sv_areagrid_scale[2] = AREA_GRID / sv_areagrid_size[2];
	for (i = 0;i < AREA_GRIDNODES;i++)
	{
		ClearLink (&sv_areagrid[i].trigger_edicts);
		ClearLink (&sv_areagrid[i].solid_edicts);
	}
	Con_DPrintf("sv_areagrid settings: divisions %ix%ix1 : box %f %f %f : %f %f %f size %f %f %f grid %f %f %f (mingrid %f)\n", AREA_GRID, AREA_GRID, sv_areagrid_mins[0], sv_areagrid_mins[1], sv_areagrid_mins[2], sv_areagrid_maxs[0], sv_areagrid_maxs[1], sv_areagrid_maxs[2], sv_areagrid_size[0], sv_areagrid_size[1], sv_areagrid_size[2], 1.0f / sv_areagrid_scale[0], 1.0f / sv_areagrid_scale[1], 1.0f / sv_areagrid_scale[2], sv_areagrid_mingridsize.value);
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld (void)
{
	Mod_CheckLoaded(sv.worldmodel);
	SV_CreateAreaGrid(sv.worldmodel->normalmins, sv.worldmodel->normalmaxs);
}


/*
===============
SV_UnlinkEdict

===============
*/
void SV_UnlinkEdict (edict_t *ent)
{
	int i;
	for (i = 0;i < ENTITYGRIDAREAS;i++)
	{
		if (ent->e->areagrid[i].prev)
		{
			RemoveLink (&ent->e->areagrid[i]);
			ent->e->areagrid[i].prev = ent->e->areagrid[i].next = NULL;
		}
	}
}


void SV_TouchAreaGrid(edict_t *ent)
{
	link_t *l, *next;
	edict_t *touch;
	areagrid_t *grid;
	int old_self, old_other, igrid[3], igridmins[3], igridmaxs[3];

	sv_areagrid_marknumber++;
	igridmins[0] = (int) ((ent->v->absmin[0] + sv_areagrid_bias[0]) * sv_areagrid_scale[0]);
	igridmins[1] = (int) ((ent->v->absmin[1] + sv_areagrid_bias[1]) * sv_areagrid_scale[1]);
	//igridmins[2] = (int) ((ent->v->absmin[2] + sv_areagrid_bias[2]) * sv_areagrid_scale[2]);
	igridmaxs[0] = (int) ((ent->v->absmax[0] + sv_areagrid_bias[0]) * sv_areagrid_scale[0]) + 1;
	igridmaxs[1] = (int) ((ent->v->absmax[1] + sv_areagrid_bias[1]) * sv_areagrid_scale[1]) + 1;
	//igridmaxs[2] = (int) ((ent->v->absmax[2] + sv_areagrid_bias[2]) * sv_areagrid_scale[2]) + 1;
	igridmins[0] = max(0, igridmins[0]);
	igridmins[1] = max(0, igridmins[1]);
	//igridmins[2] = max(0, igridmins[2]);
	igridmaxs[0] = min(AREA_GRID, igridmaxs[0]);
	igridmaxs[1] = min(AREA_GRID, igridmaxs[1]);
	//igridmaxs[2] = min(AREA_GRID, igridmaxs[2]);

	for (l = sv_areagrid_outside.trigger_edicts.next;l != &sv_areagrid_outside.trigger_edicts;l = next)
	{
		next = l->next;
		touch = EDICT_NUM(l->entitynumber);
		if (ent->v->absmin[0] > touch->v->absmax[0]
		 || ent->v->absmax[0] < touch->v->absmin[0]
		 || ent->v->absmin[1] > touch->v->absmax[1]
		 || ent->v->absmax[1] < touch->v->absmin[1]
		 || ent->v->absmin[2] > touch->v->absmax[2]
		 || ent->v->absmax[2] < touch->v->absmin[2])
			continue;
		if (touch == ent)
			continue;
		if (!touch->v->touch || touch->v->solid != SOLID_TRIGGER)
			continue;
		old_self = pr_global_struct->self;
		old_other = pr_global_struct->other;

		pr_global_struct->self = EDICT_TO_PROG(touch);
		pr_global_struct->other = EDICT_TO_PROG(ent);
		pr_global_struct->time = sv.time;
		PR_ExecuteProgram (touch->v->touch, "");

		pr_global_struct->self = old_self;
		pr_global_struct->other = old_other;
	}

	for (igrid[1] = igridmins[1];igrid[1] < igridmaxs[1];igrid[1]++)
	{
		grid = sv_areagrid + igrid[1] * AREA_GRID + igridmins[0];
		for (igrid[0] = igridmins[0];igrid[0] < igridmaxs[0];igrid[0]++, grid++)
		{
			for (l = grid->trigger_edicts.next;l != &grid->trigger_edicts;l = next)
			{
				next = l->next;
				touch = EDICT_NUM(l->entitynumber);
				if (touch->e->areagridmarknumber == sv_areagrid_marknumber)
					continue;
				touch->e->areagridmarknumber = sv_areagrid_marknumber;
				if (ent->v->absmin[0] > touch->v->absmax[0]
				 || ent->v->absmax[0] < touch->v->absmin[0]
				 || ent->v->absmin[1] > touch->v->absmax[1]
				 || ent->v->absmax[1] < touch->v->absmin[1]
				 || ent->v->absmin[2] > touch->v->absmax[2]
				 || ent->v->absmax[2] < touch->v->absmin[2])
					continue;
				// LordHavoc: id bug that won't be fixed: triggers do not ignore their owner like solid objects do
				if (touch == ent)
					continue;
				if (!touch->v->touch || touch->v->solid != SOLID_TRIGGER)
					continue;
				old_self = pr_global_struct->self;
				old_other = pr_global_struct->other;

				pr_global_struct->self = EDICT_TO_PROG(touch);
				pr_global_struct->other = EDICT_TO_PROG(ent);
				pr_global_struct->time = sv.time;
				PR_ExecuteProgram (touch->v->touch, "");

				pr_global_struct->self = old_self;
				pr_global_struct->other = old_other;
			}
		}
	}
}

void SV_LinkEdict_AreaGrid(edict_t *ent)
{
	areagrid_t *grid;
	int igrid[3], igridmins[3], igridmaxs[3], gridnum, entitynumber = NUM_FOR_EDICT(ent);

	if (entitynumber <= 0 || entitynumber >= sv.max_edicts || EDICT_NUM(entitynumber) != ent)
		Host_Error("SV_LinkEdict_AreaGrid: invalid edict %p (sv.edicts is %p, edict compared to sv.edicts is %i)\n", ent, sv.edicts, entitynumber);

	igridmins[0] = (int) ((ent->v->absmin[0] + sv_areagrid_bias[0]) * sv_areagrid_scale[0]);
	igridmins[1] = (int) ((ent->v->absmin[1] + sv_areagrid_bias[1]) * sv_areagrid_scale[1]);
	//igridmins[2] = (int) ((ent->v->absmin[2] + sv_areagrid_bias[2]) * sv_areagrid_scale[2]);
	igridmaxs[0] = (int) ((ent->v->absmax[0] + sv_areagrid_bias[0]) * sv_areagrid_scale[0]) + 1;
	igridmaxs[1] = (int) ((ent->v->absmax[1] + sv_areagrid_bias[1]) * sv_areagrid_scale[1]) + 1;
	//igridmaxs[2] = (int) ((ent->v->absmax[2] + sv_areagrid_bias[2]) * sv_areagrid_scale[2]) + 1;
	if (igridmins[0] < 0 || igridmaxs[0] > AREA_GRID || igridmins[1] < 0 || igridmaxs[1] > AREA_GRID || ((igridmaxs[0] - igridmins[0]) * (igridmaxs[1] - igridmins[1])) > ENTITYGRIDAREAS)
	{
		// wow, something outside the grid, store it as such
		if (ent->v->solid == SOLID_TRIGGER)
			InsertLinkBefore (&ent->e->areagrid[0], &sv_areagrid_outside.trigger_edicts, entitynumber);
		else
			InsertLinkBefore (&ent->e->areagrid[0], &sv_areagrid_outside.solid_edicts, entitynumber);
		return;
	}

	gridnum = 0;
	for (igrid[1] = igridmins[1];igrid[1] < igridmaxs[1];igrid[1]++)
	{
		grid = sv_areagrid + igrid[1] * AREA_GRID + igridmins[0];
		for (igrid[0] = igridmins[0];igrid[0] < igridmaxs[0];igrid[0]++, grid++, gridnum++)
		{
			if (ent->v->solid == SOLID_TRIGGER)
				InsertLinkBefore (&ent->e->areagrid[gridnum], &grid->trigger_edicts, entitynumber);
			else
				InsertLinkBefore (&ent->e->areagrid[gridnum], &grid->solid_edicts, entitynumber);
		}
	}
}

/*
===============
SV_LinkEdict

===============
*/
void SV_LinkEdict (edict_t *ent, qboolean touch_triggers)
{
	model_t *model;

	if (ent->e->areagrid[0].prev)
		SV_UnlinkEdict (ent);	// unlink from old position

	if (ent == sv.edicts)
		return;		// don't add the world

	if (ent->e->free)
		return;

// set the abs box

	if (ent->v->solid == SOLID_BSP)
	{
		if (ent->v->modelindex < 0 || ent->v->modelindex > MAX_MODELS)
			Host_Error("SOLID_BSP with invalid modelindex!\n");
		model = sv.models[(int) ent->v->modelindex];
		if (model != NULL)
		{
			Mod_CheckLoaded(model);
			if (!model->TraceBox)
				Host_Error("SOLID_BSP with non-collidable model\n");

			if (ent->v->angles[0] || ent->v->angles[2] || ent->v->avelocity[0] || ent->v->avelocity[2])
			{
				VectorAdd(ent->v->origin, model->rotatedmins, ent->v->absmin);
				VectorAdd(ent->v->origin, model->rotatedmaxs, ent->v->absmax);
			}
			else if (ent->v->angles[1] || ent->v->avelocity[1])
			{
				VectorAdd(ent->v->origin, model->yawmins, ent->v->absmin);
				VectorAdd(ent->v->origin, model->yawmaxs, ent->v->absmax);
			}
			else
			{
				VectorAdd(ent->v->origin, model->normalmins, ent->v->absmin);
				VectorAdd(ent->v->origin, model->normalmaxs, ent->v->absmax);
			}
		}
		else
		{
			// SOLID_BSP with no model is valid, mainly because some QC setup code does so temporarily
			VectorAdd(ent->v->origin, ent->v->mins, ent->v->absmin);
			VectorAdd(ent->v->origin, ent->v->maxs, ent->v->absmax);
		}
	}
	else
	{
		VectorAdd(ent->v->origin, ent->v->mins, ent->v->absmin);
		VectorAdd(ent->v->origin, ent->v->maxs, ent->v->absmax);
	}

//
// to make items easier to pick up and allow them to be grabbed off
// of shelves, the abs sizes are expanded
//
	if ((int)ent->v->flags & FL_ITEM)
	{
		ent->v->absmin[0] -= 15;
		ent->v->absmin[1] -= 15;
		ent->v->absmin[2] -= 1;
		ent->v->absmax[0] += 15;
		ent->v->absmax[1] += 15;
		ent->v->absmax[2] += 1;
	}
	else
	{
		// because movement is clipped an epsilon away from an actual edge,
		// we must fully check even when bounding boxes don't quite touch
		ent->v->absmin[0] -= 1;
		ent->v->absmin[1] -= 1;
		ent->v->absmin[2] -= 1;
		ent->v->absmax[0] += 1;
		ent->v->absmax[1] += 1;
		ent->v->absmax[2] += 1;
	}

	if (ent->v->solid == SOLID_NOT)
		return;

	SV_LinkEdict_AreaGrid(ent);

// if touch_triggers, touch all entities at this node and descend for more
	if (touch_triggers)
		SV_TouchAreaGrid(ent);
}



/*
===============================================================================

POINT TESTING IN HULLS

===============================================================================
*/

/*
============
SV_TestEntityPosition

This could be a lot more efficient...
============
*/
int SV_TestEntityPosition (edict_t *ent)
{
	return SV_Move (ent->v->origin, ent->v->mins, ent->v->maxs, ent->v->origin, MOVE_NORMAL, ent).startsolid;
}


/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/

/*
==================
SV_ClipMoveToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points
==================
*/
trace_t SV_ClipMoveToEntity(edict_t *ent, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int movetype)
{
	int i;
	trace_t trace;
	model_t *model = NULL;
	matrix4x4_t matrix, imatrix;
	float tempnormal[3], starttransformed[3], endtransformed[3];
	float starttransformedmins[3], starttransformedmaxs[3], endtransformedmins[3], endtransformedmaxs[3];

	if ((int) ent->v->solid == SOLID_BSP || movetype == MOVE_HITMODEL)
	{
		i = ent->v->modelindex;
		// if the modelindex is 0, it shouldn't be SOLID_BSP!
		if (i == 0)
		{
			Con_Printf("SV_ClipMoveToEntity: edict %i: SOLID_BSP with no model\n", NUM_FOR_EDICT(ent));
			memset(&trace, 0, sizeof(trace));
			return trace;
		}
		if ((unsigned int) i >= MAX_MODELS)
		{
			Con_Printf("SV_ClipMoveToEntity: edict %i: SOLID_BSP with invalid modelindex\n", NUM_FOR_EDICT(ent));
			memset(&trace, 0, sizeof(trace));
			return trace;
		}
		model = sv.models[i];
		if (i != 0 && model == NULL)
		{
			Con_Printf("SV_ClipMoveToEntity: edict %i: SOLID_BSP with invalid modelindex\n", NUM_FOR_EDICT(ent));
			memset(&trace, 0, sizeof(trace));
			return trace;
		}

		Mod_CheckLoaded(model);
		if ((int) ent->v->solid == SOLID_BSP)
		{
			if (!model->TraceBox)
			{
				Con_Printf("SV_ClipMoveToEntity: edict %i: SOLID_BSP with a non-collidable model\n", NUM_FOR_EDICT(ent));
				memset(&trace, 0, sizeof(trace));
				return trace;
			}
			if (ent->v->movetype != MOVETYPE_PUSH)
			{
				Con_Printf("SV_ClipMoveToEntity: edict %i: SOLID_BSP without MOVETYPE_PUSH\n", NUM_FOR_EDICT(ent));
				memset(&trace, 0, sizeof(trace));
				return trace;
			}
		}
		Matrix4x4_CreateFromQuakeEntity(&matrix, ent->v->origin[0], ent->v->origin[1], ent->v->origin[2], ent->v->angles[0], ent->v->angles[1], ent->v->angles[2], 1);
	}
	else
		Matrix4x4_CreateTranslate(&matrix, ent->v->origin[0], ent->v->origin[1], ent->v->origin[2]);

	Matrix4x4_Invert_Simple(&imatrix, &matrix);
	Matrix4x4_Transform(&imatrix, start, starttransformed);
	Matrix4x4_Transform(&imatrix, end, endtransformed);
#if COLLISIONPARANOID >= 3
	Con_Printf("trans(%f %f %f -> %f %f %f, %f %f %f -> %f %f %f)", start[0], start[1], start[2], starttransformed[0], starttransformed[1], starttransformed[2], end[0], end[1], end[2], endtransformed[0], endtransformed[1], endtransformed[2]);
#endif

	if (model && model->TraceBox)
	{
		int frame;
		frame = (int)ent->v->frame;
		frame = bound(0, frame, (model->numframes - 1));
		VectorAdd(starttransformed, maxs, starttransformedmaxs);
		VectorAdd(endtransformed, maxs, endtransformedmaxs);
		VectorAdd(starttransformed, mins, starttransformedmins);
		VectorAdd(endtransformed, mins, endtransformedmins);
		model->TraceBox(model, frame, &trace, starttransformedmins, starttransformedmaxs, endtransformedmins, endtransformedmaxs, SUPERCONTENTS_SOLID);
	}
	else
		Collision_ClipTrace_Box(&trace, ent->v->mins, ent->v->maxs, starttransformed, mins, maxs, endtransformed, SUPERCONTENTS_SOLID, SUPERCONTENTS_SOLID);

	if (trace.fraction < 1)
	{
		VectorLerp(start, trace.fraction, end, trace.endpos);
		VectorCopy(trace.plane.normal, tempnormal);
		Matrix4x4_Transform3x3(&matrix, tempnormal, trace.plane.normal);
		// FIXME: should recalc trace.plane.dist
	}
	else
		VectorCopy(end, trace.endpos);

	return trace;
}

//===========================================================================

void SV_ClipToNode(moveclip_t *clip, link_t *list)
{
	link_t *l, *next;
	edict_t *touch;
	trace_t trace;

	sv_areagrid_stats_nodechecks++;
	for (l = list->next;l != list;l = next)
	{
		next = l->next;
		touch = EDICT_NUM(l->entitynumber);
		if (touch->e->areagridmarknumber == sv_areagrid_marknumber)
			continue;
		touch->e->areagridmarknumber = sv_areagrid_marknumber;
		sv_areagrid_stats_entitychecks++;

		// LordHavoc: this box comparison isn't much use with the high resolution areagrid
		/*
		if (clip->boxmins[0] > touch->v->absmax[0]
		 || clip->boxmaxs[0] < touch->v->absmin[0]
		 || clip->boxmins[1] > touch->v->absmax[1]
		 || clip->boxmaxs[1] < touch->v->absmin[1]
		 || clip->boxmins[2] > touch->v->absmax[2]
		 || clip->boxmaxs[2] < touch->v->absmin[2])
			continue;
		*/

		if (clip->type == MOVE_NOMONSTERS && touch->v->solid != SOLID_BSP)
			continue;

		if (touch->v->solid == SOLID_NOT)
			continue;

		if (clip->passedict)
		{
			if (touch == clip->passedict)
				continue;
			if (!clip->passedict->v->size[0] && !touch->v->size[0])
				continue;	// points never interact
			if (PROG_TO_EDICT(touch->v->owner) == clip->passedict)
				continue;	// don't clip against own missiles
			if (PROG_TO_EDICT(clip->passedict->v->owner) == touch)
				continue;	// don't clip against owner
			// LordHavoc: corpse code
			if (clip->passedict->v->solid == SOLID_CORPSE && (touch->v->solid == SOLID_SLIDEBOX || touch->v->solid == SOLID_CORPSE))
				continue;
			if (clip->passedict->v->solid == SOLID_SLIDEBOX && touch->v->solid == SOLID_CORPSE)
				continue;
		}

		if (touch->v->solid == SOLID_TRIGGER)
		{
			ED_Print(touch);
			Host_Error ("Trigger in clipping list");
		}

		// might interact, so do an exact clip
		if ((int)touch->v->flags & FL_MONSTER)
			trace = SV_ClipMoveToEntity(touch, clip->start, clip->mins2, clip->maxs2, clip->end, clip->type);
		else
			trace = SV_ClipMoveToEntity(touch, clip->start, clip->mins, clip->maxs, clip->end, clip->type);
		// LordHavoc: take the 'best' answers from the new trace and combine with existing data
		if (trace.allsolid)
			clip->trace.allsolid = true;
		if (trace.startsolid)
		{
			clip->trace.startsolid = true;
			if (clip->trace.realfraction == 1)
				clip->trace.ent = touch;
		}
		if (trace.inopen)
			clip->trace.inopen = true;
		if (trace.inwater)
			clip->trace.inwater = true;
		if (trace.realfraction < clip->trace.realfraction)
		{
			clip->trace.fraction = trace.fraction;
			clip->trace.realfraction = trace.realfraction;
			VectorCopy(trace.endpos, clip->trace.endpos);
			clip->trace.plane = trace.plane;
			clip->trace.ent = touch;
		}
		clip->trace.startsupercontents |= trace.startsupercontents;
	}
}

/*
==================
SV_Move
==================
*/
#if COLLISIONPARANOID >= 1
trace_t SV_Move_(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int type, edict_t *passedict)
#else
trace_t SV_Move(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int type, edict_t *passedict)
#endif
{
	moveclip_t clip;
	vec3_t hullmins, hullmaxs;
	areagrid_t *grid;
	int i, igrid[3], igridmins[3], igridmaxs[3];

	memset(&clip, 0, sizeof(moveclip_t));

	VectorCopy(start, clip.start);
	VectorCopy(end, clip.end);
	VectorCopy(mins, clip.mins);
	VectorCopy(maxs, clip.maxs);
	VectorCopy(mins, clip.mins2);
	VectorCopy(maxs, clip.maxs2);
	clip.type = type;
	clip.passedict = passedict;
#if COLLISIONPARANOID >= 3
	Con_Printf("move(%f %f %f,%f %f %f)", clip.start[0], clip.start[1], clip.start[2], clip.end[0], clip.end[1], clip.end[2]);
#endif

	// clip to world
	clip.trace = SV_ClipMoveToEntity(sv.edicts, clip.start, clip.mins, clip.maxs, clip.end, clip.type);
	if (clip.trace.startsolid || clip.trace.fraction < 1)
		clip.trace.ent = sv.edicts;
	if (clip.type == MOVE_WORLDONLY)
		return clip.trace;

	if (clip.type == MOVE_MISSILE)
	{
		// LordHavoc: modified this, was = -15, now -= 15
		for (i = 0;i < 3;i++)
		{
			clip.mins2[i] -= 15;
			clip.maxs2[i] += 15;
		}
	}

	// get adjusted box for bmodel collisions if the world is q1bsp or hlbsp
	if (sv.worldmodel && sv.worldmodel->brush.RoundUpToHullSize)
		sv.worldmodel->brush.RoundUpToHullSize(sv.worldmodel, clip.mins, clip.maxs, hullmins, hullmaxs);
	else
	{
		VectorCopy(clip.mins, hullmins);
		VectorCopy(clip.maxs, hullmaxs);
	}

	// create the bounding box of the entire move
	for (i = 0;i < 3;i++)
	{
		clip.boxmins[i] = min(clip.start[i], clip.trace.endpos[i]) + min(hullmins[i], clip.mins2[i]) - 1;
		clip.boxmaxs[i] = max(clip.start[i], clip.trace.endpos[i]) + max(hullmaxs[i], clip.maxs2[i]) + 1;
	}

	// debug override to test against everything
	if (sv_debugmove.integer)
	{
		clip.boxmins[0] = clip.boxmins[1] = clip.boxmins[2] = -999999999;
		clip.boxmaxs[0] = clip.boxmaxs[1] = clip.boxmaxs[2] =  999999999;
	}

	// clip to enttiies
	sv_areagrid_stats_calls++;
	sv_areagrid_marknumber++;
	igridmins[0] = (int) ((clip.boxmins[0] + sv_areagrid_bias[0]) * sv_areagrid_scale[0]);
	igridmins[1] = (int) ((clip.boxmins[1] + sv_areagrid_bias[1]) * sv_areagrid_scale[1]);
	//igridmins[2] = (int) ((clip->boxmins[2] + sv_areagrid_bias[2]) * sv_areagrid_scale[2]);
	igridmaxs[0] = (int) ((clip.boxmaxs[0] + sv_areagrid_bias[0]) * sv_areagrid_scale[0]) + 1;
	igridmaxs[1] = (int) ((clip.boxmaxs[1] + sv_areagrid_bias[1]) * sv_areagrid_scale[1]) + 1;
	//igridmaxs[2] = (int) ((clip->boxmaxs[2] + sv_areagrid_bias[2]) * sv_areagrid_scale[2]) + 1;
	igridmins[0] = max(0, igridmins[0]);
	igridmins[1] = max(0, igridmins[1]);
	//igridmins[2] = max(0, igridmins[2]);
	igridmaxs[0] = min(AREA_GRID, igridmaxs[0]);
	igridmaxs[1] = min(AREA_GRID, igridmaxs[1]);
	//igridmaxs[2] = min(AREA_GRID, igridmaxs[2]);

	if (sv_areagrid_outside.solid_edicts.next != &sv_areagrid_outside.solid_edicts)
		SV_ClipToNode(&clip, &sv_areagrid_outside.solid_edicts);

	for (igrid[1] = igridmins[1];igrid[1] < igridmaxs[1];igrid[1]++)
		for (grid = sv_areagrid + igrid[1] * AREA_GRID + igridmins[0], igrid[0] = igridmins[0];igrid[0] < igridmaxs[0];igrid[0]++, grid++)
			if (grid->solid_edicts.next != &grid->solid_edicts)
				SV_ClipToNode(&clip, &grid->solid_edicts);

	return clip.trace;
}

#if COLLISIONPARANOID >= 1
trace_t SV_Move(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int type, edict_t *passedict)
{
	int endstuck;
	trace_t trace;
	vec3_t temp;
	trace = SV_Move_(start, mins, maxs, end, type, passedict);
	if (passedict)
	{
		VectorCopy(trace.endpos, temp);
		endstuck = SV_Move_(temp, mins, maxs, temp, type, passedict).startsolid;
#if COLLISIONPARANOID < 3
		if (trace.startsolid || endstuck)
#endif
			Con_Printf("%s{e%i:%f %f %f:%f %f %f:%f:%f %f %f%s%s}\n", (trace.startsolid || endstuck) ? "\002" : "", passedict ? passedict - sv.edicts : -1, passedict->v->origin[0], passedict->v->origin[1], passedict->v->origin[2], end[0] - passedict->v->origin[0], end[1] - passedict->v->origin[1], end[2] - passedict->v->origin[2], trace.fraction, trace.endpos[0] - passedict->v->origin[0], trace.endpos[1] - passedict->v->origin[1], trace.endpos[2] - passedict->v->origin[2], trace.startsolid ? " startstuck" : "", endstuck ? " endstuck" : "");
	}
	return trace;
}
#endif

int SV_PointSuperContents(const vec3_t point)
{
	return SV_Move(point, vec3_origin, vec3_origin, point, MOVE_NOMONSTERS, NULL).startsupercontents;
}

int SV_PointQ1Contents(const vec3_t point)
{
	return Mod_Q1BSP_NativeContentsFromSuperContents(NULL, SV_PointSuperContents(point));
}


