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

#include "quakedef.h"

#define MAX_PARTICLES			16384	// default max # of particles at one time
#define ABSOLUTE_MIN_PARTICLES	512		// no fewer than this no matter what's on the command line

// LordHavoc: added dust, smoke, snow, bloodcloud, and many others
typedef enum
{
	pt_static, pt_grav, pt_slowgrav, pt_blob, pt_blob2, pt_bulletsmoke, pt_smoke, pt_snow, pt_rain, pt_bloodcloud, pt_fallfadespark, pt_bubble, pt_fade, pt_smokecloud, pt_splash, pt_flame, pt_flamingdebris, pt_smokingdebris, pt_flamefall, pt_glow, pt_decal, pt_blood, pt_bloodsplatter
}
ptype_t;

typedef struct particle_s
{
	vec3_t		org;
	float		color;
	vec3_t		vel;
	float		die;
	ptype_t		type;
	float		scale;
	rtexture_t	*tex;
	byte		dynlight; // if set the particle will be dynamically lit (if r_dynamicparticles is on), used for smoke and blood
	byte		rendermode; // a TPOLYTYPE_ value
	byte		pad1;
	byte		pad2;
	float		alpha; // 0-255
	float		time2; // used for various things (snow fluttering, for example)
	float		bounce; // how much bounce-back from a surface the particle hits (0 = no physics, 1 = stop and slide, 2 = keep bouncing forever, 1.5 is typical of bouncing particles)
	vec3_t		oldorg;
	vec3_t		vel2; // used for snow fluttering (base velocity, wind for instance)
	vec3_t		direction; // used by decals
}
particle_t;

float TraceLine (vec3_t start, vec3_t end, vec3_t impact, vec3_t normal);

int		ramp1[8] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61};
int		ramp2[8] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66};
int		ramp3[8] = {0x6d, 0x6b, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};

rtexture_t *particletexture;
rtexture_t *smokeparticletexture[8];
rtexture_t *rainparticletexture;
rtexture_t *bubbleparticletexture;

particle_t	*particles;
int			r_numparticles;

vec3_t			r_pright, r_pup, r_ppn;

int			numparticles;
particle_t	**freeparticles; // list used only in compacting particles array

cvar_t r_particles = {"r_particles", "1"};
cvar_t r_drawparticles = {"r_drawparticles", "1"};
cvar_t r_dynamicparticles = {"r_dynamicparticles", "1", true};

byte shadebubble(float dx, float dy, vec3_t light)
{
	float	dz, f, dot;
	vec3_t	normal;
	if ((dx*dx+dy*dy) < 1) // it does hit the sphere
	{
		dz = 1 - (dx*dx+dy*dy);
		f = 0;
		// back side
		normal[0] = dx;normal[1] = dy;normal[2] = dz;
		VectorNormalize(normal);
		dot = DotProduct(normal, light);
		if (dot > 0.5) // interior reflection
			f += ((dot *  2) - 1);
		else if (dot < -0.5) // exterior reflection
			f += ((dot * -2) - 1);
		// front side
		normal[0] = dx;normal[1] = dy;normal[2] = -dz;
		VectorNormalize(normal);
		dot = DotProduct(normal, light);
		if (dot > 0.5) // interior reflection
			f += ((dot *  2) - 1);
		else if (dot < -0.5) // exterior reflection
			f += ((dot * -2) - 1);
		f *= 128;
		f += 16; // just to give it a haze so you can see the outline
		f = bound(0, f, 255);
		return (byte) f;
	}
	else
		return 0;
}

void R_InitParticleTexture (void)
{
	int		x,y,d,i,m;
	float	dx, dy;
	byte	data[32][32][4], noise1[64][64], noise2[64][64];
	vec3_t	light;

	for (y = 0;y < 32;y++)
	{
		dy = y - 16;
		for (x = 0;x < 32;x++)
		{
			data[y][x][0] = data[y][x][1] = data[y][x][2] = 255;
			dx = x - 16;
			d = (255 - (dx*dx+dy*dy));
			if (d < 0) d = 0;
			data[y][x][3] = (byte) d;
		}
	}
	particletexture = R_LoadTexture ("particletexture", 32, 32, &data[0][0][0], TEXF_MIPMAP | TEXF_ALPHA | TEXF_RGBA | TEXF_PRECACHE);

	for (i = 0;i < 8;i++)
	{
		do
		{
			fractalnoise(&noise1[0][0], 64, 4);
			fractalnoise(&noise2[0][0], 64, 8);
			m = 0;
			for (y = 0;y < 32;y++)
			{
				dy = y - 16;
				for (x = 0;x < 32;x++)
				{
					int j;
					j = (noise1[y][x] - 128) * 2 + 128;
					if (j < 0) j = 0;
					if (j > 255) j = 255;
					data[y][x][0] = data[y][x][1] = data[y][x][2] = j;
					dx = x - 16;
					d = (noise2[y][x] - 128) * 4 + 128;
					if (d > 0)
					{
						d = (d * (255 - (int) (dx*dx+dy*dy))) >> 8;
						//j = (sqrt(dx*dx+dy*dy) * 2.0f - 16.0f);
						//if (j > 0)
						//	d = (d * (255 - j*j)) >> 8;
						if (d < 0) d = 0;
						if (d > 255) d = 255;
						data[y][x][3] = (byte) d;
						if (m < d)
							m = d;
					}
				}
			}
		}
		while (m < 192);

		smokeparticletexture[i] = R_LoadTexture (va("smokeparticletexture%d", i), 32, 32, &data[0][0][0], TEXF_MIPMAP | TEXF_ALPHA | TEXF_RGBA | TEXF_PRECACHE);
	}

	light[0] = 1;light[1] = 1;light[2] = 1;
	VectorNormalize(light);
	for (x=0 ; x<32 ; x++)
	{
		for (y=0 ; y<32 ; y++)
		{
			data[y][x][0] = data[y][x][1] = data[y][x][2] = 255;
			data[y][x][3] = shadebubble((x - 16) * (1.0 / 8.0), y < 24 ? (y - 24) * (1.0 / 24.0) : (y - 24) * (1.0 / 8.0), light);
		}
	}
	rainparticletexture = R_LoadTexture ("rainparticletexture", 32, 32, &data[0][0][0], TEXF_MIPMAP | TEXF_ALPHA | TEXF_RGBA | TEXF_PRECACHE);

	light[0] = 1;light[1] = 1;light[2] = 1;
	VectorNormalize(light);
	for (x=0 ; x<32 ; x++)
	{
		for (y=0 ; y<32 ; y++)
		{
			data[y][x][0] = data[y][x][1] = data[y][x][2] = 255;
			data[y][x][3] = shadebubble((x - 16) * (1.0 / 16.0), (y - 16) * (1.0 / 16.0), light);
		}
	}
	bubbleparticletexture = R_LoadTexture ("bubbleparticletexture", 32, 32, &data[0][0][0], TEXF_MIPMAP | TEXF_ALPHA | TEXF_RGBA | TEXF_PRECACHE);
}

