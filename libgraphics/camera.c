#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include "../geometry.h"
#include "../graphics.h"

static int
max(int a, int b)
{
	return a > b ? a : b;
}

static void
verifycfg(Camera *c)
{
	assert(c->viewport.fb != nil);
	if(c->ptype == Ppersp)
		assert(c->fov > 0 && c->fov < 360);
	assert(c->clipn > 0 && c->clipn < c->clipf);
}

static void
updatefb(Camera *c, Rectangle r, ulong chan)
{
	Memimage *fb;

	fb = allocmemimage(r, chan);
	if(fb == nil)
		sysfatal("allocmemimage: %r");
	c->viewport.fb = fb;
	c->viewport.p = Pt2(r.min.x,r.max.y,1);
}

Camera*
alloccamera(Rectangle r, ulong chan)
{
	Camera *c;

	c = malloc(sizeof(Camera));
	if(c == nil)
		sysfatal("malloc: %r");
	memset(c, 0, sizeof *c);
	c->viewport.bx = Vec2(1,0);
	c->viewport.by = Vec2(0,-1);
	updatefb(c, r, chan);
	c->updatefb = updatefb;
}

void
configcamera(Camera *c, double fov, double n, double f, Projection p)
{
	c->fov = fov;
	c->clipn = n;
	c->clipf = f;
	c->ptype = p;
	reloadcamera(c);
}

void
placecamera(Camera *c, Point3 p, Point3 focus, Point3 up)
{
	c->p = p;
	if(focus.w == 0)
		c->bz = focus;
	else
		c->bz = normvec3(subpt3(c->p, focus));
	c->bx = normvec3(crossvec3(up, c->bz));
	c->by = crossvec3(c->bz, c->bx);
}

void
aimcamera(Camera *c, Point3 focus)
{
	placecamera(c, c->p, focus, c->by);
}

void
reloadcamera(Camera *c)
{
	double a;
	double l, r, b, t;

	verifycfg(c);
	switch(c->ptype){
	case Portho:
		/*
		r = Dx(c->viewport.fb->r)/2;
		t = Dy(c->viewport.fb->r)/2;
		l = -r;
		b = -t;
		*/
		l = t = 0;
		r = Dx(c->viewport.fb->r);
		b = Dy(c->viewport.fb->r);
		orthographic(c->proj, l, r, b, t, c->clipn, c->clipf);
		break;
	case Ppersp:
		a = (double)Dx(c->viewport.fb->r)/Dy(c->viewport.fb->r);
		perspective(c->proj, c->fov, a, c->clipn, c->clipf);
		break;
	default: sysfatal("unknown projection type");
	}
}
