
#include "quakedef.h"

// this is 80 bytes
entity_state_t defaultstate =
{
	// ! means this is not sent to client
	0,//double time; // ! time this state was built (used on client for interpolation)
	{0,0,0},//float origin[3];
	{0,0,0},//float angles[3];
	0,//int number; // entity number this state is for
	0,//int effects;
	0,//unsigned short modelindex;
	0,//unsigned short frame;
	0,//unsigned short tagentity;
	0,//unsigned short specialvisibilityradius; // ! larger if it has effects/light
	0,//unsigned short viewmodelforclient; // !
	0,//unsigned short exteriormodelforclient; // ! not shown if first person viewing from this entity, shown in all other cases
	0,//unsigned short nodrawtoclient; // !
	0,//unsigned short drawonlytoclient; // !
	{0,0,0,0},//unsigned short light[4]; // color*256 (0.00 to 255.996), and radius*1
	0,//unsigned char active; // true if a valid state
	0,//unsigned char lightstyle;
	0,//unsigned char lightpflags;
	0,//unsigned char colormap;
	0,//unsigned char skin; // also chooses cubemap for rtlights if lightpflags & LIGHTPFLAGS_FULLDYNAMIC
	255,//unsigned char alpha;
	16,//unsigned char scale;
	0,//unsigned char glowsize;
	254,//unsigned char glowcolor;
	0,//unsigned char flags;
	0,//unsigned char tagindex;
	255,//unsigned char colormod;
	// padding to a multiple of 8 bytes (to align the double time)
	{0,0,0,0}//unsigned char unused[4]; // !
};

// keep track of quake entities because they need to be killed if they get stale
int cl_lastquakeentity = 0;
qbyte cl_isquakeentity[MAX_EDICTS];

void EntityFrameQuake_ReadEntity(int bits)
{
	int num;
	entity_t *ent;
	entity_state_t s;

	if (bits & U_MOREBITS)
		bits |= (MSG_ReadByte()<<8);
	if ((bits & U_EXTEND1) && cl.protocol != PROTOCOL_NEHAHRAMOVIE)
	{
		bits |= MSG_ReadByte() << 16;
		if (bits & U_EXTEND2)
			bits |= MSG_ReadByte() << 24;
	}

	if (bits & U_LONGENTITY)
		num = (unsigned short) MSG_ReadShort ();
	else
		num = MSG_ReadByte ();

	if (num >= MAX_EDICTS)
		Host_Error("EntityFrameQuake_ReadEntity: entity number (%i) >= MAX_EDICTS (%i)\n", num, MAX_EDICTS);
	if (num < 1)
		Host_Error("EntityFrameQuake_ReadEntity: invalid entity number (%i)\n", num);

	ent = cl_entities + num;

	// note: this inherits the 'active' state of the baseline chosen
	// (state_baseline is always active, state_current may not be active if
	// the entity was missing in the last frame)
	if (bits & U_DELTA)
		s = ent->state_current;
	else
	{
		s = ent->state_baseline;
		s.active = true;
	}

	cl_isquakeentity[num] = true;
	if (cl_lastquakeentity < num)
		cl_lastquakeentity = num;
	s.number = num;
	s.time = cl.mtime[0];
	s.flags = 0;
	if (bits & U_MODEL)		s.modelindex = (s.modelindex & 0xFF00) | MSG_ReadByte();
	if (bits & U_FRAME)		s.frame = (s.frame & 0xFF00) | MSG_ReadByte();
	if (bits & U_COLORMAP)	s.colormap = MSG_ReadByte();
	if (bits & U_SKIN)		s.skin = MSG_ReadByte();
	if (bits & U_EFFECTS)	s.effects = (s.effects & 0xFF00) | MSG_ReadByte();
	if (bits & U_ORIGIN1)	s.origin[0] = MSG_ReadCoord(cl.protocol);
	if (bits & U_ANGLE1)	s.angles[0] = MSG_ReadAngle(cl.protocol);
	if (bits & U_ORIGIN2)	s.origin[1] = MSG_ReadCoord(cl.protocol);
	if (bits & U_ANGLE2)	s.angles[1] = MSG_ReadAngle(cl.protocol);
	if (bits & U_ORIGIN3)	s.origin[2] = MSG_ReadCoord(cl.protocol);
	if (bits & U_ANGLE3)	s.angles[2] = MSG_ReadAngle(cl.protocol);
	if (bits & U_STEP)		s.flags |= RENDER_STEP;
	if (bits & U_ALPHA)		s.alpha = MSG_ReadByte();
	if (bits & U_SCALE)		s.scale = MSG_ReadByte();
	if (bits & U_EFFECTS2)	s.effects = (s.effects & 0x00FF) | (MSG_ReadByte() << 8);
	if (bits & U_GLOWSIZE)	s.glowsize = MSG_ReadByte();
	if (bits & U_GLOWCOLOR)	s.glowcolor = MSG_ReadByte();
	if (bits & U_COLORMOD)	s.colormod = MSG_ReadByte();
	if (bits & U_GLOWTRAIL) s.flags |= RENDER_GLOWTRAIL;
	if (bits & U_FRAME2)	s.frame = (s.frame & 0x00FF) | (MSG_ReadByte() << 8);
	if (bits & U_MODEL2)	s.modelindex = (s.modelindex & 0x00FF) | (MSG_ReadByte() << 8);
	if (bits & U_VIEWMODEL)	s.flags |= RENDER_VIEWMODEL;
	if (bits & U_EXTERIORMODEL)	s.flags |= RENDER_EXTERIORMODEL;

	// LordHavoc: to allow playback of the Nehahra movie
	if (cl.protocol == PROTOCOL_NEHAHRAMOVIE && (bits & U_EXTEND1))
	{
		// LordHavoc: evil format
		int i = MSG_ReadFloat();
		int j = MSG_ReadFloat() * 255.0f;
		if (i == 2)
		{
			i = MSG_ReadFloat();
			if (i)
				s.effects |= EF_FULLBRIGHT;
		}
		if (j < 0)
			s.alpha = 0;
		else if (j == 0 || j >= 255)
			s.alpha = 255;
		else
			s.alpha = j;
	}

	ent->state_previous = ent->state_current;
	ent->state_current = s;
	if (ent->state_current.active)
	{
		CL_MoveLerpEntityStates(ent);
		cl_entities_active[ent->state_current.number] = true;
	}

	if (msg_badread)
		Host_Error("EntityFrameQuake_ReadEntity: read error\n");
}

void EntityFrameQuake_ISeeDeadEntities(void)
{
	int num, lastentity;
	if (cl_lastquakeentity == 0)
		return;
	lastentity = cl_lastquakeentity;
	cl_lastquakeentity = 0;
	for (num = 0;num <= lastentity;num++)
	{
		if (cl_isquakeentity[num])
		{
			if (cl_entities_active[num] && cl_entities[num].state_current.time == cl.mtime[0])
			{
				cl_isquakeentity[num] = true;
				cl_lastquakeentity = num;
			}
			else
			{
				cl_isquakeentity[num] = false;
				cl_entities_active[num] = false;
				cl_entities[num].state_current = defaultstate;
				cl_entities[num].state_current.number = num;
			}
		}
	}
}