void r_part_start()
{
	particles = (particle_t *) qmalloc(r_numparticles * sizeof(particle_t));
	freeparticles = (void *) qmalloc(r_numparticles * sizeof(particle_t *));
	numparticles = 0;
	R_InitParticleTexture ();
}

void r_part_shutdown()
{
	numparticles = 0;
	qfree(particles);
	qfree(freeparticles);
}

/*
===============
R_InitParticles
===============
*/
void R_ReadPointFile_f (void);
void R_Particles_Init (void)
{
	int		i;

	i = COM_CheckParm ("-particles");

	if (i)
	{
		r_numparticles = (int)(atoi(com_argv[i+1]));
		if (r_numparticles < ABSOLUTE_MIN_PARTICLES)
			r_numparticles = ABSOLUTE_MIN_PARTICLES;
	}
	else
	{
		r_numparticles = MAX_PARTICLES;
	}

	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);	

	Cvar_RegisterVariable (&r_particles);
	Cvar_RegisterVariable (&r_drawparticles);
	Cvar_RegisterVariable (&r_dynamicparticles);

	R_RegisterModule("R_Particles", r_part_start, r_part_shutdown);
}

//void particle(int ptype, int pcolor, int ptex, int prendermode, int plight, float pscale, float palpha, float ptime, float pbounce, float px, float py, float pz, float pvx, float pvy, float pvz)
#define particle(ptype, pcolor, ptex, prendermode, plight, pscale, palpha, ptime, pbounce, px, py, pz, pvx, pvy, pvz)\
{\
	particle_t	*part;\
	if (numparticles >= r_numparticles)\
		return;\
	part = &particles[numparticles++];\
	part->type = (ptype);\
	part->color = (pcolor);\
	part->tex = (ptex);\
	part->dynlight = (plight);\
	part->rendermode = (prendermode);\
	part->scale = (pscale);\
	part->alpha = (palpha);\
	part->die = cl.time + (ptime);\
	part->bounce = (pbounce);\
	part->org[0] = (px);\
	part->org[1] = (py);\
	part->org[2] = (pz);\
	part->vel[0] = (pvx);\
	part->vel[1] = (pvy);\
	part->vel[2] = (pvz);\
	part->time2 = 0;\
	part->vel2[0] = part->vel2[1] = part->vel2[2] = 0;\
}
#define particle2(ptype, pcolor, ptex, prendermode, plight, pscale, palpha, ptime, pbounce, pbase, poscale, pvscale)\
{\
	particle_t	*part;\
	if (numparticles >= r_numparticles)\
		return;\
	part = &particles[numparticles++];\
	part->type = (ptype);\
	part->color = (pcolor);\
	part->tex = (ptex);\
	part->dynlight = (plight);\
	part->rendermode = (prendermode);\
	part->scale = (pscale);\
	part->alpha = (palpha);\
	part->die = cl.time + (ptime);\
	part->bounce = (pbounce);\
	part->org[0] = lhrandom(-(poscale), (poscale)) + (pbase)[0];\
	part->org[1] = lhrandom(-(poscale), (poscale)) + (pbase)[1];\
	part->org[2] = lhrandom(-(poscale), (poscale)) + (pbase)[2];\
	part->vel[0] = lhrandom(-(pvscale), (pvscale));\
	part->vel[1] = lhrandom(-(pvscale), (pvscale));\
	part->vel[2] = lhrandom(-(pvscale), (pvscale));\
	part->time2 = 0;\
	part->vel2[0] = part->vel2[1] = part->vel2[2] = 0;\
}
#define particle3(ptype, pcolor, ptex, prendermode, plight, pscale, palpha, ptime, pbounce, pbase, pscalex, pscaley, pscalez, pvscalex, pvscaley, pvscalez)\
{\
	particle_t	*part;\
	if (numparticles >= r_numparticles)\
		return;\
	part = &particles[numparticles++];\
	part->type = (ptype);\
	part->color = (pcolor);\
	part->tex = (ptex);\
	part->dynlight = (plight);\
	part->rendermode = (prendermode);\
	part->scale = (pscale);\
	part->alpha = (palpha);\
	part->die = cl.time + (ptime);\
	part->bounce = (pbounce);\
	part->org[0] = lhrandom(-(pscalex), (pscalex)) + (pbase)[0];\
	part->org[1] = lhrandom(-(pscaley), (pscaley)) + (pbase)[1];\
	part->org[2] = lhrandom(-(pscalez), (pscalez)) + (pbase)[2];\
	part->vel[0] = lhrandom(-(pvscalex), (pvscalex));\
	part->vel[1] = lhrandom(-(pvscaley), (pvscaley));\
	part->vel[2] = lhrandom(-(pvscalez), (pvscalez));\
	part->time2 = 0;\
	part->vel2[0] = part->vel2[1] = part->vel2[2] = 0;\
}
#define particle4(ptype, pcolor, ptex, prendermode, plight, pscale, palpha, ptime, pbounce, px, py, pz, pvx, pvy, pvz, ptime2, pvx2, pvy2, pvz2)\
{\
	particle_t	*part;\
	if (numparticles >= r_numparticles)\
		return;\
	part = &particles[numparticles++];\
	part->type = (ptype);\
	part->color = (pcolor);\
	part->tex = (ptex);\
	part->dynlight = (plight);\
	part->rendermode = (prendermode);\
	part->scale = (pscale);\
	part->alpha = (palpha);\
	part->die = cl.time + (ptime);\
	part->bounce = (pbounce);\
	part->org[0] = (px);\
	part->org[1] = (py);\
	part->org[2] = (pz);\
	part->vel[0] = (pvx);\
	part->vel[1] = (pvy);\
	part->vel[2] = (pvz);\
	part->time2 = (ptime2);\
	part->vel2[0] = (pvx2);\
	part->vel2[1] = (pvy2);\
	part->vel2[2] = (pvz2);\
}

/*
===============
R_EntityParticles
===============
*/
void R_EntityParticles (entity_t *ent)
{
	int			i;
	float		angle;
	float		sp, sy, cp, cy;
	vec3_t		forward;
	float		dist;
	float		beamlength;
	static vec3_t avelocities[NUMVERTEXNORMALS];
	if (!r_particles.value) return; // LordHavoc: particles are optional
	
	dist = 64;
	beamlength = 16;

	if (!avelocities[0][0])
		for (i=0 ; i<NUMVERTEXNORMALS*3 ; i++)
			avelocities[0][i] = (rand()&255) * 0.01;

	for (i=0 ; i<NUMVERTEXNORMALS ; i++)
	{
		angle = cl.time * avelocities[i][0];
		sy = sin(angle);
		cy = cos(angle);
		angle = cl.time * avelocities[i][1];
		sp = sin(angle);
		cp = cos(angle);
	
		forward[0] = cp*cy;
		forward[1] = cp*sy;
		forward[2] = -sp;

		particle(pt_static, 0x6f, particletexture, TPOLYTYPE_ALPHA, false, 2, 255, 0, 0, ent->origin[0] + m_bytenormals[i][0]*dist + forward[0]*beamlength, ent->origin[1] + m_bytenormals[i][1]*dist + forward[1]*beamlength, ent->origin[2] + m_bytenormals[i][2]*dist + forward[2]*beamlength, 0, 0, 0);
	}
}


