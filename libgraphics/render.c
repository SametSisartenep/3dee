#include <u.h>
#include <libc.h>
#include <draw.h>
#include "../geometry.h"
#include "../graphics.h"

/*
 * careful with concurrent rendering.
 * use a lock or embed on each routine.
 */
static RFrame imgrframe = {
	0,  0, 1,	/* p */
	1,  0, 0,	/* bx */
	0, -1, 0	/* by */
};

/* requires p to be in NDC */
int
isclipping(Point3 p)
{
	if(p.x > p.w || p.x < -p.w ||
	   p.y > p.w || p.y < -p.w ||
	   p.z > p.w || p.z < 0)
		return 1;
	return 0;
}

static Point2
flatten(Camera *c, Point3 p)
{
	Point2 p2;
	Matrix S = {
		Dx(c->viewport->r)/2, 0, 0,
		0, Dy(c->viewport->r)/2, 0,
		0, 0, 1,
	}, T = {
		1, 0, 1,
		0, 1, 1,
		0, 0, 1,
	};

	p2 = (Point2){p.x, p.y, p.w};
	if(p2.w != 0)
		p2 = divpt2(p2, p2.w);
	mulm(S, T);
	p2 = xform(p2, S);
	return p2;
}

Point
toviewport(Camera *c, Point3 p)
{
	Point2 p2;

	imgrframe.p = Pt2(c->viewport->r.min.x, c->viewport->r.max.y, 1);
	p2 = invrframexform(flatten(c, p), imgrframe);
	return (Point){p2.x, p2.y};
}

Point2
fromviewport(Camera *c, Point p)
{
	imgrframe.p = Pt2(c->viewport->r.min.x, c->viewport->r.max.y, 1);
	return rframexform(Pt2(p.x, p.y, 1), imgrframe);
}

void
line3(Camera *c, Point3 p0, Point3 p1, int end0, int end1, Image *src)
{
	p0 = WORLD2NDC(c, p0);
	p1 = WORLD2NDC(c, p1);
	if(isclipping(p0) || isclipping(p1))
		return;
	line(c->viewport, toviewport(c, p0), toviewport(c, p1), end0, end1, 0, src, ZP);
}

Point
string3(Camera *c, Point3 p, Image *src, Font *f, char *s)
{
	p = WORLD2NDC(c, p);
	if(isclipping(p))
		return (Point){~0, ~0};
	return string(c->viewport, toviewport(c, p), src, ZP, f, s);
}