void EntityFrameQuake_WriteFrame(sizebuf_t *msg, int numstates, const entity_state_t *states)
{
	const entity_state_t *s;
	entity_state_t baseline;
	int i, bits;
	sizebuf_t buf;
	qbyte data[128];

	// prepare the buffer
	memset(&buf, 0, sizeof(buf));
	buf.data = data;
	buf.maxsize = sizeof(data);

	for (i = 0, s = states;i < numstates;i++, s++)
	{
		// prepare the buffer
		SZ_Clear(&buf);

// send an update
		bits = 0;
		if (s->number >= 256)
			bits |= U_LONGENTITY;
		if (s->flags & RENDER_STEP)
			bits |= U_STEP;
		if (s->flags & RENDER_VIEWMODEL)
			bits |= U_VIEWMODEL;
		if (s->flags & RENDER_GLOWTRAIL)
			bits |= U_GLOWTRAIL;
		if (s->flags & RENDER_EXTERIORMODEL)
			bits |= U_EXTERIORMODEL;

		// LordHavoc: old stuff, but rewritten to have more exact tolerances
		baseline = sv.edicts[s->number].e->baseline;
		if (baseline.origin[0] != s->origin[0])
			bits |= U_ORIGIN1;
		if (baseline.origin[1] != s->origin[1])
			bits |= U_ORIGIN2;
		if (baseline.origin[2] != s->origin[2])
			bits |= U_ORIGIN3;
		if (baseline.angles[0] != s->angles[0])
			bits |= U_ANGLE1;
		if (baseline.angles[1] != s->angles[1])
			bits |= U_ANGLE2;
		if (baseline.angles[2] != s->angles[2])
			bits |= U_ANGLE3;
		if (baseline.colormap != s->colormap)
			bits |= U_COLORMAP;
		if (baseline.skin != s->skin)
			bits |= U_SKIN;
		if (baseline.frame != s->frame)
		{
			bits |= U_FRAME;
			if (s->frame & 0xFF00)
				bits |= U_FRAME2;
		}
		if (baseline.effects != s->effects)
		{
			bits |= U_EFFECTS;
			if (s->effects & 0xFF00)
				bits |= U_EFFECTS2;
		}
		if (baseline.modelindex != s->modelindex)
		{
			bits |= U_MODEL;
			if (s->modelindex & 0xFF00)
				bits |= U_MODEL2;
		}
		if (baseline.alpha != s->alpha)
			bits |= U_ALPHA;
		if (baseline.scale != s->scale)
			bits |= U_SCALE;
		if (baseline.glowsize != s->glowsize)
			bits |= U_GLOWSIZE;
		if (baseline.glowcolor != s->glowcolor)
			bits |= U_GLOWCOLOR;

		// if extensions are disabled, clear the relevant update flags
		if (sv.netquakecompatible)
			bits &= 0x7FFF;

		// write the message
		if (bits >= 16777216)
			bits |= U_EXTEND2;
		if (bits >= 65536)
			bits |= U_EXTEND1;
		if (bits >= 256)
			bits |= U_MOREBITS;
		bits |= U_SIGNAL;

		MSG_WriteByte (&buf, bits);
		if (bits & U_MOREBITS)		MSG_WriteByte(&buf, bits>>8);
		if (bits & U_EXTEND1)		MSG_WriteByte(&buf, bits>>16);
		if (bits & U_EXTEND2)		MSG_WriteByte(&buf, bits>>24);
		if (bits & U_LONGENTITY)	MSG_WriteShort(&buf, s->number);
		else						MSG_WriteByte(&buf, s->number);

		if (bits & U_MODEL)			MSG_WriteByte(&buf, s->modelindex);
		if (bits & U_FRAME)			MSG_WriteByte(&buf, s->frame);
		if (bits & U_COLORMAP)		MSG_WriteByte(&buf, s->colormap);
		if (bits & U_SKIN)			MSG_WriteByte(&buf, s->skin);
		if (bits & U_EFFECTS)		MSG_WriteByte(&buf, s->effects);
		if (bits & U_ORIGIN1)		MSG_WriteCoord(&buf, s->origin[0], sv.protocol);
		if (bits & U_ANGLE1)		MSG_WriteAngle(&buf, s->angles[0], sv.protocol);
		if (bits & U_ORIGIN2)		MSG_WriteCoord(&buf, s->origin[1], sv.protocol);
		if (bits & U_ANGLE2)		MSG_WriteAngle(&buf, s->angles[1], sv.protocol);
		if (bits & U_ORIGIN3)		MSG_WriteCoord(&buf, s->origin[2], sv.protocol);
		if (bits & U_ANGLE3)		MSG_WriteAngle(&buf, s->angles[2], sv.protocol);
		if (bits & U_ALPHA)			MSG_WriteByte(&buf, s->alpha);
		if (bits & U_SCALE)			MSG_WriteByte(&buf, s->scale);
		if (bits & U_EFFECTS2)		MSG_WriteByte(&buf, s->effects >> 8);
		if (bits & U_GLOWSIZE)		MSG_WriteByte(&buf, s->glowsize);
		if (bits & U_GLOWCOLOR)		MSG_WriteByte(&buf, s->glowcolor);
		if (bits & U_COLORMOD)		MSG_WriteByte(&buf, s->colormod);
		if (bits & U_FRAME2)		MSG_WriteByte(&buf, s->frame >> 8);
		if (bits & U_MODEL2)		MSG_WriteByte(&buf, s->modelindex >> 8);

		// if the commit is full, we're done this frame
		if (msg->cursize + buf.cursize > msg->maxsize)
		{
			// next frame we will continue where we left off
			break;
		}
		// write the message to the packet
		SZ_Write(msg, buf.data, buf.cursize);
	}
}

int EntityState_DeltaBits(const entity_state_t *o, const entity_state_t *n)
{
	unsigned int bits;
	// if o is not active, delta from default
	if (!o->active)
		o = &defaultstate;
	bits = 0;
	if (fabs(n->origin[0] - o->origin[0]) > (1.0f / 256.0f))
		bits |= E_ORIGIN1;
	if (fabs(n->origin[1] - o->origin[1]) > (1.0f / 256.0f))
		bits |= E_ORIGIN2;
	if (fabs(n->origin[2] - o->origin[2]) > (1.0f / 256.0f))
		bits |= E_ORIGIN3;
	if ((qbyte) (n->angles[0] * (256.0f / 360.0f)) != (qbyte) (o->angles[0] * (256.0f / 360.0f)))
		bits |= E_ANGLE1;
	if ((qbyte) (n->angles[1] * (256.0f / 360.0f)) != (qbyte) (o->angles[1] * (256.0f / 360.0f)))
		bits |= E_ANGLE2;
	if ((qbyte) (n->angles[2] * (256.0f / 360.0f)) != (qbyte) (o->angles[2] * (256.0f / 360.0f)))
		bits |= E_ANGLE3;
	if ((n->modelindex ^ o->modelindex) & 0x00FF)
		bits |= E_MODEL1;
	if ((n->modelindex ^ o->modelindex) & 0xFF00)
		bits |= E_MODEL2;
	if ((n->frame ^ o->frame) & 0x00FF)
		bits |= E_FRAME1;
	if ((n->frame ^ o->frame) & 0xFF00)
		bits |= E_FRAME2;
	if ((n->effects ^ o->effects) & 0x00FF)
		bits |= E_EFFECTS1;
	if ((n->effects ^ o->effects) & 0xFF00)
		bits |= E_EFFECTS2;
	if (n->colormap != o->colormap)
		bits |= E_COLORMAP;
	if (n->skin != o->skin)
		bits |= E_SKIN;
	if (n->alpha != o->alpha)
		bits |= E_ALPHA;
	if (n->scale != o->scale)
		bits |= E_SCALE;
	if (n->glowsize != o->glowsize)
		bits |= E_GLOWSIZE;
	if (n->glowcolor != o->glowcolor)
		bits |= E_GLOWCOLOR;
	if (n->flags != o->flags)
		bits |= E_FLAGS;
	if (n->tagindex != o->tagindex || n->tagentity != o->tagentity)
		bits |= E_TAGATTACHMENT;
	if (n->light[0] != o->light[0] || n->light[1] != o->light[1] || n->light[2] != o->light[2] || n->light[3] != o->light[3])
		bits |= E_LIGHT;
	if (n->lightstyle != o->lightstyle)
		bits |= E_LIGHTSTYLE;
	if (n->lightpflags != o->lightpflags)
		bits |= E_LIGHTPFLAGS;

	if (bits)
	{
		if (bits &  0xFF000000)
			bits |= 0x00800000;
		if (bits &  0x00FF0000)
			bits |= 0x00008000;
		if (bits &  0x0000FF00)
			bits |= 0x00000080;
	}
	return bits;
}

void EntityState_WriteExtendBits(sizebuf_t *msg, unsigned int bits)
{
	MSG_WriteByte(msg, bits & 0xFF);
	if (bits & 0x00000080)
	{
		MSG_WriteByte(msg, (bits >> 8) & 0xFF);
		if (bits & 0x00008000)
		{
			MSG_WriteByte(msg, (bits >> 16) & 0xFF);
			if (bits & 0x00800000)
				MSG_WriteByte(msg, (bits >> 24) & 0xFF);
		}
	}
}

void EntityState_WriteFields(const entity_state_t *ent, sizebuf_t *msg, unsigned int bits)
{
	if (sv.protocol == PROTOCOL_DARKPLACES2)
	{
		if (bits & E_ORIGIN1)
			MSG_WriteCoord16i(msg, ent->origin[0]);
		if (bits & E_ORIGIN2)
			MSG_WriteCoord16i(msg, ent->origin[1]);
		if (bits & E_ORIGIN3)
			MSG_WriteCoord16i(msg, ent->origin[2]);
	}
	else if (sv.protocol == PROTOCOL_DARKPLACES1 || sv.protocol == PROTOCOL_DARKPLACES3 || sv.protocol == PROTOCOL_DARKPLACES4 || sv.protocol == PROTOCOL_DARKPLACES5)
	{
		// LordHavoc: have to write flags first, as they can modify protocol
		if (bits & E_FLAGS)
			MSG_WriteByte(msg, ent->flags);
		if (ent->flags & RENDER_LOWPRECISION)
		{
			if (bits & E_ORIGIN1)
				MSG_WriteCoord16i(msg, ent->origin[0]);
			if (bits & E_ORIGIN2)
				MSG_WriteCoord16i(msg, ent->origin[1]);
			if (bits & E_ORIGIN3)
				MSG_WriteCoord16i(msg, ent->origin[2]);
		}
		else
		{
			if (bits & E_ORIGIN1)
				MSG_WriteCoord32f(msg, ent->origin[0]);
			if (bits & E_ORIGIN2)
				MSG_WriteCoord32f(msg, ent->origin[1]);
			if (bits & E_ORIGIN3)
				MSG_WriteCoord32f(msg, ent->origin[2]);
		}
	}
	if (sv.protocol == PROTOCOL_DARKPLACES5 && !(ent->flags & RENDER_LOWPRECISION))
	{
		if (bits & E_ANGLE1)
			MSG_WriteAngle16i(msg, ent->angles[0]);
		if (bits & E_ANGLE2)
			MSG_WriteAngle16i(msg, ent->angles[1]);
		if (bits & E_ANGLE3)
			MSG_WriteAngle16i(msg, ent->angles[2]);
	}
	else
	{
		if (bits & E_ANGLE1)
			MSG_WriteAngle8i(msg, ent->angles[0]);
		if (bits & E_ANGLE2)
			MSG_WriteAngle8i(msg, ent->angles[1]);
		if (bits & E_ANGLE3)
			MSG_WriteAngle8i(msg, ent->angles[2]);
	}
	if (bits & E_MODEL1)
		MSG_WriteByte(msg, ent->modelindex & 0xFF);
	if (bits & E_MODEL2)
		MSG_WriteByte(msg, (ent->modelindex >> 8) & 0xFF);
	if (bits & E_FRAME1)
		MSG_WriteByte(msg, ent->frame & 0xFF);
	if (bits & E_FRAME2)
		MSG_WriteByte(msg, (ent->frame >> 8) & 0xFF);
	if (bits & E_EFFECTS1)
		MSG_WriteByte(msg, ent->effects & 0xFF);
	if (bits & E_EFFECTS2)
		MSG_WriteByte(msg, (ent->effects >> 8) & 0xFF);
	if (bits & E_COLORMAP)
		MSG_WriteByte(msg, ent->colormap);
	if (bits & E_SKIN)
		MSG_WriteByte(msg, ent->skin);
	if (bits & E_ALPHA)
		MSG_WriteByte(msg, ent->alpha);
	if (bits & E_SCALE)
		MSG_WriteByte(msg, ent->scale);
	if (bits & E_GLOWSIZE)
		MSG_WriteByte(msg, ent->glowsize);
	if (bits & E_GLOWCOLOR)
		MSG_WriteByte(msg, ent->glowcolor);
	if (sv.protocol == PROTOCOL_DARKPLACES2)
		if (bits & E_FLAGS)
			MSG_WriteByte(msg, ent->flags);
	if (bits & E_TAGATTACHMENT)
	{
		MSG_WriteShort(msg, ent->tagentity);
		MSG_WriteByte(msg, ent->tagindex);
	}
	if (bits & E_LIGHT)
	{
		MSG_WriteShort(msg, ent->light[0]);
		MSG_WriteShort(msg, ent->light[1]);
		MSG_WriteShort(msg, ent->light[2]);
		MSG_WriteShort(msg, ent->light[3]);
	}
	if (bits & E_LIGHTSTYLE)
		MSG_WriteByte(msg, ent->lightstyle);
	if (bits & E_LIGHTPFLAGS)
		MSG_WriteByte(msg, ent->lightpflags);
}