/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles (void)
{
//	int		i;
//	free_particles = &particles[0];
//	active_particles = NULL;

//	for (i=0 ;i<r_numparticles ; i++)
//		particles[i].next = &particles[i+1];
//	particles[r_numparticles-1].next = NULL;

	numparticles = 0;
}


void R_ReadPointFile_f (void)
{
	FILE	*f;
	vec3_t	org;
	int		r;
	int		c;
	char	name[MAX_OSPATH];
	
	sprintf (name,"maps/%s.pts", sv.name);

	COM_FOpenFile (name, &f, false);
	if (!f)
	{
		Con_Printf ("couldn't open %s\n", name);
		return;
	}
	
	Con_Printf ("Reading %s...\n", name);
	c = 0;
	for (;;)
	{
		r = fscanf (f,"%f %f %f\n", &org[0], &org[1], &org[2]);
		if (r != 3)
			break;
		c++;
		
		if (numparticles >= r_numparticles)
		{
			Con_Printf ("Not enough free particles\n");
			break;
		}
		particle(pt_static, (-c)&15, particletexture, TPOLYTYPE_ALPHA, false, 2, 255, 99999, 0, org[0], org[1], org[2], 0, 0, 0);
	}

	fclose (f);
	Con_Printf ("%i points read\n", c);
}

/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/
void R_ParseParticleEffect (void)
{
	vec3_t		org, dir;
	int			i, count, msgcount, color;
	
	for (i=0 ; i<3 ; i++)
		org[i] = MSG_ReadCoord ();
	for (i=0 ; i<3 ; i++)
		dir[i] = MSG_ReadChar () * (1.0/16);
	msgcount = MSG_ReadByte ();
	color = MSG_ReadByte ();

if (msgcount == 255)
	count = 1024;
else
	count = msgcount;
	
	R_RunParticleEffect (org, dir, color, count);
}
	
/*
===============
R_ParticleExplosion

===============
*/
void R_ParticleExplosion (vec3_t org, int smoke)
{
	int i;
	if (!r_particles.value) return; // LordHavoc: particles are optional

//	particle(pt_smoke, (rand()&7) + 8, smokeparticletexture[rand()&7], TPOLYTYPE_ALPHA, true, 30, 255, 2, org[0], org[1], org[2], 0, 0, 0);

	i = Mod_PointInLeaf(org, cl.worldmodel)->contents;
	if (i == CONTENTS_SLIME || i == CONTENTS_WATER)
	{
		for (i=0 ; i<128 ; i++)
			particle2(pt_bubble, (rand()&3) + 12, bubbleparticletexture, TPOLYTYPE_ADD, false, lhrandom(1, 2), 255, 2, 1.5, org, 16, 96);
	}
	else
		R_NewExplosion(org);
	/*
	else
	{
		int j;
//		int color;
		float f, forg[3], fvel[3], fvel2[3];
//		for (i = 0;i < 256;i++)
//			particle(pt_fallfadespark, ramp3[rand()%6], particletexture, TPOLYTYPE_ALPHA, false, 1.5, lhrandom(128, 255), 5, lhrandom(-16, 16) + org[0], lhrandom(-16, 16) + org[1], lhrandom(-16, 16) + org[2], lhrandom(-192, 192), lhrandom(-192, 192), lhrandom(-192, 192) + 192);
//		for (i = 0;i < 256;i++)
//			particle2(pt_fallfadespark, ramp3[rand()%6], particletexture, TPOLYTYPE_ALPHA, false, 1.5, lhrandom(128, 255), 5, org, 15, 150);
		for (i = 0;i < 32;i++)
		{
			fvel[0] = lhrandom(-150, 150);
			fvel[1] = lhrandom(-150, 150);
			fvel[2] = lhrandom(-150, 150) + 80;
//			particle(pt_flamefall, 106 + (rand()%6), particletexture, TPOLYTYPE_ALPHA, false, 3, 255, 5, forg[0] + lhrandom(-5, 5), forg[1] + lhrandom(-5, 5), forg[2] + lhrandom(-5, 5), fvel2[0], fvel2[1], fvel2[2]);
			for (j = 0;j < 64;j++)
			{
				forg[0] = lhrandom(-20, 20) + org[0];
				forg[1] = lhrandom(-20, 20) + org[1];
				forg[2] = lhrandom(-20, 20) + org[2];
				fvel2[0] = fvel[0] + lhrandom(-30, 30);
				fvel2[1] = fvel[1] + lhrandom(-30, 30);
				fvel2[2] = fvel[2] + lhrandom(-30, 30);
				f = lhrandom(0.2, 1);
				fvel2[0] *= f;
				fvel2[1] *= f;
				fvel2[2] *= f;
				particle(pt_flamefall, 106 + (rand()%6), particletexture, TPOLYTYPE_ALPHA, false, 5, lhrandom(96, 192), 5, forg[0], forg[1], forg[2], fvel2[0], fvel2[1], fvel2[2]);
			}
		}
//		for (i = 0;i < 16;i++)
//			particle2(pt_smoke, 12+(rand()&3), smokeparticletexture[rand()&7], TPOLYTYPE_ALPHA, true, 20, 192, 99, org, 20, 0);
//		for (i = 0;i < 50;i++)
//			particle2(pt_flamingdebris, ramp3[rand()%6], particletexture, TPOLYTYPE_ALPHA, false, 3, 255, 99, org, 10, 200);
//		for (i = 0;i < 30;i++)
//			particle2(pt_smokingdebris, 10 + (rand()%6), particletexture, TPOLYTYPE_ALPHA, false, 2, 255, 99, org, 10, 100);
	}
	*/
}

/*
===============
R_ParticleExplosion2

===============
*/
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength)
{
	int			i;
	if (!r_particles.value) return; // LordHavoc: particles are optional

	for (i = 0;i < 512;i++)
		particle2(pt_fade, colorStart + (i % colorLength), particletexture, TPOLYTYPE_ALPHA, false, 1.5, 255, 0.3, 0, org, 8, 192);
}

/*
===============
R_BlobExplosion

===============
*/
void R_BlobExplosion (vec3_t org)
{
	int			i;
	if (!r_particles.value) return; // LordHavoc: particles are optional
	
	for (i=0 ; i<512 ; i++)
		particle3(pt_blob, 66+(rand()%6), particletexture, TPOLYTYPE_ALPHA, false, 2, 255, lhrandom(1, 1.4), 0, org, 16, 16, 16, 4, 4, 128);
	for (i=0 ; i<512 ; i++)
		particle3(pt_blob2, 150+(rand()%6), particletexture, TPOLYTYPE_ALPHA, false, 2, 255, lhrandom(1, 1.4), 0, org, 16, 16, 16, 4, 4, 128);
}

