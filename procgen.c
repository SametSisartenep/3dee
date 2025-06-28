/*
 * 	Greek Sunset
 *
 * based on Morgan McGuire's work:
 * 	- https://casual-effects.com/research/McGuire2019ProcGen/McGuire2019ProcGen.pdf
 * 	- https://www.shadertoy.com/view/WsdXWr
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "libgraphics/graphics.h"
#include "fns.h"

Renderer *rctl;
Camera *cam;
Scene *scn;
Entity *ent;
Model *mdl;
Primitive quad[2];

static int doprof;

static Color
getskycolor(double x, double y)
{
	Color c;
	double h;

	h = max(0, 1.4 - y - pow(fabs(x - 0.5), 3));
	c.r = pow(h, 3);
	c.g = pow(h, 7);
	c.b = 0.2 + pow(max(0, h - 0.1), 10);
	c.a = 1;
	return c;
}

static double
fract(double x)
{
	double n;

	return modf(x, &n);
}

static double
hash(double x)
{
	return fract(sin(x) * 1e4);
}

static double
noise(double x)
{
	double i, f, u;

	i = floor(x);
	f = fract(x);
	u = f*f * (3 - 2*f);
	return 2 * flerp(hash(i), hash(i + 1), u) - 1;
}

static double
terrain(double x)
{
	double y, k;
	int oct;

	y = 0;
	for(oct = 0; oct < 10; oct++){
		k = 1<<oct;
		y += noise(x*k)/k;
	}
	return y * 0.3 + 0.36;
}

static double
water(double x, double dt)
{
	return (sin(71*x - 7*dt) * 0.5 + sin(200*x - 8*dt)) * 0.002 + 0.25;
}

static double
tree(double x, double h)
{
	if(h < 0.5)
		return 0;
	else
		return 0.2 * max(0, max(max(
						fabs(sin(x * 109)),
						fabs(sin(x * 150))), 
						fabs(sin(x * 117))) +
					noise(37 * x) +
					noise(64 * x + 100) - 1.6);
}

static Point3
vs(Shaderparams *sp)
{
	return sp->v->p;
}

static Color
fs(Shaderparams *sp)
{
	Vertexattr *va;
	Point2 uv;
	double dt, shift, h, time;

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);
	uv.y = 1 - uv.y;		/* make [0 0] the bottom-left corner */

	va = sp->getuniform(sp, "time");
	time = va == nil? 0: va->n;
	dt = time/1e9;
	shift = 0.09*dt + 0.2;
	uv.x += shift;

	h = max(water(uv.x, dt), terrain(uv.x));
	h += tree(uv.x, h);

	if(uv.y < h)
		return Pt3(0,0,0,1);
	return srgb2linear(getskycolor(uv.x, uv.y));
}

Shadertab shaders = {
	.vs = vs,
	.fs = fs
};

void
usage(void)
{
	fprint(2, "usage: %s [-s frames] [dx [dy]]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Memimage *out;
	Point dim;
	Vertex v;
	int skip;
	double time;

	dim = Pt(800,400);
	skip = 0;
	ARGBEGIN{
	case 's': skip = strtoul(EARGF(usage()), nil, 10); break;
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc > 0)
		switch(argc){
		case 1: dim.x = dim.y = strtoul(argv[0], nil, 10); break;
		case 2:
			dim.x = strtoul(argv[0], nil, 10);
			dim.y = strtoul(argv[1], nil, 10);
			break;
		default: usage();
		}

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((rctl = initgraphics()) == nil)
		sysfatal("initgraphics: %r");
	rctl->doprof = doprof;

	scn = newscene(nil);
	mdl = newmodel();
	ent = newentity(nil, mdl);

	out = eallocmemimage(Rect(0,0,dim.x,dim.y), XRGB32);
	cam = Cam(out->r, rctl, ORTHOGRAPHIC, 40*DEG, 1, 10);
	placecamera(cam, scn, Pt3(0,0,0,1), Vec3(0,0,-1), Vec3(0,1,0));

	quad[0] = quad[1] = mkprim(PTriangle);
	v.p = mdl->addposition(mdl, vcs2clip(cam, viewport2vcs(cam, Pt3(out->r.min.x, out->r.max.y, 1, 1))));
	quad[0].v[0] = mdl->addvert(mdl, v);
	v.p = mdl->addposition(mdl, vcs2clip(cam, viewport2vcs(cam, Pt3(out->r.max.x, out->r.min.y, 1, 1))));
	quad[0].v[1] = mdl->addvert(mdl, v);
	v.p = mdl->addposition(mdl, vcs2clip(cam, viewport2vcs(cam, Pt3(out->r.min.x, out->r.min.y, 1, 1))));
	quad[0].v[2] = mdl->addvert(mdl, v);
	quad[1].v[0] = quad[0].v[0];
	v.p = mdl->addposition(mdl, vcs2clip(cam, viewport2vcs(cam, Pt3(out->r.max.x, out->r.max.y, 1, 1))));
	quad[1].v[1] = mdl->addvert(mdl, v);
	quad[1].v[2] = quad[0].v[1];
	mdl->addprim(mdl, quad[0]);
	mdl->addprim(mdl, quad[1]);
	scn->addent(scn, ent);

	do{
		time = nanosec();
		setuniform(&shaders, "time", VANumber, &time);
		shootcamera(cam, &shaders);
	}while(skip--);
	cam->view->memdraw(cam->view, out, nil);
	writememimage(1, out);

	threadexitsall(nil);
}