void EntityState_WriteUpdate(const entity_state_t *ent, sizebuf_t *msg, const entity_state_t *delta)
{
	unsigned int bits;
	if (ent->active)
	{
		// entity is active, check for changes from the delta
		if ((bits = EntityState_DeltaBits(delta, ent)))
		{
			// write the update number, bits, and fields
			MSG_WriteShort(msg, ent->number);
			EntityState_WriteExtendBits(msg, bits);
			EntityState_WriteFields(ent, msg, bits);
		}
	}
	else
	{
		// entity is inactive, check if the delta was active
		if (delta->active)
		{
			// write the remove number
			MSG_WriteShort(msg, ent->number | 0x8000);
		}
	}
}

int EntityState_ReadExtendBits(void)
{
	unsigned int bits;
	bits = MSG_ReadByte();
	if (bits & 0x00000080)
	{
		bits |= MSG_ReadByte() << 8;
		if (bits & 0x00008000)
		{
			bits |= MSG_ReadByte() << 16;
			if (bits & 0x00800000)
				bits |= MSG_ReadByte() << 24;
		}
	}
	return bits;
}

void EntityState_ReadFields(entity_state_t *e, unsigned int bits)
{
	if (cl.protocol == PROTOCOL_DARKPLACES2)
	{
		if (bits & E_ORIGIN1)
			e->origin[0] = MSG_ReadCoord16i();
		if (bits & E_ORIGIN2)
			e->origin[1] = MSG_ReadCoord16i();
		if (bits & E_ORIGIN3)
			e->origin[2] = MSG_ReadCoord16i();
	}
	else if (cl.protocol == PROTOCOL_DARKPLACES1 || cl.protocol == PROTOCOL_DARKPLACES3 || cl.protocol == PROTOCOL_DARKPLACES4 || cl.protocol == PROTOCOL_DARKPLACES5)
	{
		if (bits & E_FLAGS)
			e->flags = MSG_ReadByte();
		if (e->flags & RENDER_LOWPRECISION)
		{
			if (bits & E_ORIGIN1)
				e->origin[0] = MSG_ReadCoord16i();
			if (bits & E_ORIGIN2)
				e->origin[1] = MSG_ReadCoord16i();
			if (bits & E_ORIGIN3)
				e->origin[2] = MSG_ReadCoord16i();
		}
		else
		{
			if (bits & E_ORIGIN1)
				e->origin[0] = MSG_ReadCoord32f();
			if (bits & E_ORIGIN2)
				e->origin[1] = MSG_ReadCoord32f();
			if (bits & E_ORIGIN3)
				e->origin[2] = MSG_ReadCoord32f();
		}
	}
	if (cl.protocol == PROTOCOL_DARKPLACES5 && !(e->flags & RENDER_LOWPRECISION))
	{
		if (bits & E_ANGLE1)
			e->angles[0] = MSG_ReadAngle16i();
		if (bits & E_ANGLE2)
			e->angles[1] = MSG_ReadAngle16i();
		if (bits & E_ANGLE3)
			e->angles[2] = MSG_ReadAngle16i();
	}
	else
	{
		if (bits & E_ANGLE1)
			e->angles[0] = MSG_ReadAngle8i();
		if (bits & E_ANGLE2)
			e->angles[1] = MSG_ReadAngle8i();
		if (bits & E_ANGLE3)
			e->angles[2] = MSG_ReadAngle8i();
	}
	if (bits & E_MODEL1)
		e->modelindex = (e->modelindex & 0xFF00) | (unsigned int) MSG_ReadByte();
	if (bits & E_MODEL2)
		e->modelindex = (e->modelindex & 0x00FF) | ((unsigned int) MSG_ReadByte() << 8);
	if (bits & E_FRAME1)
		e->frame = (e->frame & 0xFF00) | (unsigned int) MSG_ReadByte();
	if (bits & E_FRAME2)
		e->frame = (e->frame & 0x00FF) | ((unsigned int) MSG_ReadByte() << 8);
	if (bits & E_EFFECTS1)
		e->effects = (e->effects & 0xFF00) | (unsigned int) MSG_ReadByte();
	if (bits & E_EFFECTS2)
		e->effects = (e->effects & 0x00FF) | ((unsigned int) MSG_ReadByte() << 8);
	if (bits & E_COLORMAP)
		e->colormap = MSG_ReadByte();
	if (bits & E_SKIN)
		e->skin = MSG_ReadByte();
	if (bits & E_ALPHA)
		e->alpha = MSG_ReadByte();
	if (bits & E_SCALE)
		e->scale = MSG_ReadByte();
	if (bits & E_GLOWSIZE)
		e->glowsize = MSG_ReadByte();
	if (bits & E_GLOWCOLOR)
		e->glowcolor = MSG_ReadByte();
	if (cl.protocol == PROTOCOL_DARKPLACES2)
		if (bits & E_FLAGS)
			e->flags = MSG_ReadByte();
	if (bits & E_TAGATTACHMENT)
	{
		e->tagentity = (unsigned short) MSG_ReadShort();
		e->tagindex = MSG_ReadByte();
	}
	if (bits & E_LIGHT)
	{
		e->light[0] = (unsigned short) MSG_ReadShort();
		e->light[1] = (unsigned short) MSG_ReadShort();
		e->light[2] = (unsigned short) MSG_ReadShort();
		e->light[3] = (unsigned short) MSG_ReadShort();
	}
	if (bits & E_LIGHTSTYLE)
		e->lightstyle = MSG_ReadByte();
	if (bits & E_LIGHTPFLAGS)
		e->lightpflags = MSG_ReadByte();

	if (developer_networkentities.integer >= 2)
	{
		Con_Printf("ReadFields e%i", e->number);

		if (bits & E_ORIGIN1)
			Con_Printf(" E_ORIGIN1 %f", e->origin[0]);
		if (bits & E_ORIGIN2)
			Con_Printf(" E_ORIGIN2 %f", e->origin[1]);
		if (bits & E_ORIGIN3)
			Con_Printf(" E_ORIGIN3 %f", e->origin[2]);
		if (bits & E_ANGLE1)
			Con_Printf(" E_ANGLE1 %f", e->angles[0]);
		if (bits & E_ANGLE2)
			Con_Printf(" E_ANGLE2 %f", e->angles[1]);
		if (bits & E_ANGLE3)
			Con_Printf(" E_ANGLE3 %f", e->angles[2]);
		if (bits & (E_MODEL1 | E_MODEL2))
			Con_Printf(" E_MODEL %i", e->modelindex);

		if (bits & (E_FRAME1 | E_FRAME2))
			Con_Printf(" E_FRAME %i", e->frame);
		if (bits & (E_EFFECTS1 | E_EFFECTS2))
			Con_Printf(" E_EFFECTS %i", e->effects);
		if (bits & E_ALPHA)
			Con_Printf(" E_ALPHA %f", e->alpha / 255.0f);
		if (bits & E_SCALE)
			Con_Printf(" E_SCALE %f", e->scale / 16.0f);
		if (bits & E_COLORMAP)
			Con_Printf(" E_COLORMAP %i", e->colormap);
		if (bits & E_SKIN)
			Con_Printf(" E_SKIN %i", e->skin);

		if (bits & E_GLOWSIZE)
			Con_Printf(" E_GLOWSIZE %i", e->glowsize * 4);
		if (bits & E_GLOWCOLOR)
			Con_Printf(" E_GLOWCOLOR %i", e->glowcolor);
		
		if (bits & E_LIGHT)
			Con_Printf(" E_LIGHT %i:%i:%i:%i", e->light[0], e->light[1], e->light[2], e->light[3]);
		if (bits & E_LIGHTPFLAGS)			
			Con_Printf(" E_LIGHTPFLAGS %i", e->lightpflags);

		if (bits & E_TAGATTACHMENT)
			Con_Printf(" E_TAGATTACHMENT e%i:%i", e->tagentity, e->tagindex);
		if (bits & E_LIGHTSTYLE)
			Con_Printf(" E_LIGHTSTYLE %i", e->lightstyle);
		Con_Print("\n");
	}
}

