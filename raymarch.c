/*
 * based on kishimisu's “An Introduction to Raymarching”
 * - https://www.youtube.com/watch?v=khblXafu7iA
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "libgraphics/graphics.h"
#include "fns.h"

#define sd∪(a, b)	min((a), (b))
#define sd∩(a, b)	max((a), (b))
#define sdsub(a, b)	max(-(a), (b))

Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;
Image *screenb;
Camera *cam;
Scene *scn;
Entity *ent;
Model *mdl;

static int doprof;

static Point3
abspt3(Point3 p)
{
	return (Point3){
		fabs(p.x),
		fabs(p.y),
		fabs(p.z),
		fabs(p.w)
	};
}

static double
smin(double a, double b, double k)
{
	double h;

	h = max(k - fabs(a - b), 0)/k;
	return min(a, b) - h*h*h * k/6;
}

static double
sdBox(Point3 p, Point3 span)
{
	double tmp;

	p = subpt3(abspt3(p), span);
	tmp = min(max(p.x, max(p.y, p.z)), 0);
	return vec3len(maxpt3(p, ZP3)) + tmp;
}

static double
sdSphere(Point3 p, double r)
{
	return vec3len(p) - r;
}

static double
map(Point3 p, double dt)
{
	enum {
		RADIUS	= 1,
		SIDELEN	= 0.75,
	};
	Point3 sphp;
	double sphere, box, gnd;

	sphp = Pt3(sin(dt)*3, 0, 0, 1);
	sphere = sdSphere(subpt3(p, sphp), RADIUS);
	box = sdBox(p, Vec3(SIDELEN, SIDELEN, SIDELEN));
	gnd = p.y + 0.75;

	return sd∪(gnd, smin(sphere, box, 2));
}

static Point3
vs(Shaderparams *sp)
{
	return sp->v->p;
}

static Color
fs(Shaderparams *sp)
{
	Vertexattr *va;
	Point2 uv, m;
	Point3 mpt, ro, rd, p;
	Color c;
	double time, dt, t, d;
	int i;

	uv = Pt2(sp->p.x, sp->p.y, 1);
	uv.x = 2*uv.x - Dx(sp->fb->r);
	uv.y = Dy(sp->fb->r) - 2*uv.y;
	uv = divpt2(uv, Dy(sp->fb->r));

	va = sp->getuniform(sp, "time");
	time = va == nil? 0: va->n;
	dt = time/1e9;

	va = sp->getuniform(sp, "mouse");
	mpt = va == nil? ZP3: va->p;
	m = Pt2(mpt.x, mpt.y, 1);
	m.x = 2*m.x - Dx(sp->fb->r);
	m.y = Dy(sp->fb->r) - 2*m.y;
	m = divpt2(m, Dy(sp->fb->r)/2);

	ro = Pt3(0, 0, 3, 1);
	rd = normvec3(Vec3(uv.x, uv.y, -1));
	t = 0;

	ro = qrotate(qrotate(ro, Vec3(0,1,0), -m.x), Vec3(1,0,0), m.y);
	rd = qrotate(qrotate(rd, Vec3(0,1,0), -m.x), Vec3(1,0,0), m.y);

	for(i = 0; i < 20; i++){
		p = addpt3(ro, mulpt3(rd, t));
		d = map(p, dt);
		t += d;

		if(d < 0.001 || t > 100)
			break;
	}

	c = mulpt3(Vec3(t, t, t), 0.2);
	c.a = 1;
	return srgb2linear(c);
}

Shadertab shaders = {
	.vs = vs,
	.fs = fs
};

void
redraw(void)
{
	draw(screen, screen->r, screenb, nil, ZP);
	flushimage(display, 1);
}

void
drawthread(void *)
{
	threadsetname("drawthread");

	for(;;){
		recv(drawc, nil);
		redraw();
	}
}

void
renderproc(void *)
{
	Point3 mpt;
	uvlong t0, Δt;
	double time;

	threadsetname("renderproc");

	t0 = nanosec();
	for(;;){
		time = nanosec();
		setuniform(&shaders, "time", VANumber, &time);
		mpt = Vec3(mctl->xy.x, mctl->xy.y, 0);
		setuniform(&shaders, "mouse", VAPoint, &mpt);
		shootcamera(cam, &shaders);

		Δt = nanosec() - t0;
		if(Δt > HZ2NS(60)){
			draw(screenb, screenb->r, display->black, nil, ZP);
			cam->view->draw(cam->view, screenb, nil);
			nbsend(drawc, nil);
			t0 = nanosec();
		}else{
			Δt = HZ2NS(60) - Δt;
			if(Δt > 1000000ULL)
				sleep(Δt/1000000ULL);
			else
				sleep(1);
		}
	}
}

void
mouse(void)
{
	mctl->xy = subpt(mctl->xy, screen->r.min);
}

void
resize(void)
{
	if(getwindow(display, Refnone) < 0)
		fprint(2, "can't reattach to window\n");
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

static void
confproc(void)
{
	char buf[64];
	int fd;

	snprint(buf, sizeof buf, "/proc/%d/ctl", getpid());
	fd = open(buf, OWRITE);
	if(fd < 0)
		sysfatal("open: %r");

	if(doprof)
		fprint(fd, "profile\n");
	close(fd);
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
	Primitive quad[2];
	Vertex v;
	Rune r;

	ARGBEGIN{
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	confproc();

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((rctl = initgraphics()) == nil)
		sysfatal("initgraphics: %r");
	if(initdraw(nil, nil, "raymarch") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	rctl->doprof = doprof;

	scn = newscene(nil);
	mdl = newmodel();
	ent = newentity(nil, mdl);

	screenb = eallocimage(display, rectsubpt(screen->r, screen->r.min), XRGB32, 0, DNofill);
	cam = Cam(screenb->r, rctl, ORTHOGRAPHIC, 40*DEG, 1, 10);
	placecamera(cam, scn, Pt3(0,0,0,1), Vec3(0,0,-1), Vec3(0,1,0));

	quad[0] = quad[1] = mkprim(PTriangle);
	v = mkvert();
	v.p = mdl->addposition(mdl, vcs2clip(cam, viewport2vcs(cam, Pt3(screenb->r.min.x, screenb->r.max.y, 1, 1))));
	quad[0].v[0] = mdl->addvert(mdl, v);
	v.p = mdl->addposition(mdl, vcs2clip(cam, viewport2vcs(cam, Pt3(screenb->r.max.x, screenb->r.min.y, 1, 1))));
	quad[0].v[1] = mdl->addvert(mdl, v);
	v.p = mdl->addposition(mdl, vcs2clip(cam, viewport2vcs(cam, Pt3(screenb->r.min.x, screenb->r.min.y, 1, 1))));
	quad[0].v[2] = mdl->addvert(mdl, v);
	quad[1].v[0] = quad[0].v[0];
	v.p = mdl->addposition(mdl, vcs2clip(cam, viewport2vcs(cam, Pt3(screenb->r.max.x, screenb->r.max.y, 1, 1))));
	quad[1].v[1] = mdl->addvert(mdl, v);
	quad[1].v[2] = quad[0].v[1];
	mdl->addprim(mdl, quad[0]);
	mdl->addprim(mdl, quad[1]);
	scn->addent(scn, ent);

	drawc = chancreate(sizeof(void*), 1);

	proccreate(renderproc, nil, mainstacksize);
	threadcreate(drawthread, nil, mainstacksize);

	enum {MOUSE, RESIZE, KEY};
	Alt a[] = {
		{mctl->c, &mctl->Mouse, CHANRCV},
		{mctl->resizec, nil, CHANRCV},
		{kctl->c, &r, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;)
		switch(alt(a)){
		case MOUSE: mouse(); break;
		case RESIZE: resize(); break;
		case KEY: key(r); break;
		}
}
