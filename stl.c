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
	Primitive p;
	Stltri **tri;

	memset(&p, 0, sizeof p);
	p.type = PTriangle;
	p.v[0].c = p.v[1].c = p.v[2].c = Pt3(1,1,1,1);

	for(tri = stl->tris; tri < stl->tris+stl->ntris; tri++){
		p.v[0].p = Pt3((*tri)->p0[0], (*tri)->p0[1], (*tri)->p0[2], 1);
		p.v[1].p = Pt3((*tri)->p1[0], (*tri)->p1[1], (*tri)->p1[2], 1);
		p.v[2].p = Pt3((*tri)->p2[0], (*tri)->p2[1], (*tri)->p2[2], 1);
		p.v[0].n = Vec3((*tri)->n[0], (*tri)->n[1], (*tri)->n[2]);
		p.v[1].n = p.v[2].n = p.v[0].n;

		m->addprim(m, p);
	}

	return m->nprims;
}

static Model *
readstlmodel(int fd)
{
	Model *m;
	Stl *stl;

	m = newmodel();
	if((stl = readstl(fd)) == nil)
		sysfatal("readst: %r");
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
		sysfatal("sysfatal: %r");

	if(writemodel(1, m, dedup) == 0)
		sysfatal("writemodel: %r");

	exits(nil);
}