// (client and server) allocates a new empty database
entityframe_database_t *EntityFrame_AllocDatabase(mempool_t *mempool)
{
	return Mem_Alloc(mempool, sizeof(entityframe_database_t));
}

// (client and server) frees the database
void EntityFrame_FreeDatabase(entityframe_database_t *d)
{
	Mem_Free(d);
}

// (server) clears the database to contain no frames (thus delta compression compresses against nothing)
void EntityFrame_ClearDatabase(entityframe_database_t *d)
{
	memset(d, 0, sizeof(*d));
}

// (server and client) removes frames older than 'frame' from database
void EntityFrame_AckFrame(entityframe_database_t *d, int frame)
{
	int i;
	d->ackframenum = frame;
	for (i = 0;i < d->numframes && d->frames[i].framenum < frame;i++);
	// ignore outdated frame acks (out of order packets)
	if (i == 0)
		return;
	d->numframes -= i;
	// if some queue is left, slide it down to beginning of array
	if (d->numframes)
		memmove(&d->frames[0], &d->frames[i], sizeof(d->frames[0]) * d->numframes);
}

// (server) clears frame, to prepare for adding entities
void EntityFrame_Clear(entity_frame_t *f, vec3_t eye, int framenum)
{
	f->time = 0;
	f->framenum = framenum;
	f->numentities = 0;
	if (eye == NULL)
		VectorClear(f->eye);
	else
		VectorCopy(eye, f->eye);
}

// (server and client) reads a frame from the database
void EntityFrame_FetchFrame(entityframe_database_t *d, int framenum, entity_frame_t *f)
{
	int i, n;
	EntityFrame_Clear(f, NULL, -1);
	for (i = 0;i < d->numframes && d->frames[i].framenum < framenum;i++);
	if (i < d->numframes && framenum == d->frames[i].framenum)
	{
		f->framenum = framenum;
		f->numentities = d->frames[i].endentity - d->frames[i].firstentity;
		n = MAX_ENTITY_DATABASE - (d->frames[i].firstentity % MAX_ENTITY_DATABASE);
		if (n > f->numentities)
			n = f->numentities;
		memcpy(f->entitydata, d->entitydata + d->frames[i].firstentity % MAX_ENTITY_DATABASE, sizeof(*f->entitydata) * n);
		if (f->numentities > n)
			memcpy(f->entitydata + n, d->entitydata, sizeof(*f->entitydata) * (f->numentities - n));
		VectorCopy(d->eye, f->eye);
	}
}

// (server and client) adds a entity_frame to the database, for future reference
void EntityFrame_AddFrame(entityframe_database_t *d, vec3_t eye, int framenum, int numentities, const entity_state_t *entitydata)
{
	int n, e;
	entity_frameinfo_t *info;

	VectorCopy(eye, d->eye);

	// figure out how many entity slots are used already
	if (d->numframes)
	{
		n = d->frames[d->numframes - 1].endentity - d->frames[0].firstentity;
		if (n + numentities > MAX_ENTITY_DATABASE || d->numframes >= MAX_ENTITY_HISTORY)
		{
			// ran out of room, dump database
			EntityFrame_ClearDatabase(d);
		}
	}

	info = &d->frames[d->numframes];
	info->framenum = framenum;
	e = -1000;
	// make sure we check the newly added frame as well, but we haven't incremented numframes yet
	for (n = 0;n <= d->numframes;n++)
	{
		if (e >= d->frames[n].framenum)
		{
			if (e == framenum)
				Con_Print("EntityFrame_AddFrame: tried to add out of sequence frame to database\n");
			else
				Con_Print("EntityFrame_AddFrame: out of sequence frames in database\n");
			return;
		}
		e = d->frames[n].framenum;
	}
	// if database still has frames after that...
	if (d->numframes)
		info->firstentity = d->frames[d->numframes - 1].endentity;
	else
		info->firstentity = 0;
	info->endentity = info->firstentity + numentities;
	d->numframes++;

	n = info->firstentity % MAX_ENTITY_DATABASE;
	e = MAX_ENTITY_DATABASE - n;
	if (e > numentities)
		e = numentities;
	memcpy(d->entitydata + n, entitydata, sizeof(entity_state_t) * e);
	if (numentities > e)
		memcpy(d->entitydata, entitydata + e, sizeof(entity_state_t) * (numentities - e));
}

// (server) writes a frame to network stream
static entity_frame_t deltaframe; // FIXME?
void EntityFrame_WriteFrame(sizebuf_t *msg, entityframe_database_t *d, int numstates, const entity_state_t *states, int viewentnum)
{
	int i, onum, number;
	entity_frame_t *o = &deltaframe;
	const entity_state_t *ent, *delta;
	vec3_t eye;

	d->latestframenum++;

	VectorClear(eye);
	for (i = 0;i < numstates;i++)
	{
		if (states[i].number == viewentnum)
		{
			VectorSet(eye, states[i].origin[0], states[i].origin[1], states[i].origin[2] + 22);
			break;
		}
	}

	EntityFrame_AddFrame(d, eye, d->latestframenum, numstates, states);

	EntityFrame_FetchFrame(d, d->ackframenum, o);

	MSG_WriteByte (msg, svc_entities);
	MSG_WriteLong (msg, o->framenum);
	MSG_WriteLong (msg, d->latestframenum);
	MSG_WriteFloat (msg, eye[0]);
	MSG_WriteFloat (msg, eye[1]);
	MSG_WriteFloat (msg, eye[2]);

	onum = 0;
	for (i = 0;i < numstates;i++)
	{
		ent = states + i;
		number = ent->number;
		for (;onum < o->numentities && o->entitydata[onum].number < number;onum++)
		{
			// write remove message
			MSG_WriteShort(msg, o->entitydata[onum].number | 0x8000);
		}
		if (onum < o->numentities && (o->entitydata[onum].number == number))
		{
			// delta from previous frame
			delta = o->entitydata + onum;
			// advance to next entity in delta frame
			onum++;
		}
		else
		{
			// delta from defaults
			delta = &defaultstate;
		}
		EntityState_WriteUpdate(ent, msg, delta);
	}
	for (;onum < o->numentities;onum++)
	{
		// write remove message
		MSG_WriteShort(msg, o->entitydata[onum].number | 0x8000);
	}
	MSG_WriteShort(msg, 0xFFFF);
}

// (client) reads a frame from network stream
static entity_frame_t framedata; // FIXME?
void EntityFrame_CL_ReadFrame(void)
{
	int i, number, removed;
	entity_frame_t *f = &framedata, *delta = &deltaframe;
	entity_state_t *e, *old, *oldend;
	entity_t *ent;
	entityframe_database_t *d;
	if (!cl.entitydatabase)
		cl.entitydatabase = EntityFrame_AllocDatabase(cl_entities_mempool);
	d = cl.entitydatabase;

	EntityFrame_Clear(f, NULL, -1);

	// read the frame header info
	f->time = cl.mtime[0];
	number = MSG_ReadLong();
	cl.latestframenum = f->framenum = MSG_ReadLong();
	f->eye[0] = MSG_ReadFloat();
	f->eye[1] = MSG_ReadFloat();
	f->eye[2] = MSG_ReadFloat();
	EntityFrame_AckFrame(d, number);
	EntityFrame_FetchFrame(d, number, delta);
	old = delta->entitydata;
	oldend = old + delta->numentities;
	// read entities until we hit the magic 0xFFFF end tag
	while ((number = (unsigned short) MSG_ReadShort()) != 0xFFFF && !msg_badread)
	{
		if (msg_badread)
			Host_Error("EntityFrame_Read: read error\n");
		removed = number & 0x8000;
		number &= 0x7FFF;
		if (number >= MAX_EDICTS)
			Host_Error("EntityFrame_Read: number (%i) >= MAX_EDICTS (%i)\n", number, MAX_EDICTS);

		// seek to entity, while copying any skipped entities (assume unchanged)
		while (old < oldend && old->number < number)
		{
			if (f->numentities >= MAX_ENTITY_DATABASE)
				Host_Error("EntityFrame_Read: entity list too big\n");
			f->entitydata[f->numentities] = *old++;
			f->entitydata[f->numentities++].time = cl.mtime[0];
		}
		if (removed)
		{
			if (old < oldend && old->number == number)
				old++;
			else
				Con_Printf("EntityFrame_Read: REMOVE on unused entity %i\n", number);
		}
		else
		{
			if (f->numentities >= MAX_ENTITY_DATABASE)
				Host_Error("EntityFrame_Read: entity list too big\n");

			// reserve this slot
			e = f->entitydata + f->numentities++;

			if (old < oldend && old->number == number)
			{
				// delta from old entity
				*e = *old++;
			}
			else
			{
				// delta from defaults
				*e = defaultstate;
			}

			cl_entities_active[number] = true;
			e->active = true;
			e->time = cl.mtime[0];
			e->number = number;
			EntityState_ReadFields(e, EntityState_ReadExtendBits());
		}
	}
	while (old < oldend)
	{
		if (f->numentities >= MAX_ENTITY_DATABASE)
			Host_Error("EntityFrame_Read: entity list too big\n");
		f->entitydata[f->numentities] = *old++;
		f->entitydata[f->numentities++].time = cl.mtime[0];
	}
	EntityFrame_AddFrame(d, f->eye, f->framenum, f->numentities, f->entitydata);

	memset(cl_entities_active, 0, cl_max_entities * sizeof(qbyte));
	number = 1;
	for (i = 0;i < f->numentities;i++)
	{
		for (;number < f->entitydata[i].number;number++)
		{
			if (cl_entities_active[number])
			{
				cl_entities_active[number] = false;
				cl_entities[number].state_current.active = false;
			}
		}
		// update the entity
		ent = &cl_entities[number];
		ent->state_previous = ent->state_current;
		ent->state_current = f->entitydata[i];
		CL_MoveLerpEntityStates(ent);
		// the entity lives again...
		cl_entities_active[number] = true;
		number++;
	}
	for (;number < cl_max_entities;number++)
	{
		if (cl_entities_active[number])
		{
			cl_entities_active[number] = false;
			cl_entities[number].state_current.active = false;
		}
	}
}