/*
===============
R_RunParticleEffect

===============
*/
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
	if (!r_particles.value) return; // LordHavoc: particles are optional
	
	if (count == 1024)
	{
		R_ParticleExplosion(org, false);
		return;
	}
	while (count--)
		particle2(pt_fade, color + (rand()&7), particletexture, TPOLYTYPE_ALPHA, false, 1, 128, 1, 0, org, 8, 15);
}

// LordHavoc: added this for spawning sparks/dust (which have strong gravity)
/*
===============
R_SparkShower
===============
*/
void R_SparkShower (vec3_t org, vec3_t dir, int count)
{
	if (!r_particles.value) return; // LordHavoc: particles are optional

	// smoke puff
	particle(pt_bulletsmoke, 12+(rand()&3), smokeparticletexture[rand()&7], TPOLYTYPE_ALPHA, true, 1, 160, 99, 0, org[0], org[1], org[2], lhrandom(-4, 4), lhrandom(-4, 4), 16);
	// sparks
	while(count--)
		particle(pt_fallfadespark, ramp3[rand()%6], particletexture, TPOLYTYPE_ADD, false, 1, lhrandom(0, 255), 1.5, 1.5, org[0], org[1], org[2], lhrandom(-64, 64), lhrandom(-64, 64), lhrandom(-64, 64) + 128);
}

void R_BloodPuff (vec3_t org, vec3_t vel, int count)
{
	if (!r_particles.value) return; // LordHavoc: particles are optional

	if (count > 100)
		count = 100;
	while(count > 0)
	{
		particle(pt_bloodsplatter, 68+(rand()&3), smokeparticletexture[rand()&7], TPOLYTYPE_ALPHA, true, lhrandom(10, 20), min(count, 10) * 25 + 5, 99, -1, org[0], org[1], org[2], vel[0] + lhrandom(-64, 64), vel[1] + lhrandom(-64, 64), vel[2] + lhrandom(-64, 64));
		count -= 10;
	}
}

void R_BloodShower (vec3_t mins, vec3_t maxs, float velspeed, int count)
{
	vec3_t		diff;
	vec3_t		center;
	vec3_t		velscale;
	if (!r_particles.value) return; // LordHavoc: particles are optional

	VectorSubtract(maxs, mins, diff);
	center[0] = (mins[0] + maxs[0]) * 0.5;
	center[1] = (mins[1] + maxs[1]) * 0.5;
	center[2] = (mins[2] + maxs[2]) * 0.5;
	// FIXME: change velspeed back to 2.0x after fixing mod
	velscale[0] = velspeed * 2.0 / diff[0];
	velscale[1] = velspeed * 2.0 / diff[1];
	velscale[2] = velspeed * 2.0 / diff[2];
	
	while (count--)
	{
		vec3_t org, vel;
		org[0] = lhrandom(mins[0], maxs[0]);
		org[1] = lhrandom(mins[1], maxs[1]);
		org[2] = lhrandom(mins[2], maxs[2]);
		vel[0] = (org[0] - center[0]) * velscale[0];
		vel[1] = (org[1] - center[1]) * velscale[1];
		vel[2] = (org[2] - center[2]) * velscale[2];
		particle(pt_bloodsplatter, 68+(rand()&3), smokeparticletexture[rand()&7], TPOLYTYPE_ALPHA, true, lhrandom(10, 25), 255, 99, -1, org[0], org[1], org[2], vel[0], vel[1], vel[2]);
	}
}

void R_ParticleCube (vec3_t mins, vec3_t maxs, vec3_t dir, int count, int colorbase, int gravity, int randomvel)
{
	float		t;
	if (!r_particles.value) return; // LordHavoc: particles are optional
	if (maxs[0] <= mins[0]) {t = mins[0];mins[0] = maxs[0];maxs[0] = t;}
	if (maxs[1] <= mins[1]) {t = mins[1];mins[1] = maxs[1];maxs[1] = t;}
	if (maxs[2] <= mins[2]) {t = mins[2];mins[2] = maxs[2];maxs[2] = t;}

	while (count--)
		particle(gravity ? pt_grav : pt_static, colorbase + (rand()&3), particletexture, TPOLYTYPE_ALPHA, false, 2, 255, lhrandom(1, 2), 0, lhrandom(mins[0], maxs[0]), lhrandom(mins[1], maxs[1]), lhrandom(mins[2], maxs[2]), dir[0] + lhrandom(-randomvel, randomvel), dir[1] + lhrandom(-randomvel, randomvel), dir[2] + lhrandom(-randomvel, randomvel));
}

void R_ParticleRain (vec3_t mins, vec3_t maxs, vec3_t dir, int count, int colorbase, int type)
{
	vec3_t		vel;
	float		t, z;
	if (!r_particles.value) return; // LordHavoc: particles are optional
	if (maxs[0] <= mins[0]) {t = mins[0];mins[0] = maxs[0];maxs[0] = t;}
	if (maxs[1] <= mins[1]) {t = mins[1];mins[1] = maxs[1];maxs[1] = t;}
	if (maxs[2] <= mins[2]) {t = mins[2];mins[2] = maxs[2];maxs[2] = t;}
	if (dir[2] < 0) // falling
	{
		t = (maxs[2] - mins[2]) / -dir[2];
		z = maxs[2];
	}
	else // rising??
	{
		t = (maxs[2] - mins[2]) / dir[2];
		z = mins[2];
	}
	if (t < 0 || t > 2) // sanity check
		t = 2;

	switch(type)
	{
	case 0:
		while(count--)
		{
			vel[0] = dir[0] + lhrandom(-16, 16);
			vel[1] = dir[1] + lhrandom(-16, 16);
			vel[2] = dir[2] + lhrandom(-32, 32);
			particle4(pt_rain, colorbase + (rand()&3), rainparticletexture, TPOLYTYPE_ALPHA, true, 3, 255, t, 0, lhrandom(mins[0], maxs[0]), lhrandom(mins[1], maxs[1]), z, vel[0], vel[1], vel[2], 0, vel[0], vel[1], vel[2]);
		}
		break;
	case 1:
		while(count--)
		{
			vel[0] = dir[0] + lhrandom(-16, 16);
			vel[1] = dir[1] + lhrandom(-16, 16);
			vel[2] = dir[2] + lhrandom(-32, 32);
			particle4(pt_snow, colorbase + (rand()&3), particletexture, TPOLYTYPE_ALPHA, false, 2, 255, t, 0, lhrandom(mins[0], maxs[0]), lhrandom(mins[1], maxs[1]), z, vel[0], vel[1], vel[2], 0, vel[0], vel[1], vel[2]);
		}
		break;
	default:
		Host_Error("R_ParticleRain: unknown type %i (0 = rain, 1 = snow)\n", type);
	}
}

