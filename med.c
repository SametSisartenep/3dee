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

enum {
	Kmodeorb,
	Kmodesel,
	Kzoomin,
	Kzoomout,
	Khud,
	Kfrustum,
	Ke
};

enum {
	Sfov,
	Scampos,
	Scambx, Scamby, Scambz,
	Sfps,
	Sframes,
	Se
};

enum {
	OMOrbit,
	OMSelect,
};

typedef struct Camcfg Camcfg;
struct Camcfg
{
	Point3 p, lookat, up;
	double fov, clipn, clipf;
	int ptype;
};

typedef struct Compass Compass;
struct Compass
{
	Camera	*cam;
	Scene	*scn;
};
Compass compass;	/* 3d compass */

Rune keys[Ke] = {
 [Kmodeorb]	= 'r',
 [Kmodesel]	= 's',
 [Kzoomin]	= 'z',
 [Kzoomout]	= 'x',
 [Khud]		= 'h',
 [Kfrustum]	= ' ',
};
char stats[Se][256];
Image *screenb;
Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;
int kdown;
Scene *scene;
Entity *subject;
Model *model;
Shadertab *shader;
QLock scenelk;
Mouse om;

Camera *cam;
Camcfg camcfg = {
	0,2,4,1,
	0,0,0,1,
	0,1,0,0,
	40*DEG, 0.01, 10, PERSPECTIVE
};
Point3 center = {0,0,0,1};
LightSource light;	/* global point light */

static int doprof;
static int showhud;
static int opmode;
Color (*tsampler)(Texture*,Point2);

#include "shaders.inc"

static Point3
Vecquat(Quaternion q)
{
	return Vec3(q.i, q.j, q.k);
}

static Point3
Ptquat(Quaternion q, double w)
{
	return Pt3(q.i, q.j, q.k, w);
}

/*
 * p is the point to test
 * p0 and p1 are the centers of the circles at each end of the cylinder
 * r is the radius of these circles
 */
int
ptincylinder(Point3 p, Point3 p0, Point3 p1, double r)
{
	Point3 p01, p0p, p1p;
	double h;

	p01 = subpt3(p1, p0);
	p0p = subpt3(p, p0);
	p1p = subpt3(p, p1);
	h = vec3len(p01);

	if(h == 0)
		return 0;

	return dotvec3(p0p, p01) >= 0 &&
		dotvec3(p1p, p01) <= 0 &&
		vec3len(crossvec3(p0p, p01))/h <= r;
}

/*
 * p is the point to test
 * p0 is the apex
 * p1 is the center of the base
 * br is the radius of the base
 */
int
ptincone(Point3 p, Point3 p0, Point3 p1, double br)
{
	Point3 p01, p0p;
	double h, d, r;

	p01 = subpt3(p1, p0);
	p0p = subpt3(p, p0);
	h = vec3len(p01);
	d = dotvec3(p0p, normvec3(p01));

	if(h == 0 || d < 0 || d > h)
		return 0;

	r = d/h * br;
	return vec3len(crossvec3(p0p, p01))/h <= r;
}

void
materializefrustum(void)
{
	Primitive l;
	Point3 p[4];
	int i;

	p[0] = Pt3(0,0,1,1);
	p[1] = Pt3(Dx(cam->view->r),0,1,1);
	p[2] = Pt3(Dx(cam->view->r),Dy(cam->view->r),1,1);
	p[3] = Pt3(0,Dy(cam->view->r),1,1);
	memset(&l, 0, sizeof l);
	l.type = PLine;
	l.v[0].c = l.v[1].c = Pt3(1,1,1,1);

	for(i = 0; i < nelem(p); i++){
		/* front frame */
		l.v[0].p = world2model(subject, viewport2world(cam, p[i]));
		l.v[1].p = world2model(subject, viewport2world(cam, p[(i+1)%nelem(p)]));
		qlock(&scenelk);
		model->addprim(model, l);
		qunlock(&scenelk);

		/* middle frame */
		l.v[0].p = world2model(subject, viewport2world(cam, subpt3(p[i], Vec3(0,0,0.5))));
		l.v[1].p = world2model(subject, viewport2world(cam, subpt3(p[(i+1)%nelem(p)], Vec3(0,0,0.5))));
		qlock(&scenelk);
		model->addprim(model, l);
		qunlock(&scenelk);

		/* back frame */
		l.v[0].p = world2model(subject, viewport2world(cam, subpt3(p[i], Vec3(0,0,1))));
		l.v[1].p = world2model(subject, viewport2world(cam, subpt3(p[(i+1)%nelem(p)], Vec3(0,0,1))));
		qlock(&scenelk);
		model->addprim(model, l);
		qunlock(&scenelk);

		/* struts */
		l.v[1].p = world2model(subject, viewport2world(cam, p[i]));
		qlock(&scenelk);
		model->addprim(model, l);
		qunlock(&scenelk);
	}
}

