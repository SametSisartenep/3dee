#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "libgraphics/graphics.h"
#include "fns.h"

typedef struct AABB AABB;
struct AABB
{
	Point3 min;
	Point3 max;
	/* with its homologous bounding sphere */
	Point3 c;
	double r;
};

typedef struct Plot Plot;
struct Plot
{
	Point3 *pts;
	ulong npts;
	AABB bbox;
	Scene *scn;
};

Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;
Image *screenb;
Plot theplot;
Camera *cam;

static Point3
vs(Shaderparams *sp)
{
	return world2clip(sp->su->camera, model2world(sp->su->entity, sp->v->p));
}

static Color
fs(Shaderparams *sp)
{
	return sp->v->c;
}

Shadertab shaders = {
	.vs = vs,
	.fs = fs
};

void
updatebboxfromtheplot(void)
{
	static int inited;
	Point3 *lastpt;

	lastpt = &theplot.pts[theplot.npts-1];

	if(!inited){
		theplot.bbox.min = theplot.bbox.max = *lastpt;
		inited++;
		return;
	}

	theplot.bbox.min = minpt3(theplot.bbox.min, *lastpt);
	theplot.bbox.max = maxpt3(theplot.bbox.max, *lastpt);
	theplot.bbox.c = divpt3(addpt3(theplot.bbox.max, theplot.bbox.min), 2);
	theplot.bbox.r = max(vec3len(theplot.bbox.min), vec3len(theplot.bbox.max));
}

void
addpttotheplot(Point3 p)
{
	if(theplot.npts % 4 == 0)
		theplot.pts = erealloc(theplot.pts, (theplot.npts + 4)*sizeof(Plot));
	theplot.pts[theplot.npts++] = p;
	updatebboxfromtheplot();
}

void
readtheplot(int fd)
{
	Biobuf *bin;
	Point3 p;
	char *line, *f[3];
	ulong lineno;
	int nf;

	bin = Bfdopen(fd, OREAD);
	if(bin == nil)
		sysfatal("Bfdopen: %r");

	lineno = 0;
	p.w = 1;
	while((line = Brdstr(bin, '\n', 1)) != nil){
		lineno++;
		nf = tokenize(line, f, nelem(f));
		if(nf != 3){
			fprint(2, "not enough fields. ignoring line %uld\n", lineno);
			free(line);
			continue;
		}
		p.x = strtod(f[0], nil);
		p.y = strtod(f[1], nil);
		p.z = strtod(f[2], nil);
		addpttotheplot(p);
		free(line);
	}
	Bterm(bin);
}

#define smallestbbox(coord)	(min(theplot.bbox.min.coord, theplot.bbox.max.coord))
#define biggestbbox(coord)	(max(theplot.bbox.min.coord, theplot.bbox.max.coord))
void
frametheplot(void)
{
	Model *mdl;
	Entity *ent;
	Primitive line;

	mdl = newmodel();
	ent = newentity("axis scales", mdl);
	theplot.scn->addent(theplot.scn, ent);

	line.type = PLine;
	line.v[0].c = line.v[1].c = Pt3(0,0,0,1);

	/* x scale */
	line.v[0].p = Pt3(smallestbbox(x), smallestbbox(y), biggestbbox(z), 1);
	line.v[1].p = addpt3(line.v[0].p, Vec3(biggestbbox(x), 0, 0));
	mdl->addprim(mdl, line);

	/* y scale */
	line.v[1].p = addpt3(line.v[0].p, Vec3(0, biggestbbox(y), 0));
	mdl->addprim(mdl, line);

	/* z scale */
	line.v[1].p = addpt3(line.v[0].p, Vec3(0, 0, smallestbbox(z)));
	mdl->addprim(mdl, line);
}

void
understandtheplot(void)
{
	Model *mdl;
	Entity *ent;
	Scene *scn;
	Primitive prim;
	Point3 *p;

	mdl = newmodel();
	ent = newentity(nil, mdl);
	scn = newscene("the plot");
	scn->addent(scn, ent);
	theplot.scn = scn;

	memset(&prim, 0, sizeof prim);
	prim.type = PPoint;
	prim.v[0].c = Pt3(0,0,0,1);

	for(p = theplot.pts; p < theplot.pts + theplot.npts; p++){
		prim.v[0].p = *p;
		mdl->addprim(mdl, prim);
	}
	frametheplot();
}

void
redraw(void)
{
	shootcamera(cam, &shaders);
	lockdisplay(display);
	draw(screenb, screenb->r, display->white, nil, ZP);
	cam->view->draw(cam->view, screenb, nil);
	draw(screen, screen->r, screenb, nil, ZP);
	flushimage(display, 1);
	unlockdisplay(display);
}

void
drawproc(void *)
{
	threadsetname("drawproc");

	for(;;){
		recv(drawc, nil);
		redraw();
	}
}

void
mouse(void)
{
}

void
resize(void)
{
	lockdisplay(display);
	if(getwindow(display, Refnone) < 0)
		fprint(2, "can't reattach to window\n");
	unlockdisplay(display);
	nbsend(drawc, nil);
}

void
key(Rune r)
{
	switch(r){
	case Kdel:
	case 'q':
		threadexitsall(nil);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Renderer *rctl;
	Rune r;

	ARGBEGIN{
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	readtheplot(0);
	understandtheplot();

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((rctl = initgraphics()) == nil)
		sysfatal("initgraphics: %r");
	if(initdraw(nil, nil, "solar") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	screenb = eallocimage(display, rectsubpt(screen->r, screen->r.min), XRGB32, 0, DNofill);
	cam = Cam(screenb->r, rctl, PERSPECTIVE, 90*DEG, 0.1, 1000);
	placecamera(cam, theplot.scn, addpt3(theplot.bbox.c, mulpt3(normvec3(Vec3(1,1,1)), 1.5*theplot.bbox.r)), theplot.bbox.c, Vec3(0,1,0));

	display->locking = 1;
	unlockdisplay(display);

	drawc = chancreate(sizeof(void*), 1);
	proccreate(drawproc, nil, mainstacksize);
	nbsend(drawc, nil);

	enum {MOUSE, RESIZE, KEY};
	Alt a[] = {
		{mctl->c, &mctl->Mouse, CHANRCV},
		{mctl->resizec, nil, CHANRCV},
		{kctl->c, &r, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;)
		switch(alt(a)){
		case -1: sysfatal("alt interrupted");
		case MOUSE:
			mouse();
			break;
		case RESIZE:
			resize();
			break;
		case KEY:
			key(r);
			break;
		}
}