// (client) returns the frame number of the most recent frame recieved
int EntityFrame_MostRecentlyRecievedFrameNum(entityframe_database_t *d)
{
	if (d->numframes)
		return d->frames[d->numframes - 1].framenum;
	else
		return -1;
}






entity_state_t *EntityFrame4_GetReferenceEntity(entityframe4_database_t *d, int number)
{
	if (d->maxreferenceentities <= number)
	{
		int oldmax = d->maxreferenceentities;
		entity_state_t *oldentity = d->referenceentity;
		d->maxreferenceentities = (number + 15) & ~7;
		d->referenceentity = Mem_Alloc(d->mempool, d->maxreferenceentities * sizeof(*d->referenceentity));
		if (oldentity)
		{
			memcpy(d->referenceentity, oldentity, oldmax * sizeof(*d->referenceentity));
			Mem_Free(oldentity);
		}
		// clear the newly created entities
		for (;oldmax < d->maxreferenceentities;oldmax++)
		{
			d->referenceentity[oldmax] = defaultstate;
			d->referenceentity[oldmax].number = oldmax;
		}
	}
	return d->referenceentity + number;
}

void EntityFrame4_AddCommitEntity(entityframe4_database_t *d, const entity_state_t *s)
{
	// resize commit's entity list if full
	if (d->currentcommit->maxentities <= d->currentcommit->numentities)
	{
		entity_state_t *oldentity = d->currentcommit->entity;
		d->currentcommit->maxentities += 8;
		d->currentcommit->entity = Mem_Alloc(d->mempool, d->currentcommit->maxentities * sizeof(*d->currentcommit->entity));
		if (oldentity)
		{
			memcpy(d->currentcommit->entity, oldentity, d->currentcommit->numentities * sizeof(*d->currentcommit->entity));
			Mem_Free(oldentity);
		}
	}
	d->currentcommit->entity[d->currentcommit->numentities++] = *s;
}

entityframe4_database_t *EntityFrame4_AllocDatabase(mempool_t *pool)
{
	entityframe4_database_t *d;
	d = Mem_Alloc(pool, sizeof(*d));
	d->mempool = pool;
	EntityFrame4_ResetDatabase(d);
	return d;
}

void EntityFrame4_FreeDatabase(entityframe4_database_t *d)
{
	int i;
	for (i = 0;i < MAX_ENTITY_HISTORY;i++)
		if (d->commit[i].entity)
			Mem_Free(d->commit[i].entity);
	if (d->referenceentity)
		Mem_Free(d->referenceentity);
	Mem_Free(d);
}

void EntityFrame4_ResetDatabase(entityframe4_database_t *d)
{
	int i;
	d->referenceframenum = -1;
	for (i = 0;i < MAX_ENTITY_HISTORY;i++)
		d->commit[i].numentities = 0;
	for (i = 0;i < d->maxreferenceentities;i++)
		d->referenceentity[i] = defaultstate;
}

int EntityFrame4_AckFrame(entityframe4_database_t *d, int framenum, int servermode)
{
	int i, j, found;
	entity_database4_commit_t *commit;
	if (framenum == -1)
	{
		// reset reference, but leave commits alone
		d->referenceframenum = -1;
		for (i = 0;i < d->maxreferenceentities;i++)
			d->referenceentity[i] = defaultstate;
		// if this is the server, remove commits
			for (i = 0, commit = d->commit;i < MAX_ENTITY_HISTORY;i++, commit++)
				commit->numentities = 0;
		found = true;
	}
	else if (d->referenceframenum == framenum)
		found = true;
	else
	{
		found = false;
		for (i = 0, commit = d->commit;i < MAX_ENTITY_HISTORY;i++, commit++)
		{
			if (commit->numentities && commit->framenum <= framenum)
			{
				if (commit->framenum == framenum)
				{
					found = true;
					d->referenceframenum = framenum;
					if (developer_networkentities.integer >= 3)
					{
						for (j = 0;j < commit->numentities;j++)
						{
							entity_state_t *s = EntityFrame4_GetReferenceEntity(d, commit->entity[j].number);
							if (commit->entity[j].active != s->active)
							{
								if (commit->entity[j].active)
									Con_Printf("commit entity %i has become active (modelindex %i)\n", commit->entity[j].number, commit->entity[j].modelindex);
								else
									Con_Printf("commit entity %i has become inactive (modelindex %i)\n", commit->entity[j].number, commit->entity[j].modelindex);
							}
							*s = commit->entity[j];
						}
					}
					else
						for (j = 0;j < commit->numentities;j++)
							*EntityFrame4_GetReferenceEntity(d, commit->entity[j].number) = commit->entity[j];
				}
				commit->numentities = 0;
			}
		}
	}
	if (developer_networkentities.integer >= 1)
	{
		Con_Printf("ack ref:%i database updated to: ref:%i commits:", framenum, d->referenceframenum);
		for (i = 0;i < MAX_ENTITY_HISTORY;i++)
			if (d->commit[i].numentities)
				Con_Printf(" %i", d->commit[i].framenum);
		Con_Print("\n");
	}
	return found;
}

void EntityFrame4_CL_ReadFrame(void)
{
	int i, n, cnumber, referenceframenum, framenum, enumber, done, stopnumber, skip = false;
	entity_state_t *s;
	entityframe4_database_t *d;
	if (!cl.entitydatabase4)
		cl.entitydatabase4 = EntityFrame4_AllocDatabase(cl_entities_mempool);
	d = cl.entitydatabase4;
	// read the number of the frame this refers to
	referenceframenum = MSG_ReadLong();
	// read the number of this frame
	cl.latestframenum = framenum = MSG_ReadLong();
	// read the start number
	enumber = (unsigned short) MSG_ReadShort();
	if (developer_networkentities.integer >= 1)
	{
		Con_Printf("recv svc_entities num:%i ref:%i database: ref:%i commits:", framenum, referenceframenum, d->referenceframenum);
		for (i = 0;i < MAX_ENTITY_HISTORY;i++)
			if (d->commit[i].numentities)
				Con_Printf(" %i", d->commit[i].framenum);
		Con_Print("\n");
	}
	if (!EntityFrame4_AckFrame(d, referenceframenum, false))
	{
		Con_Print("EntityFrame4_CL_ReadFrame: reference frame invalid (VERY BAD ERROR), this update will be skipped\n");
		skip = true;
	}
	d->currentcommit = NULL;
	for (i = 0;i < MAX_ENTITY_HISTORY;i++)
	{
		if (!d->commit[i].numentities)
		{
			d->currentcommit = d->commit + i;
			d->currentcommit->framenum = framenum;
			d->currentcommit->numentities = 0;
		}
	}
	if (d->currentcommit == NULL)
	{
		Con_Printf("EntityFrame4_CL_ReadFrame: error while decoding frame %i: database full, reading but not storing this update\n", framenum);
		skip = true;
	}
	done = false;
	while (!done && !msg_badread)
	{
		// read the number of the modified entity
		// (gaps will be copied unmodified)
		n = (unsigned short)MSG_ReadShort();
		if (n == 0x8000)
		{
			// no more entities in this update, but we still need to copy the
			// rest of the reference entities (final gap)
			done = true;
			// read end of range number, then process normally
			n = (unsigned short)MSG_ReadShort();
		}
		// high bit means it's a remove message
		cnumber = n & 0x7FFF;
		// add one (the changed one) if not done
		stopnumber = cnumber + !done;
		// process entities in range from the last one to the changed one
		for (;enumber < stopnumber;enumber++)
		{
			if (skip)
			{
				if (enumber == cnumber && (n & 0x8000) == 0)
				{
					entity_state_t tempstate;
					EntityState_ReadFields(&tempstate, EntityState_ReadExtendBits());
				}
				continue;
			}
			// slide the current into the previous slot
			cl_entities[enumber].state_previous = cl_entities[enumber].state_current;
			// copy a new current from reference database
			cl_entities[enumber].state_current = *EntityFrame4_GetReferenceEntity(d, enumber);
			s = &cl_entities[enumber].state_current;
			// if this is the one to modify, read more data...
			if (enumber == cnumber)
			{
				if (n & 0x8000)
				{
					// simply removed
					if (developer_networkentities.integer >= 2)
						Con_Printf("entity %i: remove\n", enumber);
					*s = defaultstate;
				}
				else
				{
					// read the changes
					if (developer_networkentities.integer >= 2)
						Con_Printf("entity %i: update\n", enumber);
					s->active = true;
					EntityState_ReadFields(s, EntityState_ReadExtendBits());
				}
			}
			else if (developer_networkentities.integer >= 4)
				Con_Printf("entity %i: copy\n", enumber);
			// set the cl_entities_active flag
			cl_entities_active[enumber] = s->active;
			// set the update time
			s->time = cl.mtime[0];
			// fix the number (it gets wiped occasionally by copying from defaultstate)
			s->number = enumber;
			// check if we need to update the lerp stuff
			if (s->active)
				CL_MoveLerpEntityStates(&cl_entities[enumber]);
			// add this to the commit entry whether it is modified or not
			if (d->currentcommit)
				EntityFrame4_AddCommitEntity(d, &cl_entities[enumber].state_current);
			// print extra messages if desired
			if (developer_networkentities.integer >= 2 && cl_entities[enumber].state_current.active != cl_entities[enumber].state_previous.active)
			{
				if (cl_entities[enumber].state_current.active)
					Con_Printf("entity #%i has become active\n", enumber);
				else if (cl_entities[enumber].state_previous.active)
					Con_Printf("entity #%i has become inactive\n", enumber);
			}
		}
	}
	d->currentcommit = NULL;
	if (skip)
		EntityFrame4_ResetDatabase(d);
}

