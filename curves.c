
// this code written by Forest Hale, on 2003-08-23, and placed into public domain
// this code deals with quadratic splines (minimum of 3 points), the same kind used in Quake3 maps.

// LordHavoc's rant on misuse of the name 'bezier': many people seem to think that bezier is a generic term for splines, but it is not, it is a term for a specific type of spline (minimum of 4 control points, cubic spline).

#include <math.h>
#include "curves.h"
#include "zone.h"

#if 0
void QuadraticSplineSubdivideFloat(int inpoints, int components, const float *in, int instride, float *out, int outstride)
{
	int s;
	// the input (control points) is read as a stream of points, and buffered
	// by the cpprev, cpcurr, and cpnext variables (to allow subdivision in
	// overlapping memory buffers, even subdivision in-place with pre-spaced
	// control points in the buffer)
	// the output (resulting curve) is written as a stream of points
	// this subdivision is meant to be repeated until the desired flatness
	// level is reached
	if (components == 1 && instride == (int)sizeof(float) && outstride == instride)
	{
		// simple case, single component and no special stride
		float cpprev0 = 0, cpcurr0 = 0, cpnext0;
		cpnext0 = *in++;
		for (s = 0;s < inpoints - 1;s++)
		{
			cpprev0 = cpcurr0;
			cpcurr0 = cpnext0;
			if (s < inpoints - 1)
				cpnext0 = *in++;
			if (s > 0)
			{
				// 50% flattened control point
				// cp1 = average(cp1, average(cp0, cp2));
				*out++ = (cpcurr0 + (cpprev0 + cpnext0) * 0.5f) * 0.5f;
			}
			else
			{
				// copy the control point directly
				*out++ = cpcurr0;
			}
			// midpoint
			// mid = average(cp0, cp1);
			*out++ = (cpcurr0 + cpnext0) * 0.5f;
		}
		// copy the final control point
		*out++ = cpnext0;
	}
	else
	{
		// multiple components or stride is used (complex case)
		int c;
		float cpprev[4], cpcurr[4], cpnext[4];
		// check if there are too many components for the buffers
		if (components > 1)
		{
			// more components can be handled, but slowly, by calling self multiple times...
			for (c = 0;c < components;c++, in++, out++)
				QuadraticSplineSubdivideFloat(inpoints, 1, in, instride, out, outstride);
			return;
		}
		for (c = 0;c < components;c++)
			cpnext[c] = in[c];
		(unsigned char *)in += instride;
		for (s = 0;s < inpoints - 1;s++)
		{
			for (c = 0;c < components;c++)
				cpprev[c] = cpcurr[c];
			for (c = 0;c < components;c++)
				cpcurr[c] = cpnext[c];
			for (c = 0;c < components;c++)
				cpnext[c] = in[c];
			(unsigned char *)in += instride;
			// the end points are copied as-is
			if (s > 0)
			{
				// 50% flattened control point
				// cp1 = average(cp1, average(cp0, cp2));
				for (c = 0;c < components;c++)
					out[c] = (cpcurr[c] + (cpprev[c] + cpnext[c]) * 0.5f) * 0.5f;
			}
			else
			{
				// copy the control point directly
				for (c = 0;c < components;c++)
					out[c] = cpcurr[c];
			}
			(unsigned char *)out += outstride;
			// midpoint
			// mid = average(cp0, cp1);
			for (c = 0;c < components;c++)
				out[c] = (cpcurr[c] + cpnext[c]) * 0.5f;
			(unsigned char *)out += outstride;
		}
		// copy the final control point
		for (c = 0;c < components;c++)
			out[c] = cpnext[c];
		//(unsigned char *)out += outstride;
	}
}

