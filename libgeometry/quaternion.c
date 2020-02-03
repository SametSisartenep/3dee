#include <u.h>
#include <libc.h>
#include "../geometry.h"

Quaternion
Quat(double r, double i, double j, double k)
{
	return (Quaternion){r, i, j, k};
}

Quaternion
Quatvec(double s, Point3 v)
{
	return (Quaternion){s, v.x, v.y, v.z};
}

Quaternion
addq(Quaternion a, Quaternion b)
{
	return (Quaternion){a.r+b.r, a.i+b.i, a.j+b.j, a.k+b.k};
}

Quaternion
subq(Quaternion a, Quaternion b)
{
	return (Quaternion){a.r-b.r, a.i-b.i, a.j-b.j, a.k-b.k};
}

Quaternion
mulq(Quaternion q, Quaternion r)
{
	Point3 qv, rv, tmp;

	qv = Vec3(q.i, q.j, q.k);
	rv = Vec3(r.i, r.j, r.k);
	tmp = addpt3(addpt3(mulpt3(rv, q.r), mulpt3(qv, r.r)), crossvec3(qv, rv));
	return (Quaternion){q.r*r.r - dotvec3(qv, rv), tmp.x, tmp.y, tmp.z};
}

Quaternion
smulq(Quaternion q, double s)
{
	return (Quaternion){q.r*s, q.i*s, q.j*s, q.k*s};
}

Quaternion
sdivq(Quaternion q, double s)
{
	return (Quaternion){q.r/s, q.i/s, q.j/s, q.k/s};
}

double
dotq(Quaternion q, Quaternion r)
{
	return q.r*r.r + q.i*r.i + q.j*r.j + q.k*r.k;
}

Quaternion
invq(Quaternion q)
{
	double len²;

	len² = dotq(q, q);
	if(len² == 0)
		return (Quaternion){0, 0, 0, 0};
	return (Quaternion){q.r/len², -q.i/len², -q.j/len², -q.k/len²};
}

double
qlen(Quaternion q)
{
	return sqrt(q.r*q.r + q.i*q.i + q.j*q.j + q.k*q.k);
}

Quaternion
normq(Quaternion q)
{
	return sdivq(q, qlen(q));
}

Point3
qrotate(Point3 p, Point3 axis, double angle)
{
	Quaternion qaxis, qr;

	angle /= 2;
	qaxis = Quatvec(cos(angle), mulpt3(axis, sin(angle)));
	qr = mulq(mulq(qaxis, Quatvec(0, p)), invq(qaxis));
	return Vec3(qr.i, qr.j, qr.k);
}
