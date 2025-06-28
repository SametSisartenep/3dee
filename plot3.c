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
typedef struct PColor PColor;
typedef struct Plotpt Plotpt;
typedef struct Plot Plot;

struct AABB
{
	Point3 min;
	Point3 max;
	/* with its homologous bounding sphere */
	Point3 c;
	double r;
};

struct PColor
{
	char *k;
	ulong v;
	Color c;
};

struct Plotpt
{
	Point3 p;
	PColor *c;
};

struct Plot
{
	Plotpt *pts;
	ulong npts;
	AABB bbox;
	Scene *scn;
};

Mousectl *mctl;
Keyboardctl *kctl;
Mouse om;
Channel *drawc;
Image *screenb;
Plot theplot;
Camera *cam;
PColor pal[] = {
	{ .k = "black",		.v = DBlack },
	{ .k = "white",		.v = DWhite },
	{ .k = "red",		.v = DRed },
	{ .k = "green",		.v = DGreen },
	{ .k = "blue",		.v = DBlue },
	{ .k = "yellow",	.v = DYellow },
}, *brush;

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
initpalette(void)
{
	int i;

	for(i = 0; i < nelem(pal); i++)
		pal[i].c = ul2col(pal[i].v);
	brush = &pal[0];
}

void
soakbrush(char *ck)
{
	int i;

	for(i = 0; i < nelem(pal); i++)
		if(strcmp(ck, pal[i].k) == 0){
			brush = &pal[i];
			break;
		}
}