void R_FlameCube (vec3_t mins, vec3_t maxs, int count)
{
	float		t;
	if (!r_particles.value) return; // LordHavoc: particles are optional
	if (maxs[0] <= mins[0]) {t = mins[0];mins[0] = maxs[0];maxs[0] = t;}
	if (maxs[1] <= mins[1]) {t = mins[1];mins[1] = maxs[1];maxs[1] = t;}
	if (maxs[2] <= mins[2]) {t = mins[2];mins[2] = maxs[2];maxs[2] = t;}

	while (count--)
		particle(pt_flame, 224 + (rand()&15), smokeparticletexture[rand()&7], TPOLYTYPE_ADD, false, 1, lhrandom(64, 255), 5, 0, lhrandom(mins[0], maxs[0]), lhrandom(mins[1], maxs[1]), lhrandom(mins[2], maxs[2]), lhrandom(-16, 16), lhrandom(-16, 16), lhrandom(-16, 48));
}

void R_Flames (vec3_t org, vec3_t vel, int count)
{
	if (!r_particles.value) return; // LordHavoc: particles are optional

	while (count--)
		particle(pt_flame, 224 + (rand()&15), smokeparticletexture[rand()&7], TPOLYTYPE_ADD, false, 1, lhrandom(64, 255), 5, 1.5, org[0], org[1], org[2], vel[0] + lhrandom(-16, 16), vel[1] + lhrandom(-16, 16), vel[2] + lhrandom(-16, 16));
}



/*
===============
R_LavaSplash

===============
*/
void R_LavaSplash (vec3_t origin)
{
	int			i, j;
	float		vel;
	vec3_t		dir, org;
	if (!r_particles.value) return; // LordHavoc: particles are optional

	for (i=-128 ; i<128 ; i+=8)
	{
		for (j=-128 ; j<128 ; j+=8)
		{
			dir[0] = j + lhrandom(0, 8);
			dir[1] = i + lhrandom(0, 8);
			dir[2] = 256;
			org[0] = origin[0] + dir[0];
			org[1] = origin[1] + dir[1];
			org[2] = origin[2] + lhrandom(0, 64);
			vel = lhrandom(50, 120) / VectorLength(dir); // normalize and scale
			particle(pt_slowgrav, 224 + (rand()&7), particletexture, TPOLYTYPE_ALPHA, false, 3, 128, lhrandom(2, 2.5), 0, org[0], org[1], org[2], dir[0] * vel, dir[1] * vel, dir[2] * vel);
//			particle(pt_slowgrav, 224 + (rand()&7), particletexture, TPOLYTYPE_ALPHA, false, 3, 128, lhrandom(2, 2.5), 0, origin[0] + i, origin[1] + j, origin[2] + lhrandom(0, 63), i * lhrandom(0.125, 0.25), j * lhrandom(0.125, 0.25), lhrandom(64, 128));
		}
	}
}

/*
===============
R_TeleportSplash

===============
*/
void R_TeleportSplash (vec3_t org)
{
	int			i, j, k;
	if (!r_particles.value) return; // LordHavoc: particles are optional

	for (i=-16 ; i<16 ; i+=8)
		for (j=-16 ; j<16 ; j+=8)
			for (k=-24 ; k<32 ; k+=8)
				particle(pt_fade, 254, particletexture, TPOLYTYPE_ADD, false, 1, lhrandom(64, 128), 5, 0, org[0] + i + lhrandom(0, 8), org[1] + j + lhrandom(0, 8), org[2] + k + lhrandom(0, 8), i*2 + lhrandom(-12.5, 12.5), j*2 + lhrandom(-12.5, 12.5), k*2 + lhrandom(27.5, 52.5));
}

