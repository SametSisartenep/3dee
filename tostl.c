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
loadstlmodel(Stl *stl, Model *m)
{
	Primitive *prim, *lastprim;
	Vertex *v;
	Point3 *p;
	Stltri **tri, *t;
	int i;

	snprint((char*)stl->hdr, sizeof(stl->hdr), "Exported with libstl from ⑨");

	/* XXX we assume all prims are triangles */
	stl->ntris = m->prims->nitems;
	stl->tris = emalloc(stl->ntris*sizeof(Stltri*));

	/* since we don't use attributes we can allocate tris in bulk */
	t = emalloc(stl->ntris*sizeof(Stltri));
	memset(t, 0, stl->ntris*sizeof(Stltri));
	for(tri = stl->tris; tri < stl->tris+stl->ntris; tri++)
		*tri = &t[tri - stl->tris];

	tri = stl->tris;
	lastprim = itemarrayget(m->prims, m->prims->nitems-1);
	for(prim = m->prims->items; prim <= lastprim; prim++){
		if(prim->type != PTriangle){
			stl->ntris--;
			continue;
		}

		v = itemarrayget(m->verts, prim->v[0]);
		p = itemarrayget(m->normals, v->n);

		(*tri)->n[0] = p->x;
		(*tri)->n[1] = p->y;
		(*tri)->n[2] = p->z;

		for(i = 0; i < 3; i++){
			v = itemarrayget(m->verts, prim->v[i]);
			p = itemarrayget(m->positions, v->p);
			(*tri)->v[i][0] = p->x;
			(*tri)->v[i][1] = p->y;
			(*tri)->v[i][2] = p->z;
		}
		tri++;
	}

	return stl->ntris;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-t] [mdlfile]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Model *m;
	Stl *stl;
	char *infile;
	int fd, ofmt;

	ofmt = STLBINARY;
	infile = "/fd/0";
	ARGBEGIN{
	case 't': ofmt = STLTEXT; break;
	default: usage();
	}ARGEND;
	if(argc == 1)
		infile = argv[0];
	else if(argc > 1)
		usage();

	fd = open(infile, OREAD);
	if(fd < 0)
		sysfatal("open: %r");

	m = readmodel(fd);
	if(m == nil)
		sysfatal("readmodel: %r");

	stl = emalloc(sizeof(Stl));
	loadstlmodel(stl, m);

	if(writestl(1, stl, ofmt) == 0)
		sysfatal("writestl: %r");

	exits(nil);
}