void
updatebboxfromtheplot(void)
{
	static int inited;
	Point3 *lastpt;

	lastpt = &theplot.pts[theplot.npts-1].p;

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
	if(theplot.npts % 16 == 0)
		theplot.pts = erealloc(theplot.pts, (theplot.npts + 16)*sizeof(Plotpt));
	theplot.pts[theplot.npts++] = (Plotpt){p, brush};
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
		if(nf == 2 && strncmp(f[0], "co", 2) == 0){
			soakbrush(f[1]);
			free(line);
			continue;
		}
		if(nf != 3){
			fprint(2, "not enough coordinates. ignoring line %uld\n", lineno);
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
	Primitive line, mark;
	Vertex v;
	Point3 stepv, p0, p1, mp0;
	int i;

	mdl = newmodel();
	ent = newentity("axis scales", mdl);
	theplot.scn->addent(theplot.scn, ent);

	line = mkprim(PLine);
	v = mkvert();
	v.c = mdl->addcolor(mdl, Pt3(0.4,0.4,0.4,1));
	mark = line;

	/* x scale */
	v.p = mdl->addposition(mdl, Pt3(smallestbbox(x), smallestbbox(y), smallestbbox(z), 1));
	line.v[0] = mdl->addvert(mdl, v);
	v.p = mdl->addposition(mdl, Pt3(biggestbbox(x), smallestbbox(y), smallestbbox(z), 1));
	line.v[1] = mdl->addvert(mdl, v);
	mdl->addprim(mdl, line);
	p0 = *(Point3*)itemarrayget(mdl->positions, v.p - 1);
	p1 = *(Point3*)itemarrayget(mdl->positions, v.p);
	stepv = subpt3(p1, p0);
	stepv = divpt3(stepv, 10);
	for(i = 1; i <= 10; i++){
		mp0 = addpt3(p0, mulpt3(stepv, i));
		v.p = mdl->addposition(mdl, mp0);
		mark.v[0] = mdl->addvert(mdl, v);
		v.p = mdl->addposition(mdl, addpt3(mp0, qrotate(stepv, Vec3(0,1,0), 90*DEG)));
		mark.v[1] = mdl->addvert(mdl, v);
		mdl->addprim(mdl, mark);
	}

	/* y scale */
	v.p = mdl->addposition(mdl, Pt3(smallestbbox(x), biggestbbox(y), smallestbbox(z), 1));
	line.v[1] = mdl->addvert(mdl, v);
	mdl->addprim(mdl, line);
	p1 = *(Point3*)itemarrayget(mdl->positions, v.p);
	stepv = subpt3(p1, p0);
	stepv = divpt3(stepv, 10);
	for(i = 1; i <= 10; i++){
		mp0 = addpt3(p0, mulpt3(stepv, i));
		v.p = mdl->addposition(mdl, mp0);
		mark.v[0] = mdl->addvert(mdl, v);
		v.p = mdl->addposition(mdl, addpt3(mp0, qrotate(stepv, normvec3(Vec3(-1,0,1)), 90*DEG)));
		mark.v[1] = mdl->addvert(mdl, v);
		mdl->addprim(mdl, mark);
	}

	/* z scale */
	v.p = mdl->addposition(mdl, Pt3(smallestbbox(x), smallestbbox(y), biggestbbox(z), 1));
	line.v[1] = mdl->addvert(mdl, v);
	mdl->addprim(mdl, line);
	p1 = *(Point3*)itemarrayget(mdl->positions, v.p);
	stepv = subpt3(p1, p0);
	stepv = divpt3(stepv, 10);
	for(i = 1; i <= 10; i++){
		mp0 = addpt3(p0, mulpt3(stepv, i));
		v.p = mdl->addposition(mdl, mp0);
		mark.v[0] = mdl->addvert(mdl, v);
		v.p = mdl->addposition(mdl, addpt3(mp0, qrotate(stepv, Vec3(0,1,0), -90*DEG)));
		mark.v[1] = mdl->addvert(mdl, v);
		mdl->addprim(mdl, mark);
	}
}

void
understandtheplot(void)
{
	Model *mdl;
	Entity *ent;
	Scene *scn;
	Primitive prim;
	Vertex v;
	static Color prevcol;
	Plotpt *p;

	mdl = newmodel();
	ent = newentity(nil, mdl);
	scn = newscene("the plot");
	scn->addent(scn, ent);
	theplot.scn = scn;

	prim = mkprim(PPoint);
	v = mkvert();
	for(p = theplot.pts; p < theplot.pts + theplot.npts; p++){
		v.p = mdl->addposition(mdl, p->p);
		if(!eqpt3(prevcol, p->c->c)){
			v.c = mdl->addcolor(mdl, p->c->c);
			prevcol = p->c->c;
		}
		prim.v[0] = mdl->addvert(mdl, v);
		mdl->addprim(mdl, prim);
	}
	free(theplot.pts);
	theplot.pts = nil;
	theplot.npts = 0;
	frametheplot();
}

void
redrawb(void)
{
	shootcamera(cam, &shaders);
	lockdisplay(display);
	draw(screenb, screenb->r, display->white, nil, ZP);
	cam->view->draw(cam->view, screenb, nil);
	unlockdisplay(display);
	nbsend(drawc, nil);
}

void
redraw(void)
{
	lockdisplay(display);
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
zoomin(void)
{
	cam->fov = fclamp(cam->fov - 1*DEG, 1*DEG, 180*DEG);
	reloadcamera(cam);
	redrawb();
}

void
zoomout(void)
{
	cam->fov = fclamp(cam->fov + 1*DEG, 1*DEG, 180*DEG);
	reloadcamera(cam);
	redrawb();
}

void
lmb(void)
{
	static Quaternion orient = {1,0,0,0};
	Quaternion Δorient;
	Point3 v;

	if((om.buttons^mctl->buttons) != 0)
		return;

	Δorient = orient;
	qball(screen->r, om.xy, mctl->xy, &orient, nil);
	Δorient = mulq(Δorient, invq(orient));

	/* orbit camera around the center */
	v = subpt3(cam->p, theplot.bbox.c);
	v = vcs2world(cam, qsandwichpt3(Δorient, world2vcs(cam, v)));
	movecamera(cam, addpt3(theplot.bbox.c, v));
	aimcamera(cam, theplot.bbox.c);

	redrawb();
}

void
mouse(void)
{
	if(mctl->buttons & 1)
		lmb();
	if(mctl->buttons & 8)
		zoomin();
	if(mctl->buttons & 16)
		zoomout();
	om = mctl->Mouse;
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

	initpalette();
	readtheplot(0);
	understandtheplot();

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((rctl = initgraphics()) == nil)
		sysfatal("initgraphics: %r");
	if(initdraw(nil, nil, "plot3") < 0)
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
	redrawb();

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