// note: out must have enough room!
// (see finalwidth/finalheight calcs below)
void QuadraticSplinePatchSubdivideFloatBuffer(int cpwidth, int cpheight, int xlevel, int ylevel, int components, const float *in, float *out)
{
	int finalwidth, finalheight, xstep, ystep, x, y, c;
	float *o;

	// error out on various bogus conditions
	if (xlevel < 0 || ylevel < 0 || xlevel > 16 || ylevel > 16 || cpwidth < 3 || cpheight < 3)
		return;

	xstep = 1 << xlevel;
	ystep = 1 << ylevel;
	finalwidth = (cpwidth - 1) * xstep + 1;
	finalheight = (cpheight - 1) * ystep + 1;

	for (y = 0;y < finalheight;y++)
		for (x = 0;x < finalwidth;x++)
			for (c = 0, o = out + (y * finalwidth + x) * components;c < components;c++)
				o[c] = 0;

	if (xlevel == 1 && ylevel == 0)
	{
		for (y = 0;y < finalheight;y++)
			QuadraticSplineSubdivideFloat(cpwidth, components, in + y * cpwidth * components, sizeof(float) * components, out + y * finalwidth * components, sizeof(float) * components);
		return;
	}
	if (xlevel == 0 && ylevel == 1)
	{
		for (x = 0;x < finalwidth;x++)
			QuadraticSplineSubdivideFloat(cpheight, components, in + x * components, sizeof(float) * cpwidth * components, out + x * components, sizeof(float) * finalwidth * components);
		return;
	}

	// copy control points into correct positions in destination buffer
	for (y = 0;y < finalheight;y += ystep)
		for (x = 0;x < finalwidth;x += xstep)
			for (c = 0, o = out + (y * finalwidth + x) * components;c < components;c++)
				o[c] = *in++;

	// subdivide in place in the destination buffer
	while (xstep > 1 || ystep > 1)
	{
		if (xstep > 1)
		{
			xstep >>= 1;
			for (y = 0;y < finalheight;y += ystep)
				QuadraticSplineSubdivideFloat(cpwidth, components, out + y * finalwidth * components, sizeof(float) * xstep * 2 * components, out + y * finalwidth * components, sizeof(float) * xstep * components);
			cpwidth = (cpwidth - 1) * 2 + 1;
		}
		if (ystep > 1)
		{
			ystep >>= 1;
			for (x = 0;x < finalwidth;x += xstep)
				QuadraticSplineSubdivideFloat(cpheight, components, out + x * components, sizeof(float) * ystep * 2 * finalwidth * components, out + x * components, sizeof(float) * ystep * finalwidth * components);
			cpheight = (cpheight - 1) * 2 + 1;
		}
	}
}
#elif 1
void QuadraticSplinePatchSubdivideFloatBuffer(int cpwidth, int cpheight, int xlevel, int ylevel, int components, const float *in, float *out)
{
	int c, x, y, outwidth, outheight, halfstep, xstep, ystep;
	float prev, curr, next;
	xstep = 1 << xlevel;
	ystep = 1 << ylevel;
	outwidth = ((cpwidth - 1) * xstep) + 1;
	outheight = ((cpheight - 1) * ystep) + 1;
	for (y = 0;y < cpheight;y++)
		for (x = 0;x < cpwidth;x++)
			for (c = 0;c < components;c++)
				out[(y * ystep * outwidth + x * xstep) * components + c] = in[(y * cpwidth + x) * components + c];
	while (xstep > 1 || ystep > 1)
	{
		if (xstep >= ystep)
		{
			// subdivide on X
			halfstep = xstep >> 1;
			for (y = 0;y < outheight;y += ystep)
			{
				for (c = 0;c < components;c++)
				{
					x = xstep;
					// fetch first two control points 
					prev = out[(y * outwidth + (x - xstep)) * components + c];
					curr = out[(y * outwidth + x) * components + c];
					// create first midpoint
					out[(y * outwidth + (x - halfstep)) * components + c] = (curr + prev) * 0.5f;
					for (;x < outwidth - xstep;x += xstep, prev = curr, curr = next)
					{
						// fetch next control point
						next = out[(y * outwidth + (x + xstep)) * components + c];
						// flatten central control point 
						out[(y * outwidth + x) * components + c] = (curr + (prev + next) * 0.5f) * 0.5f;
						// create following midpoint
						out[(y * outwidth + (x + halfstep)) * components + c] = (curr + next) * 0.5f;
					}
				}
			}
			xstep >>= 1;
		}
		else
		{
			// subdivide on Y
			halfstep = ystep >> 1;
			for (x = 0;x < outwidth;x += xstep)
			{
				for (c = 0;c < components;c++)
				{
					y = ystep;
					// fetch first two control points 
					prev = out[((y - ystep) * outwidth + x) * components + c];
					curr = out[(y * outwidth + x) * components + c];
					// create first midpoint
					out[((y - halfstep) * outwidth + x) * components + c] = (curr + prev) * 0.5f;
					for (;y < outheight - ystep;y += ystep, prev = curr, curr = next)
					{
						// fetch next control point
						next = out[((y + ystep) * outwidth + x) * components + c];
						// flatten central control point 
						out[(y * outwidth + x) * components + c] = (curr + (prev + next) * 0.5f) * 0.5f;;
						// create following midpoint
						out[((y + halfstep) * outwidth + x) * components + c] = (curr + next) * 0.5f;
					}
				}
			}
			ystep >>= 1;
		}
	}
	// flatten control points on X
	for (y = 0;y < outheight;y += ystep)
	{
		for (c = 0;c < components;c++)
		{
			x = xstep;
			// fetch first two control points 
			prev = out[(y * outwidth + (x - xstep)) * components + c];
			curr = out[(y * outwidth + x) * components + c];
			for (;x < outwidth - xstep;x += xstep, prev = curr, curr = next)
			{
				// fetch next control point 
				next = out[(y * outwidth + (x + xstep)) * components + c];
				// flatten central control point 
				out[(y * outwidth + x) * components + c] = (curr + (prev + next) * 0.5f) * 0.5f;;
			}
		}
	}
	// flatten control points on Y
	for (x = 0;x < outwidth;x += xstep)
	{
		for (c = 0;c < components;c++)
		{
			y = ystep;
			// fetch first two control points 
			prev = out[((y - ystep) * outwidth + x) * components + c];
			curr = out[(y * outwidth + x) * components + c];
			for (;y < outheight - ystep;y += ystep, prev = curr, curr = next)
			{
				// fetch next control point 
				next = out[((y + ystep) * outwidth + x) * components + c];
				// flatten central control point 
				out[(y * outwidth + x) * components + c] = (curr + (prev + next) * 0.5f) * 0.5f;;
			}
		}
	}

	/*
	for (y = ystep;y < outheight - ystep;y += ystep)
	{
		for (c = 0;c < components;c++)
		{
			for (x = xstep, outp = out + (y * outwidth + x) * components + c, prev = outp[-xstep * components], curr = outp[0], next = outp[xstep * components];x < outwidth;x += xstep, outp += ystep * outwidth * components, prev = curr, curr = next, next = outp[xstep * components])
			{
				// midpoint
				outp[-halfstep * components] = (prev + curr) * 0.5f;
				// flatten control point
				outp[0] = (curr + (prev + next) * 0.5f) * 0.5f;
				// next midpoint (only needed for end segment)
				outp[halfstep * components] = (next + curr) * 0.5f;
			}
		}
	}
	*/
}
#else
// unfinished code
void QuadraticSplinePatchSubdivideFloatBuffer(int cpwidth, int cpheight, int xlevel, int ylevel, int components, const float *in, float *out)
{
	int outwidth, outheight;
	outwidth = ((cpwidth - 1) << xlevel) + 1;
	outheight = ((cpheight - 1) << ylevel) + 1;
	for (y = 0;y < cpheight;y++)
	{
		for (x = 0;x < cpwidth;x++)
		{
			for (c = 0;c < components;c++)
			{
				inp = in + (y * cpwidth + x) * components + c;
				outp = out + ((y<<ylevel) * outwidth + (x<<xlevel)) * components + c;
				for (sy = 0;sy < expandy;sy++)
				{
					for (sx = 0;sx < expandx;sx++)
					{
						d = a + (b - a) * 2 * t + (a - b + c - b) * t * t;
					}
				}
			}
		}
	}
}
#endif

