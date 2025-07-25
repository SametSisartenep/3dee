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

#define SEC	(1000000000ULL)

enum {
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

typedef struct Usermsg Usermsg;
typedef struct Userlog Userlog;

struct Usermsg
{
	char *s;
	Image *i;
	uvlong eol;
	Usermsg *prev, *next;
};

struct Userlog
{
	QLock;
	Usermsg msgs;
	ulong nmsgs;
	ulong cap;

	void (*send)(Userlog*, char*, ...);
	void (*update)(Userlog*);
	void (*draw)(Userlog*);
	void (*delmsg)(Userlog*, Usermsg*);
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

Rune keys[Ke] = {
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
LightSource *light;	/* global point light */
Compass compass;	/* 3d compass */
Userlog *usrlog;

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

static Point
randptfromrect(Rectangle *r)
{
	if(badrect(*r))
		return r->min;
	return addpt(r->min, Pt(ntruerand(Dx(*r)), ntruerand(Dy(*r))));
}

static void
userlog_send(Userlog *l, char *msg, ...)
{
	Usermsg *m;
	Rectangle ar;	/* available spawn area */
	Point dim, off;
	va_list va;
	char buf[ERRMAX];

	m = emalloc(sizeof *m);
	memset(m, 0, sizeof *m);

	va_start(va, msg);
	vsnprint(buf, sizeof buf, msg, va);
	va_end(va);

	m->s = strdup(buf);
	if(m->s == nil){
		free(m);
		return;		/* lost message */
	}

	dim = stringsize(font, m->s);
	ar = screen->r;
	ar.max = subpt(ar.max, dim);
	off = randptfromrect(&ar);

	m->i = eallocimage(display, Rpt(off, addpt(off, dim)), XRGB32, 0, DNofill);
	stringbg(m->i, m->i->r.min, display->white, ZP, font, m->s, display->black, ZP);
	m->eol = nanosec() + 5*SEC;

	qlock(l);
	m->prev = l->msgs.prev;
	m->next = l->msgs.prev->next;
	l->msgs.prev->next = m;
	l->msgs.prev = m;
	l->nmsgs++;
	qunlock(l);
}

static void
userlog_delmsg(Userlog *l, Usermsg *m)
{
	m->prev->next = m->next;
	m->next->prev = m->prev;
	m->prev = m->next = nil;

	freeimage(m->i);
	free(m->s);
	free(m);

	l->nmsgs--;
}

static void
userlog_update(Userlog *l)
{
	Usermsg *m, *nm;

	qlock(l);
	for(m = l->msgs.next; m != &l->msgs; m = nm){
		nm = m->next;
		if(nanosec() >= m->eol)
			l->delmsg(l, m);
	}
	qunlock(l);
}

static void
userlog_draw(Userlog *l)
{
	Usermsg *m;

	qlock(l);
	for(m = l->msgs.next; m != &l->msgs; m = m->next)
		draw(screen, m->i->r, m->i, nil, m->i->r.min);
	qunlock(l);
}

Userlog *
mkuserlog(void)
{
	Userlog *l;

	l = emalloc(sizeof *l);
	memset(l, 0, sizeof *l);
	l->msgs.prev = l->msgs.next = &l->msgs;
	l->send = userlog_send;
	l->update = userlog_update;
	l->draw = userlog_draw;
	l->delmsg = userlog_delmsg;
	return l;
}

void
rmuserlog(Userlog *l)
{
	if(l->msgs.next != &l->msgs){
		l->delmsg(l, l->msgs.next);
		rmuserlog(l);
		return;
	}
	free(l);
}

void
materializefrustum(void)
{
	Primitive l;
	Vertex v;
	Point3 p[4];
	usize vidx0;
	int i, j;

	p[0] = Pt3(0,0,1,1);
	p[1] = Pt3(Dx(cam->view->r),0,1,1);
	p[2] = Pt3(Dx(cam->view->r),Dy(cam->view->r),1,1);
	p[3] = Pt3(0,Dy(cam->view->r),1,1);
	l = mkprim(PLine);
	v = mkvert();
	v.c = model->addcolor(model, Pt3(1,1,1,1));

	qlock(&scenelk);
	vidx0 = model->verts->nitems;
	/* add front, middle and back frames' vertices */
	for(i = 0; i < 3; i++)
		for(j = 0; j < nelem(p); j++){
			v.p = model->addposition(model, world2model(subject, viewport2world(cam, p[i])));
			p[i] = addpt3(p[i], Vec3(0,0,0.5));
			model->addvert(model, v);
		}

	/* build the frames */
	for(i = 0; i < 3; i++)
		for(j = 1; j <= nelem(p); j++){
			l.v[0] = vidx0 + 4*i + j-1;
			l.v[1] = vidx0 + 4*i + j%4;
			model->addprim(model, l);
		}

	/* connect the frames with struts */
	for(i = 0; i < nelem(p); i++)
		for(j = 1; j < 3; j++){
			l.v[0] = vidx0 + 4*(j-1) + i;
			l.v[1] = vidx0 + 4*j + i;
			model->addprim(model, l);
		}
	qunlock(&scenelk);
}

void
addcube(void)
{
	static int pindices[] = {
		/* front */
		0, 1, 4+1, 4+0,
		/* bottom */
		0, 3, 2, 1,
		/* right */
		1, 2, 4+2, 4+1,
		/* back */
		3, 4+3, 4+2, 2,
		/* top */
		4+0, 4+1, 4+2, 4+3,
		/* left */
		0, 4+0, 4+3, 3,
	};
	static Point3 axes[3] = {{0,1,0,0}, {1,0,0,0}, {0,0,1,0}};
	Primitive t;
	Vertex v;
	Point3 p;
	usize pidx0, nidx0, vidx0;
	int i;

	t = mkprim(PTriangle);
	v = mkvert();

	v.c = model->addcolor(model, Pt3(1,1,1,1));

	qlock(&scenelk);
	/* build bottom and top vertex quads around y-axis */
	pidx0 = model->positions->nitems;
	p = Pt3(-0.5,-0.5,0.5,1);
	for(i = 0; i < 8; i++){
		if(i == 4)
			p.y++;
		model->addposition(model, p);
		p = qrotate(p, Vec3(0,1,0), PI/2);
	}

	/* build normals */
	nidx0 = model->normals->nitems;
	p = Vec3(0,0,1);
	for(i = 0; i < 6; i++){
		model->addnormal(model, p);
		p = qrotate(p, axes[(i+1)%3], PI/2);
	}

	/* build vertices */
	vidx0 = model->verts->nitems;
	for(i = 0; i < nelem(pindices); i++){
		v.p = pidx0 + pindices[i];
		v.n = nidx0 + i/4;
		model->addvert(model, v);
	}

	/* triangulate vertices */
	for(i = 0; i < 6; i++){
		t.v[0] = vidx0 + 4*i + 0;
		t.v[1] = vidx0 + 4*i + 1;
		t.v[2] = vidx0 + 4*i + 2;
		model->addprim(model, t);
		t.v[1] = vidx0 + 4*i + 2;
		t.v[2] = vidx0 + 4*i + 3;
		model->addprim(model, t);
	}
	qunlock(&scenelk);
}

static void
addbasis(Scene *s)
{
	Entity *e;
	Model *m;
	Vertex v;
	Primitive l;
	int i;

	m = newmodel();
	e = newentity("basis", m);

	v = mkvert();
	v.p = m->addposition(m, center);
	v.c = m->addcolor(m, Pt3(0,0,0,1));
	m->addvert(m, v);
	v.p = m->addposition(m, addpt3(center, e->bx));
	v.c = m->addcolor(m, Pt3(1,0,0,1));
	m->addvert(m, v);
	v.p = m->addposition(m, addpt3(center, e->by));
	v.c = m->addcolor(m, Pt3(0,1,0,1));
	m->addvert(m, v);
	v.p = m->addposition(m, addpt3(center, e->bz));
	v.c = m->addcolor(m, Pt3(0,0,1,1));
	m->addvert(m, v);

	l = mkprim(PLine);
	l.v[0] = 0;
	for(i = 0; i < 3; i++){
		l.v[1] = i+1;
		m->addprim(m, l);
	}

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
	usrlog->draw(usrlog);
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

	t0 = nanosec();
	for(;;){
		qlock(&scenelk);
		shootcamera(cam, shader);
		qunlock(&scenelk);

		shootcamera(compass.cam, getshader("ident"));

		Δt = nanosec() - t0;
		if(Δt > HZ2NS(60)){
			lockdisplay(display);
			draw(screenb, screenb->r, bg, nil, ZP);
			cam->view->draw(cam->view, screenb, nil);
			compass.cam->view->draw(compass.cam->view, screenb, nil);
			unlockdisplay(display);

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
drawproc(void *)
{
	threadsetname("drawproc");

	for(;;){
		recv(drawc, nil);
		redraw();

		usrlog->update(usrlog);
	}
}

void
lmb(void)
{
	static Quaternion orient = {1,0,0,0};
	Quaternion Δorient;
	Point3 p, cp0, cp1, cv;
	Primitive *prim, *lastprim;
	Vertex *v;
	double cr;
	int i, cidx;

	cidx = -1;

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

		if(cidx < 0)
			cidx = model->addcolor(model, Pt3(0.5,0.5,0,1));

		lastprim = itemarrayget(model->prims, model->prims->nitems-1);
		for(prim = model->prims->items; prim <= lastprim; prim++)
			for(i = 0; i < prim->type+1; i++){
				v = itemarrayget(model->verts, prim->v[i]);
				p = *(Point3*)itemarrayget(model->positions, v->p);
				if(ptincone(p, cam->p, cp1, cr))
					v->c = cidx;
			}
		break;
	}
}

void
mmb(void)
{
	enum {
		ORBIT,
		SELECT,
		SP0,
		SAVE,
		SP1,
		QUIT,
	};
	static char *items[] = {
	 [ORBIT]	"orbit",
	 [SELECT]	"select",
			"",
	 [SAVE]		"save",
			"",
	 [QUIT]		"quit",
		nil,
	};
	static Menu menu = { .item = items };
	static char buf[256];
	int fd;

	lockdisplay(display);
	switch(menuhit(2, mctl, &menu, _screen)){
	case ORBIT:
		opmode = OMOrbit;
		break;
	case SELECT:
		opmode = OMSelect;
		break;
	case SAVE:
		if(enter("path", buf, sizeof buf, mctl, kctl, nil) <= 0)
			break;
		fd = create(buf, OWRITE, 0644);
		if(fd < 0){
			usrlog->send(usrlog, "create: %r");
			break;
		}
		writemodel(fd, model);
		close(fd);
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
	fprint(2, "usage: %s [mdlfile]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Renderer *rctl;
	Channel *keyc;
	char *mdlpath;
	int fd;

	GEOMfmtinstall();
	mdlpath = nil;
	ARGBEGIN{
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc == 1)
		mdlpath = argv[0];
	else if(argc > 1)
		usage();

	confproc();

	if((shader = getshader("gouraud")) == nil)
		sysfatal("couldn't find main shader");

	scene = newscene(nil);
	if(mdlpath != nil){
		fd = open(mdlpath, OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		model = readmodel(fd);
		if(model == nil)
			sysfatal("readmodel: %r");
		close(fd);
	}else
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
	light = newpointlight(Pt3(0,100,100,1), Pt3(1,1,1,1));
	light->cutoff = 10000;
	scene->addlight(scene, light);
	tsampler = neartexsampler;

	setupcompass(&compass, rectaddpt(Rect(0,0,100,100), subpt(screenb->r.max, Pt(100,100))), rctl);
	usrlog = mkuserlog();

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

	enum {MOUSE, RESIZE, KEY};
	Alt a[] = {
		{mctl->c, &mctl->Mouse, CHANRCV},
		{mctl->resizec, nil, CHANRCV},
		{keyc, nil, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;)
		switch(alt(a)){
		case MOUSE: mouse(); break;
		case RESIZE: resize(); break;
		case KEY: handlekeys(); break;
		}
}