void R_RocketTrail (vec3_t start, vec3_t end, int type, entity_t *ent)
{
	vec3_t		vec, dir, vel;
	float		len, dec = 0, speed;
	int			contents, bubbles, polytype;
	double		t;
	if (!r_particles.value) return; // LordHavoc: particles are optional

	VectorSubtract(end, start, dir);
	VectorNormalize(dir);

	/*
	if (type == 0) // rocket glow
		particle(pt_glow, 254, particletexture, TPOLYTYPE_ADD, false, 10, 160, 999, 0, start[0] - 12 * dir[0], start[1] - 12 * dir[1], start[2] - 12 * dir[2], 0, 0, 0);
	*/

	t = ent->trail_time;
	if (t >= cl.time)
		return; // no particles to spawn this frame (sparse trail)

	if (t < cl.oldtime)
		t = cl.oldtime;

	VectorSubtract (end, start, vec);
	len = VectorNormalizeLength (vec);
	if (len <= 0.01f)
	{
		// advance the trail time
		ent->trail_time = cl.time;
		return;
	}
	speed = len / (cl.time - cl.oldtime);
	VectorScale(vec, speed, vel);

	// advance into this frame to reach the first puff location
	dec = t - cl.oldtime;
	dec *= speed;
	VectorMA(start, dec, vec, start);

	contents = Mod_PointInLeaf(start, cl.worldmodel)->contents;
	if (contents == CONTENTS_SKY || contents == CONTENTS_LAVA)
	{
		// advance the trail time
		ent->trail_time = cl.time;
		return;
	}

	bubbles = (contents == CONTENTS_WATER || contents == CONTENTS_SLIME);

	polytype = TPOLYTYPE_ALPHA;
	if (ent->effects & EF_ADDITIVE)
		polytype = TPOLYTYPE_ADD;

	while (t < cl.time)
	{
		switch (type)
		{
			case 0:	// rocket trail
				if (bubbles)
				{
					dec = 0.01f;
					particle(pt_bubble, 254, bubbleparticletexture, polytype, false, lhrandom(1, 2), 255, 2, 1.5, start[0], start[1], start[2], lhrandom(-16, 16), lhrandom(-16, 16), lhrandom(-16, 16));
				}
				else
				{
					dec = 0.01f;
					particle(pt_smoke, 254, smokeparticletexture[rand()&7], polytype, true, 2, 160, 9999, 0, start[0], start[1], start[2], 0, 0, 16);
					if (type == 0)
					{
						particle(pt_fallfadespark, 0x68 + (rand() & 7), particletexture, TPOLYTYPE_ADD, false, 1, lhrandom(128, 255), 5, 0, start[0], start[1], start[2], lhrandom(-64, 64) - vel[0] * 0.25, lhrandom(-64, 64) - vel[1] * 0.25, lhrandom(-64, 64) - vel[2] * 0.25);
						particle(pt_fallfadespark, 0x68 + (rand() & 7), particletexture, TPOLYTYPE_ADD, false, 1, lhrandom(128, 255), 5, 0, start[0], start[1], start[2], lhrandom(-64, 64) - vel[0] * 0.25, lhrandom(-64, 64) - vel[1] * 0.25, lhrandom(-64, 64) - vel[2] * 0.25);
//						particle(pt_fallfadespark, 0x68 + (rand() & 7), particletexture, TPOLYTYPE_ADD, false, 1, lhrandom(128, 255), 5, 0, start[0], start[1], start[2], lhrandom(-64, 64) - vel[0] * 0.25, lhrandom(-64, 64) - vel[1] * 0.25, lhrandom(-64, 64) - vel[2] * 0.25);
//						particle(pt_fallfadespark, 0x68 + (rand() & 7), particletexture, TPOLYTYPE_ADD, false, 1, lhrandom(128, 255), 5, 0, start[0], start[1], start[2], lhrandom(-64, 64) - vel[0] * 0.25, lhrandom(-64, 64) - vel[1] * 0.25, lhrandom(-64, 64) - vel[2] * 0.25);
					}
				}
				break;

			case 1: // grenade trail
				// FIXME: make it gradually stop smoking
				if (bubbles)
				{
					dec = 0.02f;
					particle(pt_bubble, 254, bubbleparticletexture, polytype, false, lhrandom(1, 2), 255, 2, 1.5, start[0], start[1], start[2], lhrandom(-16, 16), lhrandom(-16, 16), lhrandom(-16, 16));
				}
				else
				{
					dec = 0.02f;
					particle(pt_smoke, 6, smokeparticletexture[rand()&7], polytype, true, 2, 160, 9999, 0, start[0], start[1], start[2], 0, 0, 16);
				}
				break;


			case 2:	// blood
				dec = 0.025f;
				particle(pt_bloodsplatter, 67+(rand()&3), smokeparticletexture[rand()&7], polytype, true, lhrandom(5, 20), 255, 9999, -1, start[0], start[1], start[2], vel[0] + lhrandom(-64, 64), vel[1] + lhrandom(-64, 64), vel[2] + lhrandom(-64, 64));
				break;

			case 4:	// slight blood
				dec = 0.025f;
				particle(pt_bloodsplatter, 67+(rand()&3), smokeparticletexture[rand()&7], polytype, true, lhrandom(5, 20), 192, 9999, -1, start[0], start[1], start[2], vel[0] + lhrandom(-64, 64), vel[1] + lhrandom(-64, 64), vel[2] + lhrandom(-64, 64));
				break;

			case 3:	// green tracer
				dec = 0.02f;
				particle(pt_fade,  56, smokeparticletexture[rand()&7], polytype, false, 4, 255, 9999, 0, start[0], start[1], start[2], 0, 0, 0);
				break;

			case 5:	// flame tracer
				dec = 0.02f;
				particle(pt_fade, 234, smokeparticletexture[rand()&7], polytype, false, 4, 255, 9999, 0, start[0], start[1], start[2], 0, 0, 0);
				break;

			case 6:	// voor trail
				dec = 0.05f; // sparse trail
				particle(pt_fade, 152 + (rand()&3), smokeparticletexture[rand()&7], polytype, false, 4, 255, 9999, 0, start[0], start[1], start[2], 0, 0, 0);
				break;

			case 7:	// Nehahra smoke tracer
				dec = 0.14f;
				particle(pt_smoke, 12, smokeparticletexture[rand()&7], polytype, true, 10, 64, 9999, 0, start[0], start[1], start[2], 0, 0, 0);
				break;
		}
		
		// advance to next time and position
		t += dec;
		dec *= speed;
		VectorMA (start, dec, vec, start);
	}
	ent->trail_time = t;
}

void R_RocketTrail2 (vec3_t start, vec3_t end, int color, entity_t *ent)
{
	vec3_t		vec;
	int			len;
	if (!r_particles.value) return; // LordHavoc: particles are optional

	VectorSubtract (end, start, vec);
	len = (int) (VectorNormalizeLength (vec) * (1.0f / 3.0f));
	VectorScale(vec, 3, vec);
	while (len--)
	{
		particle(pt_smoke, color, particletexture, TPOLYTYPE_ALPHA, false, 8, 192, 99, 0, start[0], start[1], start[2], 0, 0, 0);
		VectorAdd (start, vec, start);
	}
}


/*
===============
R_DrawParticles
===============
*/
extern	cvar_t	sv_gravity;