void
addcube(void)
{
	static Point3 axis[3] = {{0,1,0,0}, {1,0,0,0}, {0,0,1,0}};
	Primitive t[2];
	Point3 p, v1, v2;
	int i, j, k;

	memset(t, 0, sizeof t);
	t[0].type = t[1].type = PTriangle;

	/* build the first face/quad, facing the positive z axis */
	p = Vec3(-0.5,-0.5,0.5);
	v1 = Vec3(1,0,0);
	v2 = Vec3(0,1,0);
	t[0].v[0].p = addpt3(center, p);
	t[0].v[0].n = t[0].v[1].n = t[0].v[2].n = t[1].v[2].n = Vec3(0,0,1);
	t[0].v[1].p = addpt3(center, addpt3(p, v1));
	t[0].v[2].p = addpt3(center, addpt3(p, addpt3(v1, v2)));
	t[0].v[0].c = t[0].v[1].c = t[0].v[2].c = Pt3(1,1,1,1);
	t[1].v[0] = t[0].v[0];
	t[1].v[1] = t[0].v[2];
	t[1].v[2].p = addpt3(center, addpt3(p, v2));
	t[1].v[2].c = Pt3(1,1,1,1);

	/* make a cube by rotating the reference face */
	for(i = 0; i < 6; i++){
		if(i > 0)
			for(j = 0; j < 2; j++)
				for(k = 0; k < 3; k++){
					t[j].v[k].p = qrotate(t[j].v[k].p, axis[i%3], PI/2);
					t[j].v[k].n = qrotate(t[j].v[k].n, axis[i%3], PI/2);
				}

		qlock(&scenelk);
		model->addprim(model, t[0]);
		model->addprim(model, t[1]);
		qunlock(&scenelk);
	}
}

static void
addbasis(Scene *s)
{
	Entity *e;
	Model *m;
	Primitive prims[3];

	m = newmodel();
	e = newentity("basis", m);

	memset(prims, 0, sizeof prims);
	prims[0].type = prims[1].type = prims[2].type = PLine;
	prims[0].v[0].p = prims[1].v[0].p = prims[2].v[0].p = center;
	prims[0].v[0].c = prims[1].v[0].c = prims[2].v[0].c = Pt3(0,0,0,1);
	prims[0].v[1].p = addpt3(center, e->bx);
	prims[0].v[1].c = Pt3(1,0,0,1);
	prims[1].v[1].p = addpt3(center, e->by);
	prims[1].v[1].c = Pt3(0,1,0,1);
	prims[2].v[1].p = addpt3(center, e->bz);
	prims[2].v[1].c = Pt3(0,0,1,1);

	m->addprim(m, prims[0]);
	m->addprim(m, prims[1]);
	m->addprim(m, prims[2]);

	s->addent(s, e);
}

