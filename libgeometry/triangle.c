#include <u.h>
#include <libc.h>
#include "../geometry.h"

Point3
centroid(Triangle3 t)
{
	return divpt3(addpt3(t.p0, addpt3(t.p1, t.p2)), 3);
}