void R_MoveParticles (void)
{
	particle_t		*p;
	int				i, activeparticles, maxparticle, j, a;
	vec3_t			v;
	float			gravity, dvel, frametime;

	// LordHavoc: early out condition
	if (!numparticles)
		return;

	frametime = cl.time - cl.oldtime;
	gravity = frametime * sv_gravity.value;
	dvel = 1+4*frametime;

	activeparticles = 0;
	maxparticle = -1;
	j = 0;
	for (i = 0, p = particles;i < numparticles;i++, p++)
	{
		if (p->die < cl.time)
		{
			freeparticles[j++] = p;
			continue;
		}

		VectorCopy(p->org, p->oldorg);
		p->org[0] += p->vel[0]*frametime;
		p->org[1] += p->vel[1]*frametime;
		p->org[2] += p->vel[2]*frametime;
		if (p->bounce)
		{
			vec3_t normal;
			float dist;
			if (TraceLine(p->oldorg, p->org, v, normal) < 1)
			{
				VectorCopy(v, p->org);
				if (p->bounce < 0)
				{
					VectorClear(p->vel);
					p->type = pt_decal;
					// have to negate the direction (why?)
					VectorNegate(normal, p->direction);
					p->bounce = 0;
					p->time2 = cl.time + 30;
				}
				else
				{
					dist = DotProduct(p->vel, normal) * -p->bounce;
					VectorMAQuick(p->vel, dist, normal, p->vel);
					if (DotProduct(p->vel, p->vel) < 0.03)
					{
						VectorClear(p->vel);
						// hack - world is static, therefore there won't be any moving or disappearing surfaces to worry about
						//p->bounce = 0;
					}
				}
			}
		}
		
		switch (p->type)
		{
		case pt_static:
			break;

			// LordHavoc: drop-through because of shared code
		case pt_blob:
			p->vel[2] *= dvel;
		case pt_blob2:
			p->vel[0] *= dvel;
			p->vel[1] *= dvel;
			break;

		case pt_grav:
			p->vel[2] -= gravity;
			break;
		case pt_slowgrav:
			p->vel[2] -= gravity * 0.05;
			break;
		case pt_snow:
			if (cl.time > p->time2)
			{
				p->time2 = cl.time + (rand() & 3) * 0.1;
				p->vel[0] = (rand()&63)-32 + p->vel2[0];
				p->vel[1] = (rand()&63)-32 + p->vel2[1];
				p->vel[2] = (rand()&63)-32 + p->vel2[2];
			}
			a = Mod_PointInLeaf(p->org, cl.worldmodel)->contents;
			if (a != CONTENTS_EMPTY && a != CONTENTS_SKY)
			{
				vec3_t normal;
				if (a == CONTENTS_SOLID && Mod_PointInLeaf(p->oldorg, cl.worldmodel)->contents == CONTENTS_SOLID)
					break; // still in solid
				p->die = cl.time + 1000;
				p->vel[0] = p->vel[1] = p->vel[2] = 0;
				switch (a)
				{
				case CONTENTS_LAVA:
				case CONTENTS_SLIME:
					p->tex = smokeparticletexture[rand()&7];
					p->type = pt_smokecloud;
					p->alpha = 96;
					p->scale = 5;
					p->vel[2] = 96;
					break;
				case CONTENTS_WATER:
					p->tex = smokeparticletexture[rand()&7];
					p->type = pt_splash;
					p->alpha = 96;
					p->scale = 5;
					p->vel[2] = 96;
					break;
				default: // CONTENTS_SOLID and any others
					TraceLine(p->oldorg, p->org, v, normal);
					VectorCopy(v, p->org);
					p->tex = smokeparticletexture[rand()&7];
					p->type = pt_fade;
					VectorClear(p->vel);
					break;
				}
			}
			break;
		case pt_bloodcloud:
//			if (Mod_PointInLeaf(p->org, cl.worldmodel)->contents != CONTENTS_EMPTY)
//			{
//				p->die = -1;
//				break;
//			}
			p->scale += frametime * 16;
			p->alpha -= frametime * 256;
			p->vel[2] -= gravity * 0.25;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_blood:
//			if (Mod_PointInLeaf(p->org, cl.worldmodel)->contents != CONTENTS_EMPTY)
//			{
//				p->die = -1;
//				break;
//			}
			p->scale += frametime * 16;
			p->alpha -= frametime * 512;
			p->vel[2] -= gravity * 0.25;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_bloodsplatter:
//			if (Mod_PointInLeaf(p->org, cl.worldmodel)->contents != CONTENTS_EMPTY)
//			{
//				p->die = -1;
//				break;
//			}
			p->alpha -= frametime * 128;
			if (p->alpha < 1)
				p->die = -1;
			p->vel[2] -= gravity * 0.5;
			break;
		case pt_fallfadespark:
			p->alpha -= frametime * 256;
			p->vel[2] -= gravity;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_fade:
			p->alpha -= frametime * 512;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_bubble:
			a = Mod_PointInLeaf(p->org, cl.worldmodel)->contents;
			if (a != CONTENTS_WATER && a != CONTENTS_SLIME)
			{
				p->tex = smokeparticletexture[rand()&7];
				p->type = pt_splash;
				p->alpha = 96;
				p->scale = 5;
				p->vel[0] = p->vel[1] = p->vel[2] = 0;
				p->die = cl.time + 1000;
//				p->die = -1;
			}
			p->vel[2] += gravity * 0.25;
			p->vel[0] *= (1 - (frametime * 0.0625));
			p->vel[1] *= (1 - (frametime * 0.0625));
			p->vel[2] *= (1 - (frametime * 0.0625));
			if (cl.time > p->time2)
			{
				p->time2 = cl.time + lhrandom(0, 0.5);
				p->vel[0] += lhrandom(-32,32);
				p->vel[1] += lhrandom(-32,32);
				p->vel[2] += lhrandom(-32,32);
			}
			p->alpha -= frametime * 64;
			if (p->alpha < 1)
				p->die = -1;
			if (p->alpha < 1)
				p->die = -1;
			break;
// LordHavoc: for smoke trails
		case pt_bulletsmoke:
			p->scale += frametime * 60;
			p->alpha -= frametime * 512;
			p->vel[2] += gravity * 0.05;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_smoke:
			p->scale += frametime * 20;
			p->alpha -= frametime * 256;
			p->vel[2] += gravity * 0.05;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_smokecloud:
			p->scale += frametime * 64;
			p->alpha -= frametime * 256;
			p->vel[2] += gravity * 0.05;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_splash:
			p->scale += frametime * 24;
			p->alpha -= frametime * 512;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_rain:
			a = Mod_PointInLeaf(p->org, cl.worldmodel)->contents;
			if (a != CONTENTS_EMPTY && a != CONTENTS_SKY)
			{
				vec3_t normal;
				if (a == CONTENTS_SOLID && Mod_PointInLeaf(p->oldorg, cl.worldmodel)->contents == CONTENTS_SOLID)
					break; // still in solid
				p->die = cl.time + 1000;
				p->vel[0] = p->vel[1] = p->vel[2] = 0;
				switch (a)
				{
				case CONTENTS_LAVA:
				case CONTENTS_SLIME:
					p->tex = smokeparticletexture[rand()&7];
					p->type = pt_smokecloud;
					p->alpha = 96;
					p->scale = 5;
					p->vel[2] = 96;
					break;
				case CONTENTS_WATER:
					p->tex = smokeparticletexture[rand()&7];
					p->type = pt_splash;
					p->alpha = 96;
					p->scale = 5;
					break;
				default: // CONTENTS_SOLID and any others
					TraceLine(p->oldorg, p->org, v, normal);
					VectorCopy(v, p->org);
					p->tex = smokeparticletexture[rand()&7];
					p->type = pt_splash;
					p->alpha = 96;
					p->scale = 5;
					particle(pt_fallfadespark, 245, particletexture, TPOLYTYPE_ADD, false, 1,  64, 1, 1.3, p->org[0], p->org[1], p->org[2] + 1, lhrandom(-32, 32), lhrandom(-32, 32), lhrandom(-32, 32) + 48);
					particle(pt_fallfadespark, 245, particletexture, TPOLYTYPE_ADD, false, 1, 128, 1, 1.3, p->org[0], p->org[1], p->org[2] + 1, lhrandom(-32, 32), lhrandom(-32, 32), lhrandom(-32, 32) + 48);
					particle(pt_fallfadespark, 245, particletexture, TPOLYTYPE_ADD, false, 1, 192, 1, 1.3, p->org[0], p->org[1], p->org[2] + 1, lhrandom(-32, 32), lhrandom(-32, 32), lhrandom(-32, 32) + 48);
					particle(pt_fallfadespark, 245, particletexture, TPOLYTYPE_ADD, false, 1, 255, 1, 1.3, p->org[0], p->org[1], p->org[2] + 1, lhrandom(-32, 32), lhrandom(-32, 32), lhrandom(-32, 32) + 48);
					break;
				}
			}
			break;
		case pt_flame:
			p->alpha -= frametime * 512;
			p->vel[2] += gravity * 0.2;
			if (p->alpha < 1)
				p->die = -1;
			break;
			/*
		case pt_flamingdebris:
			if (cl.time >= p->time2)
			{
				p->time2 = cl.time + 0.01;
				particle2(pt_flame, p->color, particletexture, TPOLYTYPE_ADD, false, 4, p->alpha, 999, 0, p->org, 0, 50);
			}
			p->alpha -= frametime * 512;
			p->vel[2] -= gravity * 0.5f;
			if (Mod_PointInLeaf(p->org, cl.worldmodel)->contents != CONTENTS_EMPTY)
				p->die = -1;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_smokingdebris:
			if (cl.time >= p->time2)
			{
				p->time2 = cl.time + 0.01;
				particle2(pt_flame, 15, smokeparticletexture[rand()&7], TPOLYTYPE_ALPHA, false, 4, p->alpha, 999, 0, p->org, 0, 50);
			}
			p->alpha -= frametime * 512;
			p->vel[2] -= gravity * 0.5f;
			if (Mod_PointInLeaf(p->org, cl.worldmodel)->contents != CONTENTS_EMPTY)
				p->die = -1;
			if (p->alpha < 1)
				p->die = -1;
			break;
		case pt_flamefall:
			p->alpha -= frametime * 512;
			p->vel[2] -= gravity * 0.5f;
			if (p->alpha < 1)
				p->die = -1;
			break;
			*/
		case pt_glow:
			if (p->time2)
				p->die = -1;
			p->time2 = 1;
			break;
		case pt_decal:
			if (cl.time > p->time2)
			{
				p->alpha -= frametime * 256;
				if (p->alpha < 1)
					p->die = -1;
			}
			if (p->alpha < 64)
				p->die = -1;
			break;
		default:
			break;
		}

		// LordHavoc: immediate removal of unnecessary particles (must be done to ensure compactor below operates properly in all cases)
		if (p->die < cl.time)
			freeparticles[j++] = p;
		else
		{
			maxparticle = i;
			activeparticles++;
		}
	}
	// fill in gaps to compact the array
	i = 0;
	while (maxparticle >= activeparticles)
	{
		*freeparticles[i++] = particles[maxparticle--];
		while (maxparticle >= activeparticles && particles[maxparticle].die < cl.time)
			maxparticle--;
	}
	numparticles = activeparticles;
}