static void
setupcompass(Compass *c, Rectangle r, Renderer *rctl)
{
	static int scale = 3;

	r.max.x = r.min.x + Dx(r)/scale;
	r.max.y = r.min.y + Dy(r)/scale;

	c->cam = Cam(rectsubpt(r, r.min), rctl, PERSPECTIVE, 30*DEG, 0.1, 10);
	c->cam->view->p = Pt2(r.min.x,r.min.y,1);
	c->cam->view->setscale(c->cam->view, scale, scale);

	c->scn = newscene(nil);
	addbasis(c->scn);
	placecamera(c->cam, c->scn, camcfg.p, center, Vec3(0,1,0));
}

void
zoomin(void)
{
	cam->fov = fclamp(cam->fov - 1*DEG, 1*DEG, 180*DEG);
	reloadcamera(cam);
}

void
zoomout(void)
{
	cam->fov = fclamp(cam->fov + 1*DEG, 1*DEG, 180*DEG);
	reloadcamera(cam);
}

void
drawopmode(void)
{
	static char *opmodestr[] = {
	 [OMOrbit]	"ORBIT",
	 [OMSelect]	"SELECT",
			nil
	};
	Point p;

	p = Pt(screen->r.min.x + 10, screen->r.max.y - font->height-10);
	stringbg(screen, p, display->white, ZP, font, opmodestr[opmode], display->black, ZP);
}

void
drawstats(void)
{
	Point p;
	int i;

	snprint(stats[Sfov], sizeof(stats[Sfov]), "FOV %g°", cam->fov/DEG);
	snprint(stats[Scampos], sizeof(stats[Scampos]), "%V", cam->p);
	snprint(stats[Scambx], sizeof(stats[Scambx]), "bx %V", cam->bx);
	snprint(stats[Scamby], sizeof(stats[Scamby]), "by %V", cam->by);
	snprint(stats[Scambz], sizeof(stats[Scambz]), "bz %V", cam->bz);
	snprint(stats[Sfps], sizeof(stats[Sfps]), "FPS %.0f/%.0f/%.0f/%.0f",
		!cam->stats.max? 0: 1e9/cam->stats.max,
		!cam->stats.avg? 0: 1e9/cam->stats.avg,
		!cam->stats.min? 0: 1e9/cam->stats.min,
		!cam->stats.v? 0: 1e9/cam->stats.v);
	snprint(stats[Sframes], sizeof(stats[Sframes]), "frame %llud", cam->stats.nframes);
	for(i = 0; i < Se; i++){
		p = addpt(screen->r.min, Pt(10,10 + i*font->height));
		stringbg(screen, p, display->black, ZP, font, stats[i], display->white, ZP);
	}
}

void
redraw(void)
{
	lockdisplay(display);
	draw(screen, screen->r, screenb, nil, ZP);
	drawopmode();
	if(showhud)
		drawstats();
	flushimage(display, 1);
	unlockdisplay(display);
}