/*
0.00000 ?.????? ?.????? ?.????? ?.????? ?.????? ?.????? ?.????? 1.00000 ?.????? ?.????? ?.????? ?.????? ?.????? ?.????? ?.????? 0.00000 deviation: 0.5
0.00000 ?.????? ?.????? ?.????? 0.50000 ?.????? ?.????? ?.????? 0.50000 ?.????? ?.????? ?.????? 0.50000 ?.????? ?.????? ?.????? 0.00000 deviation: 0.125
0.00000 ?.????? 0.25000 ?.????? 0.37500 ?.????? 0.50000 ?.????? 0.50000 ?.????? 0.50000 ?.????? 0.37500 ?.????? 0.25000 ?.????? 0.00000 deviation: 0.03125
0.00000 0.12500 0.21875 0.31250 0.37500 0.43750 0.46875 0.50000 0.50000 0.50000 0.46875 0.43750 0.37500 0.31250 0.21875 0.12500 0.00000 deviation: not available
*/

float QuadraticSplinePatchLargestDeviationOnX(int cpwidth, int cpheight, int components, const float *in)
{
	int c, x, y;
	const float *cp;
	float deviation, squareddeviation, bestsquareddeviation;
	bestsquareddeviation = 0;
	for (y = 0;y < cpheight;y++)
	{
		for (x = 1;x < cpwidth-1;x++)
		{
			squareddeviation = 0;
			for (c = 0, cp = in + ((y * cpwidth) + x) * components;c < components;c++, cp++)
			{
				deviation = cp[0] * 0.5f - cp[-components] * 0.25f - cp[components] * 0.25f;
				squareddeviation += deviation*deviation;
			}
			if (bestsquareddeviation < squareddeviation)
				bestsquareddeviation = squareddeviation;
		}
	}
	return sqrt(bestsquareddeviation);
}