void R_DrawParticles (void)
{
	particle_t		*p;
	int				i, r,g,b,a, dynlight;
	float			scale, scale2, minparticledist;
	byte			*color24;
	vec3_t			up, right, uprightangles, up2, right2, tempcolor, corner, decalright, decalup, v;

	// LordHavoc: early out condition
	if ((!numparticles) || (!r_drawparticles.value))
		return;

	dynlight = r_dynamicparticles.value;
	if (!r_dynamic.value)
		dynlight = 0;
	c_particles += numparticles;

	VectorScale (vup, 1.5, up);
	VectorScale (vright, 1.5, right);

	uprightangles[0] = 0;
	uprightangles[1] = r_refdef.viewangles[1];
	uprightangles[2] = 0;
	AngleVectors (uprightangles, NULL, right2, up2);

	minparticledist = DotProduct(r_refdef.vieworg, vpn) + 16.0f;

	for (i = 0, p = particles;i < numparticles;i++, p++)
	{
		// LordHavoc: unnecessary (array was already compacted)
//		if (p->die < cl.time)
//			continue;

		if (p->type == pt_decal)
		{
			VectorSubtract(p->org, r_refdef.vieworg, v);
			if (DotProduct(p->direction, v) < 0)
				continue;
		}

		// LordHavoc: only render if not too close
		if (DotProduct(p->org, vpn) < minparticledist)
			continue;

		color24 = (byte *) &d_8to24table[(int)p->color];
		r = color24[0];
		g = color24[1];
		b = color24[2];
		a = p->alpha;
		if (dynlight && (p->dynlight || dynlight >= 2)) // LordHavoc: only light blood and smoke
		{
			R_CompleteLightPoint(tempcolor, p->org);
			r = (r * (int) tempcolor[0]) >> 7;
			g = (g * (int) tempcolor[1]) >> 7;
			b = (b * (int) tempcolor[2]) >> 7;
		}
		transpolybegin(R_GetTexture(p->tex), 0, R_GetTexture(p->tex), p->rendermode);
		scale = p->scale * -0.5;scale2 = p->scale;
		if (p->type == pt_decal)
		{
			VectorVectors(p->direction, decalright, decalup);
			corner[0] = p->org[0] + decalup[0]*scale + decalright[0]*scale;
			corner[1] = p->org[1] + decalup[1]*scale + decalright[1]*scale;
			corner[2] = p->org[2] + decalup[2]*scale + decalright[2]*scale;
			transpolyvert(corner[0]                                           , corner[1]                                           , corner[2]                                           , 0,1,r,g,b,a);
			transpolyvert(corner[0] + decalup[0]*scale2                       , corner[1] + decalup[1]*scale2                       , corner[2] + decalup[2]*scale2                       , 0,0,r,g,b,a);
			transpolyvert(corner[0] + decalup[0]*scale2 + decalright[0]*scale2, corner[1] + decalup[1]*scale2 + decalright[1]*scale2, corner[2] + decalup[2]*scale2 + decalright[2]*scale2, 1,0,r,g,b,a);
			transpolyvert(corner[0]                     + decalright[0]*scale2, corner[1]                     + decalright[1]*scale2, corner[2]                     + decalright[2]*scale2, 1,1,r,g,b,a);
		}
		else if (p->tex == rainparticletexture) // rain streak
		{
			corner[0] = p->org[0] + up2[0]*scale + right2[0]*scale;
			corner[1] = p->org[1] + up2[1]*scale + right2[1]*scale;
			corner[2] = p->org[2] + up2[2]*scale + right2[2]*scale;
			transpolyvert(corner[0]                                   , corner[1]                                   , corner[2]                                   , 0,1,r,g,b,a);
			transpolyvert(corner[0] + up2[0]*scale2                   , corner[1] + up2[1]*scale2                   , corner[2] + up2[2]*scale2                   , 0,0,r,g,b,a);
			transpolyvert(corner[0] + up2[0]*scale2 + right2[0]*scale2, corner[1] + up2[1]*scale2 + right2[1]*scale2, corner[2] + up2[2]*scale2 + right2[2]*scale2, 1,0,r,g,b,a);
			transpolyvert(corner[0]                 + right2[0]*scale2, corner[1]                 + right2[1]*scale2, corner[2]                 + right2[2]*scale2, 1,1,r,g,b,a);
		}
		else
		{
			corner[0] = p->org[0] + up[0]*scale + right[0]*scale;
			corner[1] = p->org[1] + up[1]*scale + right[1]*scale;
			corner[2] = p->org[2] + up[2]*scale + right[2]*scale;
			transpolyvert(corner[0]                                 , corner[1]                                 , corner[2]                                 , 0,1,r,g,b,a);
			transpolyvert(corner[0] + up[0]*scale2                  , corner[1] + up[1]*scale2                  , corner[2] + up[2]*scale2                  , 0,0,r,g,b,a);
			transpolyvert(corner[0] + up[0]*scale2 + right[0]*scale2, corner[1] + up[1]*scale2 + right[1]*scale2, corner[2] + up[2]*scale2 + right[2]*scale2, 1,0,r,g,b,a);
			transpolyvert(corner[0]                + right[0]*scale2, corner[1]                + right[1]*scale2, corner[2]                + right[2]*scale2, 1,1,r,g,b,a);
		}
		transpolyend();
	}
}