void
renderproc(void *)
{
	static Image *bg, *mist;
	uvlong t0, Δt;

	threadsetname("renderproc");

	bg = eallocimage(display, Rect(0,0,32,32), XRGB32, 1, DNofill);
	mist = eallocimage(display, UR, RGBA32, 1, 0xDF);
	draw(bg, Rect( 0, 0,16,16), display->white, nil, ZP);
	draw(bg, Rect(16, 0,32,16), display->black, nil, ZP);
	draw(bg, Rect( 0,16,16,32), display->black, nil, ZP);
	draw(bg, Rect(16,16,32,32), display->white, nil, ZP);
	draw(bg, bg->r, mist, nil, ZP);
	freeimage(mist);

	t0 = nsec();
	for(;;){
		qlock(&scenelk);
		shootcamera(cam, shader);
		qunlock(&scenelk);

		shootcamera(compass.cam, getshader("ident"));

		Δt = nsec() - t0;
		if(Δt > HZ2MS(60)*1000000ULL){
			lockdisplay(display);
			draw(screenb, screenb->r, bg, nil, ZP);
			cam->view->draw(cam->view, screenb, nil);
			compass.cam->view->draw(compass.cam->view, screenb, nil);
			unlockdisplay(display);

			nbsend(drawc, nil);
			t0 += Δt;
		}
	}
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
lmb(void)
{
	static Quaternion orient = {1,0,0,0};
	Quaternion Δorient;
	Point3 p, cp0, cp1, cv;
	double cr;
	int i, j;

	switch(opmode){
	case OMOrbit:
		if((om.buttons^mctl->buttons) != 0)
			break;

		Δorient = orient;
		qball(screen->r, om.xy, mctl->xy, &orient, nil);
		Δorient = mulq(Δorient, invq(orient));

		/* orbit camera around the center */
		p = subpt3(cam->p, center);
		p = vcs2world(cam, qsandwichpt3(Δorient, world2vcs(cam, p)));
		p.w = cam->p.w;
		movecamera(cam, p);
		aimcamera(cam, center);

		/* same for the compass */
		p = subpt3(compass.cam->p, center);
		p = vcs2world(compass.cam, qsandwichpt3(Δorient, world2vcs(compass.cam, p)));
		p.w = compass.cam->p.w;
		movecamera(compass.cam, p);
		aimcamera(compass.cam, center);
		break;
	case OMSelect:
		if((om.buttons^mctl->buttons) == 0)
			break;

		mctl->xy = subpt(mctl->xy, screen->r.min);
		cp0 = viewport2world(cam, Pt3(mctl->xy.x, mctl->xy.y, 1, 1));
		cp1 = viewport2world(cam, Pt3(mctl->xy.x, mctl->xy.y, 0, 1));
		cv = viewport2world(cam, Pt3(mctl->xy.x+10, mctl->xy.y, 1, 1));
		cr = vec3len(subpt3(cv, cp0)) * cam->clip.f/cam->clip.n;

		for(i = 0; i < model->nprims; i++)
			for(j = 0; j < model->prims[i].type+1; j++){
				if(ptincone(model->prims[i].v[j].p, cam->p, cp1, cr))
					model->prims[i].v[j].c = Pt3(0.5,0.5,0,1);
			}
		break;
	}
}

void
mmb(void)
{
	enum {
		TSNEAR,
		TSBILI,
		SP,
		QUIT,
	};
	static char *items[] = {
	 [TSNEAR]	"use nearest sampler",
	 [TSBILI]	"use bilinear sampler",
	 [SP]	"",
	 [QUIT]	"quit",
		nil,
	};
	static Menu menu = { .item = items };

	lockdisplay(display);
	switch(menuhit(2, mctl, &menu, _screen)){
	case TSNEAR:
		tsampler = neartexsampler;
		break;
	case TSBILI:
		tsampler = bilitexsampler;
		break;
	case QUIT:
		threadexitsall(nil);
	}
	unlockdisplay(display);
	nbsend(drawc, nil);
}

static char *
genrmbmenuitem(int idx)
{
	static char *items[] = {
		"",
		"add cube",
		nil
	};
	if(idx < nelem(shadertab))
		return shadertab[idx].name;
	idx -= nelem(shadertab);
	return items[idx];
}

void
rmb(void)
{
	enum {
		SP,
		ADDCUBE,
	};
	static Menu menu = { .gen = genrmbmenuitem };
	int idx;

	lockdisplay(display);
	idx = menuhit(3, mctl, &menu, _screen);
	if(idx < 0)
		goto nohit;
	if(idx < nelem(shadertab)){
		shader = &shadertab[idx];
		memset(&cam->stats, 0, sizeof(cam->stats));
	}
	idx -= nelem(shadertab);
	switch(idx){
	case ADDCUBE:
		addcube();
		break;
	}
nohit:
	unlockdisplay(display);
	nbsend(drawc, nil);
}

void
mouse(void)
{
	if((mctl->buttons & 1) != 0)
		lmb();
	if((mctl->buttons & 2) != 0)
		mmb();
	if((mctl->buttons & 4) != 0)
		rmb();
	if((mctl->buttons & 8) != 0)
		zoomin();
	if((mctl->buttons & 16) != 0)
		zoomout();
	om = mctl->Mouse;
}

void
kbdproc(void *)
{
	Rune r, *a;
	char buf[128], *s;
	int fd, n;

	threadsetname("kbdproc");

	if((fd = open("/dev/kbd", OREAD)) < 0)
		sysfatal("kbdproc: %r");
	memset(buf, 0, sizeof buf);

	for(;;){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if(buf[0] == 0){
			if((n = read(fd, buf, sizeof(buf)-1)) <= 0)
				break;
			buf[n-1] = 0;
			buf[n] = 0;
		}
		if(buf[0] == 'c'){
			chartorune(&r, buf+1);
			if(r == Kdel){
				close(fd);
				threadexitsall(nil);
			}else
				nbsend(kctl->c, &r);
		}
		if(buf[0] != 'k' && buf[0] != 'K')
			continue;
		s = buf+1;
		kdown = 0;
		while(*s){
			s += chartorune(&r, s);
			for(a = keys; a < keys+Ke; a++)
				if(r == *a){
					kdown |= 1 << a-keys;
					break;
				}
		}
	}
}

void
keyproc(void *c)
{
	threadsetname("keyproc");

	for(;;){
		nbsend(c, nil);
		sleep(HZ2MS(100));	/* key poll rate */
	}
}

void
handlekeys(void)
{
	static int okdown;

	if((okdown & 1<<Kmodeorb) == 0 && (kdown & 1<<Kmodeorb) != 0)
		opmode = OMOrbit;
	else if((okdown & 1<<Kmodesel) == 0 && (kdown & 1<<Kmodesel) != 0)
		opmode = OMSelect;

	if(kdown & 1<<Kzoomin)
		zoomin();
	if(kdown & 1<<Kzoomout)
		zoomout();

	if((okdown & 1<<Khud) == 0 && (kdown & 1<<Khud) != 0)
		showhud ^= 1;

	if((okdown & 1<<Kfrustum) == 0 && (kdown & 1<<Kfrustum) != 0)
		materializefrustum();

	okdown = kdown;
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
	Channel *keyc;

	GEOMfmtinstall();
	ARGBEGIN{
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	confproc();

	if((shader = getshader("gouraud")) == nil)
		sysfatal("couldn't find main shader");

	scene = newscene(nil);
	model = newmodel();
	subject = newentity("main", model);
	scene->addent(scene, subject);

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((rctl = initgraphics()) == nil)
		sysfatal("initgraphics: %r");
	if(initdraw(nil, nil, "med") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	rctl->doprof = doprof;

	screenb = eallocimage(display, rectsubpt(screen->r, screen->r.min), XRGB32, 0, DNofill);
	cam = Cam(screenb->r, rctl, camcfg.ptype, camcfg.fov, camcfg.clipn, camcfg.clipf);
	placecamera(cam, scene, camcfg.p, camcfg.lookat, camcfg.up);
	light.p = Pt3(0,100,100,1);
	light.c = Pt3(1,1,1,1);
	light.type = LightPoint;
	tsampler = neartexsampler;

	setupcompass(&compass, rectaddpt(Rect(0,0,100,100), subpt(screenb->r.max, Pt(100,100))), rctl);

	kctl = emalloc(sizeof *kctl);
	kctl->c = chancreate(sizeof(Rune), 16);
	keyc = chancreate(sizeof(void*), 1);
	drawc = chancreate(sizeof(void*), 1);
	display->locking = 1;
	unlockdisplay(display);

	proccreate(kbdproc, nil, mainstacksize);
	proccreate(keyproc, keyc, mainstacksize);
	proccreate(renderproc, nil, mainstacksize);
	proccreate(drawproc, nil, mainstacksize);

	for(;;){
		enum {MOUSE, RESIZE, KEY};
		Alt a[] = {
			{mctl->c, &mctl->Mouse, CHANRCV},
			{mctl->resizec, nil, CHANRCV},
			{keyc, nil, CHANRCV},
			{nil, nil, CHANEND}
		};
		switch(alt(a)){
		case MOUSE: mouse(); break;
		case RESIZE: resize(); break;
		case KEY: handlekeys(); break;
		}
	}
}