void EntityFrame4_WriteFrame(sizebuf_t *msg, entityframe4_database_t *d, int numstates, const entity_state_t *states)
{
	const entity_state_t *e, *s;
	entity_state_t inactiveentitystate;
	int i, n, startnumber;
	sizebuf_t buf;
	qbyte data[128];

	// if there isn't enough space to accomplish anything, skip it
	if (msg->cursize + 24 > msg->maxsize)
		return;

	// prepare the buffer
	memset(&buf, 0, sizeof(buf));
	buf.data = data;
	buf.maxsize = sizeof(data);

	for (i = 0;i < MAX_ENTITY_HISTORY;i++)
		if (!d->commit[i].numentities)
			break;
	// if commit buffer full, just don't bother writing an update this frame
	if (i == MAX_ENTITY_HISTORY)
		return;
	d->currentcommit = d->commit + i;

	// this state's number gets played around with later
	inactiveentitystate = defaultstate;

	d->currentcommit->numentities = 0;
	d->currentcommit->framenum = ++d->latestframenumber;
	MSG_WriteByte(msg, svc_entities);
	MSG_WriteLong(msg, d->referenceframenum);
	MSG_WriteLong(msg, d->currentcommit->framenum);
	if (developer_networkentities.integer >= 1)
	{
		Con_Printf("send svc_entities num:%i ref:%i (database: ref:%i commits:", d->currentcommit->framenum, d->referenceframenum, d->referenceframenum);
		for (i = 0;i < MAX_ENTITY_HISTORY;i++)
			if (d->commit[i].numentities)
				Con_Printf(" %i", d->commit[i].framenum);
		Con_Print(")\n");
	}
	if (d->currententitynumber >= sv.max_edicts)
		startnumber = 1;
	else
		startnumber = bound(1, d->currententitynumber, sv.max_edicts - 1);
	MSG_WriteShort(msg, startnumber);
	// reset currententitynumber so if the loop does not break it we will
	// start at beginning next frame (if it does break, it will set it)
	d->currententitynumber = 1;
	for (i = 0, n = startnumber;n < sv.max_edicts;n++)
	{
		// find the old state to delta from
		e = EntityFrame4_GetReferenceEntity(d, n);
		// prepare the buffer
		SZ_Clear(&buf);
		// entity exists, build an update (if empty there is no change)
		// find the state in the list
		for (;i < numstates && states[i].number < n;i++);
		// make the message
		s = states + i;
		if (s->number == n)
		{
			// build the update
			EntityState_WriteUpdate(s, &buf, e);
		}
		else
		{
			inactiveentitystate.number = n;
			s = &inactiveentitystate;
			if (e->active)
			{
				// entity used to exist but doesn't anymore, send remove
				MSG_WriteShort(&buf, n | 0x8000);
			}
		}
		// if the commit is full, we're done this frame
		if (msg->cursize + buf.cursize > msg->maxsize - 4)
		{
			// next frame we will continue where we left off
			break;
		}
		// add the entity to the commit
		EntityFrame4_AddCommitEntity(d, s);
		// if the message is empty, skip out now
		if (buf.cursize)
		{
			// write the message to the packet
			SZ_Write(msg, buf.data, buf.cursize);
		}
	}
	d->currententitynumber = n;

	// remove world message (invalid, and thus a good terminator)
	MSG_WriteShort(msg, 0x8000);
	// write the number of the end entity
	MSG_WriteShort(msg, d->currententitynumber);
	// just to be sure
	d->currentcommit = NULL;
}




#define E5_PROTOCOL_PRIORITYLEVELS 32

entityframe5_database_t *EntityFrame5_AllocDatabase(mempool_t *pool)
{
	entityframe5_database_t *d;
	d = Mem_Alloc(pool, sizeof(*d));
	EntityFrame5_ResetDatabase(d);
	return d;
}

void EntityFrame5_FreeDatabase(entityframe5_database_t *d)
{
	Mem_Free(d);
}

void EntityFrame5_ResetDatabase(entityframe5_database_t *d)
{
	int i;
	memset(d, 0, sizeof(*d));
	d->latestframenum = 0;
	for (i = 0;i < MAX_EDICTS;i++)
		d->states[i] = defaultstate;
}


int EntityState5_Priority(entityframe5_database_t *d, entity_state_t *view, entity_state_t *s, int changedbits, int age)
{
	int lowprecision, limit, priority;
	double distance;
	if (!changedbits)
		return 0;
	if (!s->active/* && changedbits & E5_FULLUPDATE*/)
		return E5_PROTOCOL_PRIORITYLEVELS - 1;
	// check whole attachment chain to judge relevance to player
	lowprecision = false;
	for (limit = 0;limit < 256;limit++)
	{
		if (s == view)
			return E5_PROTOCOL_PRIORITYLEVELS - 1;
		if (s->flags & RENDER_VIEWMODEL)
			return E5_PROTOCOL_PRIORITYLEVELS - 1;
		if (s->flags & RENDER_LOWPRECISION)
			lowprecision = true;
		if (!s->tagentity)
		{
			if (VectorCompare(s->origin, view->origin))
				return E5_PROTOCOL_PRIORITYLEVELS - 1;
			break;
		}
		s = d->states + s->tagentity;
	}
	if (limit >= 256)
		Con_Printf("Protocol: Runaway loop recursing tagentity links on entity %i\n", s->number);
	// it's not a viewmodel for this client
	distance = VectorDistance(view->origin, s->origin);
	priority = (E5_PROTOCOL_PRIORITYLEVELS / 2) + age - (int)(distance * (E5_PROTOCOL_PRIORITYLEVELS / 16384.0f));
	if (lowprecision)
		priority -= (E5_PROTOCOL_PRIORITYLEVELS / 4);
	//if (changedbits & E5_FULLUPDATE)
	//	priority += 4;
	//if (changedbits & (E5_ATTACHMENT | E5_MODEL | E5_FLAGS | E5_COLORMAP))
	//	priority += 4;
	return (int) bound(1, priority, E5_PROTOCOL_PRIORITYLEVELS - 1);
}

