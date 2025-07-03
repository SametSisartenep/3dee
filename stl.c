#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libgraphics/graphics.h"
#include "fns.h"
#include "libstl/stl.h"

static int
loadstlmodel(Model *m, Stl *stl)
{
	Primitive prim;
	Vertex v;
	Stltri **tri;
	int i;

	prim = mkprim(PTriangle);
	v = mkvert();
	v.c = m->addcolor(m, Pt3(1,1,1,1));

	for(tri = stl->tris; tri < stl->tris+stl->ntris; tri++){
		v.n = m->addnormal(m, Vec3((*tri)->n[0], (*tri)->n[1], (*tri)->n[2]));
		for(i = 0; i < 3; i++){
			v.p = m->addposition(m, Pt3((*tri)->v[i][0], (*tri)->v[i][1], (*tri)->v[i][2], 1));
			prim.v[i] = m->addvert(m, v);
		}

		m->addprim(m, prim);
	}

	return m->prims->nitems;
}

static Model *
readstlmodel(int fd)
{
	Model *m;
	Stl *stl;

	m = newmodel();
	if((stl = readstl(fd)) == nil)
		sysfatal("readstl: %r");
	loadstlmodel(m, stl);
	freestl(stl);
	return m;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-d] [stlfile]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Model *m;
	char *infile;
	int fd, dedup;

	dedup = 1;
	infile = "/fd/0";
	ARGBEGIN{
	case 'd': dedup--; break;
	default: usage();
	}ARGEND;
	if(argc == 1)
		infile = argv[0];
	else if(argc > 1)
		usage();

	fd = open(infile, OREAD);
	if(fd < 0)
		sysfatal("open: %r");

	m = readstlmodel(fd);
	if(m == nil)
		sysfatal("readstlmodel: %r");

	if(dedup)
		compactmodel(m);

	if(writemodel(1, m) == 0)
		sysfatal("writemodel: %r");

	exits(nil);
}
