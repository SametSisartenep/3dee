/*
 * Ken Shoemake's Quaternion rotation controller
 * “Arcball Rotation Control”, Graphics Gems IV § III.1, pp. 175-192, August 1994.
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "libgraphics/graphics.h"
#include "fns.h"

static int
min(int a, int b)
{
	return a < b? a: b;
}

/*
 * Convert a mouse point into a unit quaternion, flattening if
 * constrained to a particular plane.
 */
static Quaternion
mouseq(Point2 p, Quaternion *axis)
{
	double l;
	Quaternion q;
	double rsq = p.x*p.x + p.y*p.y;	/* quadrance */

	if(rsq > 1){	/* outside the sphere */
		rsq = sqrt(rsq);
		q.r = 0;
		q.i = p.x/rsq;
		q.j = p.y/rsq;
		q.k = 0;
	}else{		/* within the sphere */
		q.r = 0;
		q.i = p.x;
		q.j = p.y;
		q.k = sqrt(1 - rsq);
	}

	if(axis != nil){
		l    = dotq(q, *axis);
		q.i -= l*axis->i;
		q.j -= l*axis->j;
		q.k -= l*axis->k;
		l    = qlen(q);
		if(l != 0){
			q.i /= l;
			q.j /= l;
			q.k /= l;
		}
	}

	return q;
}

void
qb(Rectangle r, Point p0, Point p1, Quaternion *orient, Quaternion *axis)
{
	Quaternion q, down;
	Point2 rmin, rmax;
	Point2 s0, s1;	/* screen coords */
	Point2 v0, v1;	/* unit sphere coords */
	Point2 ctlcen;	/* controller center */
	double ctlrad;	/* controller radius */

	rmin = Vec2(r.min.x, r.min.y);
	rmax = Vec2(r.max.x, r.max.y);
	s0 = Vec2(p0.x, p0.y);
	s1 = Vec2(p1.x, p1.y);
	ctlcen = divpt2(addpt2(rmin, rmax), 2);
	ctlrad = min(Dx(r), Dy(r));
	v0 = divpt2(subpt2(s0, ctlcen), ctlrad);
	down = invq(mouseq(v0, axis));

	q = *orient;
	v1 = divpt2(subpt2(s1, ctlcen), ctlrad);
	*orient = mulq(q, mulq(down, mouseq(v1, axis)));
}