void EntityState5_WriteUpdate(int number, const entity_state_t *s, int changedbits, sizebuf_t *msg)
{
	unsigned int bits = 0;
	if (!s->active)
		MSG_WriteShort(msg, number | 0x8000);
	else
	{
		bits = changedbits;
		if ((bits & E5_ORIGIN) && (s->origin[0] < -4096 || s->origin[0] >= 4096 || s->origin[1] < -4096 || s->origin[1] >= 4096 || s->origin[2] < -4096 || s->origin[2] >= 4096))
			bits |= E5_ORIGIN32;
		if ((bits & E5_ANGLES) && !(s->flags & RENDER_LOWPRECISION))
			bits |= E5_ANGLES16;
		if ((bits & E5_MODEL) && s->modelindex >= 256)
			bits |= E5_MODEL16;
		if ((bits & E5_FRAME) && s->frame >= 256)
			bits |= E5_FRAME16;
		if (bits & E5_EFFECTS)
		{
			if (s->effects >= 65536)
				bits |= E5_EFFECTS32;
			else if (s->effects >= 256)
				bits |= E5_EFFECTS16;
		}
		if (bits >= 256)
			bits |= E5_EXTEND1;
		if (bits >= 65536)
			bits |= E5_EXTEND2;
		if (bits >= 16777216)
			bits |= E5_EXTEND3;
		MSG_WriteShort(msg, number);
		MSG_WriteByte(msg, bits & 0xFF);
		if (bits & E5_EXTEND1)
			MSG_WriteByte(msg, (bits >> 8) & 0xFF);
		if (bits & E5_EXTEND2)
			MSG_WriteByte(msg, (bits >> 16) & 0xFF);
		if (bits & E5_EXTEND3)
			MSG_WriteByte(msg, (bits >> 24) & 0xFF);
		if (bits & E5_FLAGS)
			MSG_WriteByte(msg, s->flags);
		if (bits & E5_ORIGIN)
		{
			if (bits & E5_ORIGIN32)
			{
				MSG_WriteCoord32f(msg, s->origin[0]);
				MSG_WriteCoord32f(msg, s->origin[1]);
				MSG_WriteCoord32f(msg, s->origin[2]);
			}
			else
			{
				MSG_WriteCoord13i(msg, s->origin[0]);
				MSG_WriteCoord13i(msg, s->origin[1]);
				MSG_WriteCoord13i(msg, s->origin[2]);
			}
		}
		if (bits & E5_ANGLES)
		{
			if (bits & E5_ANGLES16)
			{
				MSG_WriteAngle16i(msg, s->angles[0]);
				MSG_WriteAngle16i(msg, s->angles[1]);
				MSG_WriteAngle16i(msg, s->angles[2]);
			}
			else
			{
				MSG_WriteAngle8i(msg, s->angles[0]);
				MSG_WriteAngle8i(msg, s->angles[1]);
				MSG_WriteAngle8i(msg, s->angles[2]);
			}
		}
		if (bits & E5_MODEL)
		{
			if (bits & E5_MODEL16)
				MSG_WriteShort(msg, s->modelindex);
			else
				MSG_WriteByte(msg, s->modelindex);
		}
		if (bits & E5_FRAME)
		{
			if (bits & E5_FRAME16)
				MSG_WriteShort(msg, s->frame);
			else
				MSG_WriteByte(msg, s->frame);
		}
		if (bits & E5_SKIN)
			MSG_WriteByte(msg, s->skin);
		if (bits & E5_EFFECTS)
		{
			if (bits & E5_EFFECTS32)
				MSG_WriteLong(msg, s->effects);
			else if (bits & E5_EFFECTS16)
				MSG_WriteShort(msg, s->effects);
			else
				MSG_WriteByte(msg, s->effects);
		}
		if (bits & E5_ALPHA)
			MSG_WriteByte(msg, s->alpha);
		if (bits & E5_SCALE)
			MSG_WriteByte(msg, s->scale);
		if (bits & E5_COLORMAP)
			MSG_WriteByte(msg, s->colormap);
		if (bits & E5_ATTACHMENT)
		{
			MSG_WriteShort(msg, s->tagentity);
			MSG_WriteByte(msg, s->tagindex);
		}
		if (bits & E5_LIGHT)
		{
			MSG_WriteShort(msg, s->light[0]);
			MSG_WriteShort(msg, s->light[1]);
			MSG_WriteShort(msg, s->light[2]);
			MSG_WriteShort(msg, s->light[3]);
			MSG_WriteByte(msg, s->lightstyle);
			MSG_WriteByte(msg, s->lightpflags);
		}
		if (bits & E5_GLOW)
		{
			MSG_WriteByte(msg, s->glowsize);
			MSG_WriteByte(msg, s->glowcolor);
		}
	}
}

void EntityState5_ReadUpdate(entity_state_t *s)
{
	int bits;
	bits = MSG_ReadByte();
	if (bits & E5_EXTEND1)
	{
		bits |= MSG_ReadByte() << 8;
		if (bits & E5_EXTEND2)
		{
			bits |= MSG_ReadByte() << 16;
			if (bits & E5_EXTEND3)
				bits |= MSG_ReadByte() << 24;
		}
	}
	if (bits & E5_FULLUPDATE)
	{
		*s = defaultstate;
		s->active = true;
	}
	if (bits & E5_FLAGS)
		s->flags = MSG_ReadByte();
	if (bits & E5_ORIGIN)
	{
		if (bits & E5_ORIGIN32)
		{
			s->origin[0] = MSG_ReadCoord32f();
			s->origin[1] = MSG_ReadCoord32f();
			s->origin[2] = MSG_ReadCoord32f();
		}
		else
		{
			s->origin[0] = MSG_ReadCoord13i();
			s->origin[1] = MSG_ReadCoord13i();
			s->origin[2] = MSG_ReadCoord13i();
		}
	}
	if (bits & E5_ANGLES)
	{
		if (bits & E5_ANGLES16)
		{
			s->angles[0] = MSG_ReadAngle16i(); 
			s->angles[1] = MSG_ReadAngle16i(); 
			s->angles[2] = MSG_ReadAngle16i(); 
		}
		else
		{
			s->angles[0] = MSG_ReadAngle8i(); 
			s->angles[1] = MSG_ReadAngle8i(); 
			s->angles[2] = MSG_ReadAngle8i(); 
		}
	}
	if (bits & E5_MODEL)
	{
		if (bits & E5_MODEL16)
			s->modelindex = (unsigned short) MSG_ReadShort();
		else
			s->modelindex = MSG_ReadByte();
	}
	if (bits & E5_FRAME)
	{
		if (bits & E5_FRAME16)
			s->frame = (unsigned short) MSG_ReadShort();
		else
			s->frame = MSG_ReadByte();
	}
	if (bits & E5_SKIN)
		s->skin = MSG_ReadByte();
	if (bits & E5_EFFECTS)
	{
		if (bits & E5_EFFECTS32)
			s->effects = (unsigned int) MSG_ReadLong();
		else if (bits & E5_EFFECTS16)
			s->effects = (unsigned short) MSG_ReadShort();
		else
			s->effects = MSG_ReadByte();
	}
	if (bits & E5_ALPHA)
		s->alpha = MSG_ReadByte();
	if (bits & E5_SCALE)
		s->scale = MSG_ReadByte();
	if (bits & E5_COLORMAP)
		s->colormap = MSG_ReadByte();
	if (bits & E5_ATTACHMENT)
	{
		s->tagentity = (unsigned short) MSG_ReadShort();
		s->tagindex = MSG_ReadByte();
	}
	if (bits & E5_LIGHT)
	{
		s->light[0] = (unsigned short) MSG_ReadShort();
		s->light[1] = (unsigned short) MSG_ReadShort();
		s->light[2] = (unsigned short) MSG_ReadShort();
		s->light[3] = (unsigned short) MSG_ReadShort();
		s->lightstyle = MSG_ReadByte();
		s->lightpflags = MSG_ReadByte();
	}
	if (bits & E5_GLOW)
	{
		s->glowsize = MSG_ReadByte();
		s->glowcolor = MSG_ReadByte();
	}


	if (developer_networkentities.integer >= 2)
	{
		Con_Printf("ReadFields e%i", s->number);

		if (bits & E5_ORIGIN)
			Con_Printf(" E5_ORIGIN %f %f %f", s->origin[0], s->origin[1], s->origin[2]);
		if (bits & E5_ANGLES)
			Con_Printf(" E5_ANGLES %f %f %f", s->angles[0], s->angles[1], s->angles[2]);
		if (bits & E5_MODEL)
			Con_Printf(" E5_MODEL %i", s->modelindex);
		if (bits & E5_FRAME)
			Con_Printf(" E5_FRAME %i", s->frame);
		if (bits & E5_SKIN)
			Con_Printf(" E5_SKIN %i", s->skin);
		if (bits & E5_EFFECTS)
			Con_Printf(" E5_EFFECTS %i", s->effects);
		if (bits & E5_FLAGS)
		{
			Con_Printf(" E5_FLAGS %i (", s->flags);
			if (s->flags & RENDER_STEP)
				Con_Print(" STEP");
			if (s->flags & RENDER_GLOWTRAIL)
				Con_Print(" GLOWTRAIL");
			if (s->flags & RENDER_VIEWMODEL)
				Con_Print(" VIEWMODEL");
			if (s->flags & RENDER_EXTERIORMODEL)
				Con_Print(" EXTERIORMODEL");
			if (s->flags & RENDER_LOWPRECISION)
				Con_Print(" LOWPRECISION");
			if (s->flags & RENDER_COLORMAPPED)
				Con_Print(" COLORMAPPED");
			if (s->flags & RENDER_SHADOW)
				Con_Print(" SHADOW");
			if (s->flags & RENDER_LIGHT)
				Con_Print(" LIGHT");
			Con_Print(")");
		}
		if (bits & E5_ALPHA)
			Con_Printf(" E5_ALPHA %f", s->alpha / 255.0f);
		if (bits & E5_SCALE)
			Con_Printf(" E5_SCALE %f", s->scale / 16.0f);
		if (bits & E5_COLORMAP)
			Con_Printf(" E5_COLORMAP %i", s->colormap);
		if (bits & E5_ATTACHMENT)
			Con_Printf(" E5_ATTACHMENT e%i:%i", s->tagentity, s->tagindex);
		if (bits & E5_LIGHT)
			Con_Printf(" E5_LIGHT %i:%i:%i:%i %i:%i", s->light[0], s->light[1], s->light[2], s->light[3], s->lightstyle, s->lightpflags);
		if (bits & E5_GLOW)
			Con_Printf(" E5_GLOW %i:%i", s->glowsize * 4, s->glowcolor);
		Con_Print("\n");
	}
}

int EntityState5_DeltaBits(const entity_state_t *o, const entity_state_t *n)
{
	unsigned int bits = 0;
	if (n->active)
	{
		if (!o->active)
			bits |= E5_FULLUPDATE;
		if (!VectorCompare(o->origin, n->origin))
			bits |= E5_ORIGIN;
		if (!VectorCompare(o->angles, n->angles))
			bits |= E5_ANGLES;
		if (o->modelindex != n->modelindex)
			bits |= E5_MODEL;
		if (o->frame != n->frame)
			bits |= E5_FRAME;
		if (o->skin != n->skin)
			bits |= E5_SKIN;
		if (o->effects != n->effects)
			bits |= E5_EFFECTS;
		if (o->flags != n->flags)
			bits |= E5_FLAGS;
		if (o->alpha != n->alpha)
			bits |= E5_ALPHA;
		if (o->scale != n->scale)
			bits |= E5_SCALE;
		if (o->colormap != n->colormap)
			bits |= E5_COLORMAP;
		if (o->tagentity != n->tagentity || o->tagindex != n->tagindex)
			bits |= E5_ATTACHMENT;
		if (o->light[0] != n->light[0] || o->light[1] != n->light[1] || o->light[2] != n->light[2] || o->light[3] != n->light[3] || o->lightstyle != n->lightstyle || o->lightpflags != n->lightpflags)
			bits |= E5_LIGHT;
		if (o->glowsize != n->glowsize || o->glowcolor != n->glowcolor)
			bits |= E5_GLOW;
	}
	else
		if (o->active)
			bits |= E5_FULLUPDATE;
	return bits;
}