float QuadraticSplinePatchLargestDeviationOnY(int cpwidth, int cpheight, int components, const float *in)
{
	int c, x, y;
	const float *cp;
	float deviation, squareddeviation, bestsquareddeviation;
	bestsquareddeviation = 0;
	for (y = 1;y < cpheight-1;y++)
	{
		for (x = 0;x < cpwidth;x++)
		{
			squareddeviation = 0;
			for (c = 0, cp = in + ((y * cpwidth) + x) * components;c < components;c++, cp++)
			{
				deviation = cp[0] * 0.5f - cp[-cpwidth * components] * 0.25f - cp[cpwidth * components] * 0.25f;
				squareddeviation += deviation*deviation;
			}
			if (bestsquareddeviation < squareddeviation)
				bestsquareddeviation = squareddeviation;
		}
	}
	return sqrt(bestsquareddeviation);
}

int QuadraticSplinePatchSubdivisionLevelForDeviation(float deviation, float level1tolerance, int levellimit)
{
	int level;
	// count the automatic flatten step which reduces deviation by 50%
	deviation *= 0.5f;
	// count the levels to subdivide to come under the tolerance
	for (level = 0;level < levellimit && deviation > level1tolerance;level++)
		deviation *= 0.25f;
	return level;
}

int QuadraticSplinePatchSubdivisionLevelOnX(int cpwidth, int cpheight, int components, const float *in, float level1tolerance, int levellimit)
{
	return QuadraticSplinePatchSubdivisionLevelForDeviation(QuadraticSplinePatchLargestDeviationOnX(cpwidth, cpheight, components, in), level1tolerance, levellimit);
}

int QuadraticSplinePatchSubdivisionLevelOnY(int cpwidth, int cpheight, int components, const float *in, float level1tolerance, int levellimit)
{
	return QuadraticSplinePatchSubdivisionLevelForDeviation(QuadraticSplinePatchLargestDeviationOnY(cpwidth, cpheight, components, in), level1tolerance, levellimit);
}

/*
	d = a * (1 - 2 * t + t * t) + b * (2 * t - 2 * t * t) + c * t * t;
	d = a * (1 + t * t + -2 * t) + b * (2 * t + -2 * t * t) + c * t * t;
	d = a * 1 + a * t * t + a * -2 * t + b * 2 * t + b * -2 * t * t + c * t * t;
	d = a * 1 + (a * t + a * -2) * t + (b * 2 + b * -2 * t) * t + (c * t) * t;
	d = a + ((a * t + a * -2) + (b * 2 + b * -2 * t) + (c * t)) * t;
	d = a + (a * (t - 2) + b * 2 + b * -2 * t + c * t) * t;
	d = a + (a * (t - 2) + b * 2 + (b * -2 + c) * t) * t;
	d = a + (a * (t - 2) + b * 2 + (c + b * -2) * t) * t;
	d = a + a * (t - 2) * t + b * 2 * t + (c + b * -2) * t * t;
	d = a * (1 + (t - 2) * t) + b * 2 * t + (c + b * -2) * t * t;
	d = a * (1 + (t - 2) * t) + b * 2 * t + c * t * t + b * -2 * t * t;
	d = a * 1 + a * (t - 2) * t + b * 2 * t + c * t * t + b * -2 * t * t;
	d = a * 1 + a * t * t + a * -2 * t + b * 2 * t + c * t * t + b * -2 * t * t;
	d = a * (1 - 2 * t + t * t) + b * 2 * t + c * t * t + b * -2 * t * t;
	d = a * (1 - 2 * t) + a * t * t + b * 2 * t + c * t * t + b * -2 * t * t;
	d = a + a * -2 * t + a * t * t + b * 2 * t + c * t * t + b * -2 * t * t;
	d = a + a * -2 * t + a * t * t + b * 2 * t + b * -2 * t * t + c * t * t;
	d = a + a * -2 * t + a * t * t + b * 2 * t + b * -2 * t * t + c * t * t;
	d = a + a * -2 * t + b * 2 * t + b * -2 * t * t + a * t * t + c * t * t;
	d = a + a * -2 * t + b * 2 * t + (a + c + b * -2) * t * t;
	d = a + (a * -2 + b * 2) * t + (a + c + b * -2) * t * t;
	d = a + ((a * -2 + b * 2) + (a + c + b * -2) * t) * t;
	d = a + ((b + b - a - a) + (a + c - b - b) * t) * t;
	d = a + (b + b - a - a) * t + (a + c - b - b) * t * t;
	d = a + (b - a) * 2 * t + (a + c - b * 2) * t * t;
	d = a + (b - a) * 2 * t + (a - b + c - b) * t * t;
	
	d = in[0] + (in[1] - in[0]) * 2 * t + (in[0] - in[1] + in[2] - in[1]) * t * t;
*/

