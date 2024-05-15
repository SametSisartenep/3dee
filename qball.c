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

#define MIN(a, b)	((a)<(b)?(a):(b))

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

	q.r = 0;
	if(rsq > 1){	/* outside the sphere */
		rsq = 1/sqrt(rsq);
		q.i = p.x*rsq;
		q.j = p.y*rsq;
		q.k = 0;
	}else{
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
qball(Rectangle r, Point p0, Point p1, Quaternion *orient, Quaternion *axis)
{
	Quaternion qdown, qdrag;
	Point2 rmin, rmax;
	Point2 v0, v1;	/* unit sphere coords */
	Point2 ctlcen;	/* controller center */
	double ctlrad;	/* controller radius */

	if(orient == nil)
		return;

	rmin = Vec2(r.min.x, r.min.y);
	rmax = Vec2(r.max.x, r.max.y);
	ctlcen = divpt2(addpt2(rmin, rmax), 2);
	ctlrad = MIN(Dx(r)/2, Dy(r)/2);
	v0 = divpt2(Vec2(p0.x-ctlcen.x, ctlcen.y-p0.y), ctlrad);
	v1 = divpt2(Vec2(p1.x-ctlcen.x, ctlcen.y-p1.y), ctlrad);
	qdown = mouseq(v0, axis);
	qdrag = mulq(mouseq(v1, axis), qdown);
	*orient = mulq(qdrag, *orient);
}