void EntityFrame5_CL_ReadFrame(void)
{
	int n, enumber;
	entity_t *ent;
	entity_state_t *s;
	// read the number of this frame to echo back in next input packet
	cl.latestframenum = MSG_ReadLong();
	// read entity numbers until we find a 0x8000
	// (which would be remove world entity, but is actually a terminator)
	while ((n = (unsigned short)MSG_ReadShort()) != 0x8000 && !msg_badread)
	{
		// get the entity number and look it up
		enumber = n & 0x7FFF;
		ent = cl_entities + enumber;
		// slide the current into the previous slot
		ent->state_previous = ent->state_current;
		// read the update
		s = &ent->state_current;
		if (n & 0x8000)
		{
			// remove entity
			*s = defaultstate;
		}
		else
		{
			// update entity
			EntityState5_ReadUpdate(s);
		}
		// set the cl_entities_active flag
		cl_entities_active[enumber] = s->active;
		// set the update time
		s->time = cl.mtime[0];
		// fix the number (it gets wiped occasionally by copying from defaultstate)
		s->number = enumber;
		// check if we need to update the lerp stuff
		if (s->active)
			CL_MoveLerpEntityStates(&cl_entities[enumber]);
		// print extra messages if desired
		if (developer_networkentities.integer >= 2 && cl_entities[enumber].state_current.active != cl_entities[enumber].state_previous.active)
		{
			if (cl_entities[enumber].state_current.active)
				Con_Printf("entity #%i has become active\n", enumber);
			else if (cl_entities[enumber].state_previous.active)
				Con_Printf("entity #%i has become inactive\n", enumber);
		}
	}
}

void EntityFrame5_AckFrame(entityframe5_database_t *d, int framenum, int viewentnum)
{
	int i, j, k, l, bits;
	entityframe5_changestate_t *s, *s2;
	entityframe5_packetlog_t *p, *p2;
	// scan for packets made obsolete by this ack
	for (i = 0, p = d->packetlog;i < ENTITYFRAME5_MAXPACKETLOGS;i++, p++)
	{
		// skip packets that are empty or in the future
		if (p->packetnumber == 0 || p->packetnumber > framenum)
			continue;
		// if the packetnumber matches it is deleted without any processing 
		// (since it was received).
		// if the packet number is less than this ack it was lost and its
		// important information will be repeated in this update if it is not
		// already obsolete due to a later update.
		if (p->packetnumber < framenum)
		{
			// packet was lost - merge deltabits into the main array so they
			// will be re-sent, but only if there is no newer update of that
			// bit in the logs (as those will arrive before this update)
			for (j = 0, s = p->states;j < p->numstates;j++, s++)
			{
				// check for any newer updates to this entity and mask off any
				// overlapping bits (we don't need to send something again if
				// it has already been sent more recently)
				bits = s->bits & ~d->deltabits[s->number];
				for (k = 0, p2 = d->packetlog;k < ENTITYFRAME5_MAXPACKETLOGS && bits;k++, p2++)
				{
					if (p2->packetnumber > framenum)
					{
						for (l = 0, s2 = p2->states;l < p2->numstates;l++, p2++)
						{
							if (s2->number == s->number)
							{
								bits &= ~s2->bits;
								break;
							}
						}
					}
				}
				// if the bits haven't all been cleared, there were some bits
				// lost with this packet, so set them again now
				if (bits)
				{
					d->deltabits[s->number] |= bits;
					d->priorities[s->number] = EntityState5_Priority(d, d->states + viewentnum, d->states + s->number, d->deltabits[s->number], d->latestframenum - d->updateframenum[s->number]);
				}
			}
		}
		// delete this packet log as it is now obsolete
		p->packetnumber = 0;
	}
}

int entityframe5_prioritychaincounts[E5_PROTOCOL_PRIORITYLEVELS];
unsigned short entityframe5_prioritychains[E5_PROTOCOL_PRIORITYLEVELS][ENTITYFRAME5_MAXSTATES];

void EntityFrame5_WriteFrame(sizebuf_t *msg, entityframe5_database_t *d, int numstates, const entity_state_t *states, int viewentnum)
{
	const entity_state_t *n;
	int i, num, l, framenum, packetlognumber, priority;
	sizebuf_t buf;
	qbyte data[128];
	entityframe5_packetlog_t *packetlog;

	framenum = d->latestframenum + 1;

	// prepare the buffer
	memset(&buf, 0, sizeof(buf));
	buf.data = data;
	buf.maxsize = sizeof(data);

	// detect changes in states
	num = 0;
	for (i = 0, n = states;i < numstates;i++, n++)
	{
		// mark gaps in entity numbering as removed entities
		for (;num < n->number;num++)
		{
			// if the entity used to exist, clear it
			if (CHECKPVSBIT(d->visiblebits, num))
			{
				CLEARPVSBIT(d->visiblebits, num);
				d->deltabits[num] = E5_FULLUPDATE;
				d->priorities[num] = EntityState5_Priority(d, d->states + viewentnum, d->states + num, d->deltabits[num], framenum - d->updateframenum[num]);
				d->states[num] = defaultstate;
				d->states[num].number = num;
			}
		}
		// update the entity state data
		if (!CHECKPVSBIT(d->visiblebits, num))
		{
			// entity just spawned in, don't let it completely hog priority
			// because of being ancient on the first frame
			d->updateframenum[num] = framenum;
		}
		SETPVSBIT(d->visiblebits, num);
		d->deltabits[num] |= EntityState5_DeltaBits(d->states + num, n);
		d->priorities[num] = EntityState5_Priority(d, d->states + viewentnum, d->states + num, d->deltabits[num], framenum - d->updateframenum[num]);
		d->states[num] = *n;
		d->states[num].number = num;
		// advance to next entity so the next iteration doesn't immediately remove it
		num++;
	}
	// all remaining entities are dead
	for (;num < MAX_EDICTS;num++)
	{
		if (CHECKPVSBIT(d->visiblebits, num))
		{
			CLEARPVSBIT(d->visiblebits, num);
			d->deltabits[num] = E5_FULLUPDATE;
			d->priorities[num] = EntityState5_Priority(d, d->states + viewentnum, d->states + num, d->deltabits[num], framenum - d->updateframenum[num]);
			d->states[num] = defaultstate;
			d->states[num].number = num;
		}
	}

	// build lists of entities by priority level
	memset(entityframe5_prioritychaincounts, 0, sizeof(entityframe5_prioritychaincounts));
	l = 0;
	for (num = 0;num < MAX_EDICTS;num++)
	{
		if (d->priorities[num])
		{
			l = num;
			priority = d->priorities[num];
			if (entityframe5_prioritychaincounts[priority] < ENTITYFRAME5_MAXSTATES)
				entityframe5_prioritychains[priority][entityframe5_prioritychaincounts[priority]++] = num;
		}
	}

	// return early if there are no entities to send this time
	if (l == 0)
		return;

	d->latestframenum = framenum;
	MSG_WriteByte(msg, svc_entities);
	MSG_WriteLong(msg, framenum);

	// if packet log is full, an empty update is still written
	// (otherwise the client might have nothing to ack to remove packetlogs)
	for (packetlognumber = 0, packetlog = d->packetlog;packetlognumber < ENTITYFRAME5_MAXPACKETLOGS;packetlognumber++, packetlog++)
		if (packetlog->packetnumber == 0)
			break;
	if (packetlognumber < ENTITYFRAME5_MAXPACKETLOGS)
	{
		// write to packet and log
		packetlog->packetnumber = framenum;
		packetlog->numstates = 0;
		for (priority = E5_PROTOCOL_PRIORITYLEVELS - 1;priority >= 0 && packetlog->numstates < ENTITYFRAME5_MAXSTATES;priority--)
		{
			for (i = 0;i < entityframe5_prioritychaincounts[priority] && packetlog->numstates < ENTITYFRAME5_MAXSTATES;i++)
			{
				num = entityframe5_prioritychains[priority][i];
				n = d->states + num;
				if (d->deltabits[num] & E5_FULLUPDATE)
					d->deltabits[num] = E5_FULLUPDATE | EntityState5_DeltaBits(&defaultstate, n);
				buf.cursize = 0;
				EntityState5_WriteUpdate(num, n, d->deltabits[num], &buf);
				// if the entity won't fit, try the next one
				if (msg->cursize + buf.cursize + 2 > msg->maxsize)
					continue;
				// write entity to the packet
				SZ_Write(msg, buf.data, buf.cursize);
				// mark age on entity for prioritization
				d->updateframenum[num] = framenum;
				// log entity so deltabits can be restored later if lost
				packetlog->states[packetlog->numstates].number = num;
				packetlog->states[packetlog->numstates].bits = d->deltabits[num];
				packetlog->numstates++;
				// clear deltabits and priority so it won't be sent again
				d->deltabits[num] = 0;
				d->priorities[num] = 0;
			}
		}
	}

	MSG_WriteShort(msg, 0x8000);
}

