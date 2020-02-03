#include <u.h>
#include <libc.h>
#include <draw.h>
#include "../geometry.h"
#include "../graphics.h"

Triangle
Trian(int x0, int y0, int x1, int y1, int x2, int y2)
{
	return (Triangle){Pt(x0, y0), Pt(x1, y1), Pt(x2, y2)};
}

Triangle
Trianpt(Point p0, Point p1, Point p2)
{
	return (Triangle){p0, p1, p2};
}

void
triangle(Image *dst, Triangle t, int thick, Image *src, Point sp)
{
	Point pl[4];

	pl[0] = t.p0;
	pl[1] = t.p1;
	pl[2] = t.p2;
	pl[3] = pl[0];

	poly(dst, pl, nelem(pl), 0, 0, thick, src, sp);
}

void
filltriangle(Image *dst, Triangle t, Image *src, Point sp)
{
	Point pl[4];

	pl[0] = t.p0;
	pl[1] = t.p1;
	pl[2] = t.p2;
	pl[3] = pl[0];

	fillpoly(dst, pl, nelem(pl), 0, src, sp);
}
