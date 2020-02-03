#include <u.h>
#include <libc.h>
#include "../geometry.h"

/* 2D */

Point2
Pt2(double x, double y, double w)
{
	return (Point2){x, y, w};
}

Point2
Vec2(double x, double y)
{
	return (Point2){x, y, 0};
}

Point2
addpt2(Point2 a, Point2 b)
{
	return (Point2){a.x+b.x, a.y+b.y, a.w+b.w};
}

Point2
subpt2(Point2 a, Point2 b)
{
	return (Point2){a.x-b.x, a.y-b.y, a.w-b.w};
}

Point2
mulpt2(Point2 p, double s)
{
	return (Point2){p.x*s, p.y*s, p.w*s};
}

Point2
divpt2(Point2 p, double s)
{
	return (Point2){p.x/s, p.y/s, p.w/s};
}

Point2
lerp2(Point2 a, Point2 b, double t)
{
	if(t < 0) t = 0;
	if(t > 1) t = 1;
	return (Point2){
		(1 - t)*a.x + t*b.x,
		(1 - t)*a.y + t*b.y,
		(1 - t)*a.w + t*b.w
	};
}

double
dotvec2(Point2 a, Point2 b)
{
	return a.x*b.x + a.y*b.y;
}

double
vec2len(Point2 v)
{
	return sqrt(dotvec2(v, v));
}

Point2
normvec2(Point2 v)
{
	double len;

	len = vec2len(v);
	if(len == 0)
		return (Point2){0, 0, 0};
	return (Point2){v.x/len, v.y/len, 0};
}

/* 3D */

Point3
Pt3(double x, double y, double z, double w)
{
	return (Point3){x, y, z, w};
}

Point3
Vec3(double x, double y, double z)
{
	return (Point3){x, y, z, 0};
}

Point3
addpt3(Point3 a, Point3 b)
{
	return (Point3){a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w};
}

Point3
subpt3(Point3 a, Point3 b)
{
	return (Point3){a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w};
}

Point3
mulpt3(Point3 p, double s)
{
	return (Point3){p.x*s, p.y*s, p.z*s, p.w*s};
}

Point3
divpt3(Point3 p, double s)
{
	return (Point3){p.x/s, p.y/s, p.z/s, p.w/s};
}

Point3
lerp3(Point3 a, Point3 b, double t)
{
	if(t < 0) t = 0;
	if(t > 1) t = 1;
	return (Point3){
		(1 - t)*a.x + t*b.x,
		(1 - t)*a.y + t*b.y,
		(1 - t)*a.z + t*b.z,
		(1 - t)*a.w + t*b.w
	};
}

double
dotvec3(Point3 a, Point3 b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

Point3
crossvec3(Point3 a, Point3 b)
{
	return (Point3){
		a.y*b.z - a.z*b.y,
		a.z*b.x - a.x*b.z,
		a.x*b.y - a.y*b.x,
		0
	};
}

double
vec3len(Point3 v)
{
	return sqrt(dotvec3(v, v));
}

Point3
normvec3(Point3 v)
{
	double len;

	len = vec3len(v);
	if(len == 0)
		return (Point3){0, 0, 0, 0};
	return (Point3){v.x/len, v.y/len, v.z/len, 0};
}
