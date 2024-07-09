#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "libgraphics/graphics.h"
#include "fns.h"

enum {
	K↑,
	K↓,
	K←,
	K→,
	Krise,
	Kfall,
	KR↑,
	KR↓,
	KR←,
	KR→,
	KR↺,
	KR↻,
	Kzoomin,
	Kzoomout,
	Khud,
	Ke
};

enum {
	Sfov,
	Scampos,
	Scambx, Scamby, Scambz,
	Sfps,
	Sframes,
	Splanet,
	Se
};

enum {
	Cmdwinht = 50,
	Cmdmargin = 10,
	Cmdpadding = 3,

	Cmdlookat = 0,
	Cmdgoto,
};

typedef struct Planet Planet;
typedef struct Camcfg Camcfg;
typedef struct HReq HReq;
typedef struct Cmdbut Cmdbut;
typedef struct Cmdbox Cmdbox;

struct Planet
{
	int id;		/* Horizons API ID */
	char *name;
	double scale;
	Entity *body;
	Material *mtl;
};

struct Camcfg
{
	Point3 p, lookat, up;
	double fov, clipn, clipf;
	int ptype;
};

struct HReq
{
	int pfd[2];
	int pid;	/* planet id */
	char *t0, *t1;	/* start and end times */
};

struct Cmdbut
{
	char *label;
	Rectangle r;
	void (*handler)(Cmdbut*);
};

struct Cmdbox
{
	Rectangle r;
	Cmdbut *cmds;
	ulong ncmds;
};

Rune keys[Ke] = {
 [K↑]		= Kup,
 [K↓]		= Kdown,
 [K←]		= Kleft,
 [K→]		= Kright,
 [Krise]	= Kpgup,
 [Kfall]	= Kpgdown,
 [KR↑]		= 'w',
 [KR↓]		= 's',
 [KR←]		= 'a',
 [KR→]		= 'd',
 [KR↺]		= 'q',
 [KR↻]		= 'e',
 [Kzoomin]	= 'z',
 [Kzoomout]	= 'x',
 [Khud]		= 'h',
};
char *skyboxpaths[] = {
	"cubemap/solar/left.pic",
	"cubemap/solar/right.pic",
	"cubemap/solar/bottom.pic",
	"cubemap/solar/top.pic",
	"cubemap/solar/front.pic",
	"cubemap/solar/back.pic",
};
Planet planets[] = {
	{ .id = 10,	.name = "Sol",		.scale = 695700 },
	{ .id = 1,	.name = "Mercury",	.scale = 2439.4 },
	{ .id = 2,	.name = "Venus",	.scale = 6051.8 },
	{ .id = 399,	.name = "Earth",	.scale = 6371.0084 },
	{ .id = 301,	.name = "Luna",		.scale = 1737.4 },
	{ .id = 4,	.name = "Mars",		.scale = 3389.50 },
	{ .id = 5,	.name = "Jupiter",	.scale = 69911 },
	{ .id = 6,	.name = "Saturn",	.scale = 58232 },
	{ .id = 7,	.name = "Uranus",	.scale = 25362 },
	{ .id = 8,	.name = "Neptune",	.scale = 24622 },
	{ .id = 9,	.name = "Pluto",	.scale = 1188.3 },
};
Planet *selplanet;
char stats[Se][256];
char datefmt[] = "YYYY-MM-DD";
Rectangle viewr, cmdr;
Cmdbox cmdbox;
Image *screenb;
Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;
Mouse om;
int kdown;
Tm date;
char datestr[16];
Scene *scene;
QLock drawlk;

Camera camera;
Camcfg cameracfg = {
	0,0,0,1,
	0,0,0,1,
	0,1,0,0,
	80*DEG, 1, 1e12, PERSPECTIVE
};
Point3 center = {0,0,0,1};
double speed = 10;

static int museummode;
static int showskybox;
static int doprof;
static int showhud;

static int
min(int a, int b)
{
	return a < b? a: b;
}

static int
max(int a, int b)
{
	return a > b? a: b;
}

static void
sailor(void *arg)
{
	char buf[128], pidstr[8];
	HReq *r;

	r = arg;

	close(r->pfd[0]);
	dup(r->pfd[1], 1);
	close(r->pfd[1]);

	getwd(buf, sizeof(buf)-1);
	snprint(buf+strlen(buf), sizeof(buf)-strlen(buf), "/tools/horizonget");
	snprint(pidstr, sizeof pidstr, "%d", r->pid);

	execl(buf, "horizonget", pidstr, r->t0, r->t1, nil);
	sysfatal("execl: %r");
}

static char *
getplanetstate(int id, Tm *t)
{
	Biobuf *bin;
	char *line, *lastline, t0[16], t1[16];
	HReq r;

	lastline = nil;
	r.pid = id;
	r.t0 = t0;
	r.t1 = t1;

	snprint(t1, sizeof t1, "%τ", tmfmt(t, datefmt));
	t->mday--;
	snprint(t0, sizeof t0, "%τ", tmfmt(t, datefmt));
	t->mday++;

	if(pipe(r.pfd) < 0)
		sysfatal("pipe: %r");
	switch(fork()){
	case -1:
		sysfatal("fork: %r");
	case 0:
		sailor(&r);
	default:
		close(r.pfd[1]);
		bin = Bfdopen(r.pfd[0], OREAD);
		if(bin == nil)
			sysfatal("Bfdopen: %r");
		while((line = Brdline(bin, '\n')) != nil){
			line[Blinelen(bin)-1] = 0;
			lastline = line;
		}
		if(lastline != nil)
			lastline = strdup(lastline);
		Bterm(bin);
		close(r.pfd[0]);
	}

	return lastline;
}

void
updateplanets(void)
{
	char *s, *p;
	int i;

	fprint(2, "loading planet states...\n");
	for(i = 1; i < nelem(planets); i++){
		s = getplanetstate(planets[i].id, &date);
		if(s == nil){
			fprint(2, "couldn't load planet: %s", planets[i].name);
			continue;
		}
		p = strchr(s, '=');
		planets[i].body->p.x = strtod(++p, nil);
		p = strchr(p, '=');
		planets[i].body->p.y = strtod(++p, nil);
		p = strchr(p, '=');
		planets[i].body->p.z = strtod(++p, nil);
		planets[i].body->p.w = 1;
		free(s);
		fprint(2, "%s ready\n", planets[i].name);
	}
}

static Planet *
getplanet(char *name)
{
	int i;

	for(i = 0; i < nelem(planets); i++)
		if(strcmp(planets[i].name, name) == 0)
			return &planets[i];
	return nil;
}

static Planet *
getentityplanet(Entity *e)
{
	int i;

	for(i = 0; i < nelem(planets); i++)
		if(e == planets[i].body)
			return &planets[i];
	return nil;
}

static void
gotoplanet(Planet *p)
{
	placecamera(&camera, addpt3(p->body->p, Vec3(0,0,1.5*p->scale)), p->body->p, cameracfg.up);
}

/*
 * ray-sphere (planet) intersection
 */
int
rayXsphere(Point3 *rp, Point3 p0, Point3 u, Point3 c, double r)
{
	Point3 dp;
	double u·dp, Δ, d;
	int n;

	dp = subpt3(p0, c);
	u·dp = dotvec3(u, dp);
	if(u·dp > 0)	/* ignore what's behind */
		return 0;

	Δ = u·dp*u·dp - dotvec3(dp, dp) + r*r;
	if(Δ < 0)		/* no intersection */
		n = 0;
	else if(Δ == 0){	/* tangent */
		if(rp != nil){
			d = -u·dp;
			rp[0] = addpt3(p0, mulpt3(u, d));
		}
		n = 1;
	}else{			/* secant */
		if(rp != nil){
			d = -u·dp + sqrt(Δ);
			rp[0] = addpt3(p0, mulpt3(u, d));
			d = -u·dp - sqrt(Δ);
			rp[1] = addpt3(p0, mulpt3(u, d));
		}
		n = 2;
	}
	return n;
}

Point3
identvshader(VSparams *sp)
{
	Planet *p;
	Point3 pos;

	p = getentityplanet(sp->su->entity);
	assert(p != nil);

	Matrix3 S = {
		p->scale, 0, 0, 0,
		0, p->scale, 0, 0,
		0, 0, p->scale, 0,
		0, 0, 0, 1,
	};
	pos = xform3(sp->v->p, S);

	sp->v->mtl = p->mtl;
	sp->v->c = p->mtl->diffuse;
	return world2clip(sp->su->camera, model2world(sp->su->entity, pos));
}

Color
identshader(FSparams *sp)
{
	if(sp->v.mtl != nil && sp->v.mtl->diffusemap != nil && sp->v.uv.w != 0)
		return sampletexture(sp->v.mtl->diffusemap, sp->v.uv, neartexsampler);
	return sp->v.c;
}

Shadertab shader = { "ident", identvshader, identshader };

void
zoomin(void)
{
	camera.fov = fclamp(camera.fov - 1*DEG, 1*DEG, 180*DEG);
	reloadcamera(&camera);
}

void
zoomout(void)
{
	camera.fov = fclamp(camera.fov + 1*DEG, 1*DEG, 180*DEG);
	reloadcamera(&camera);
}

void
drawstats(void)
{
	int i;

	snprint(stats[Sfov], sizeof(stats[Sfov]), "FOV %g°", camera.fov/DEG);
	snprint(stats[Scampos], sizeof(stats[Scampos]), "%V", camera.p);
	snprint(stats[Scambx], sizeof(stats[Scambx]), "bx %V", camera.bx);
	snprint(stats[Scamby], sizeof(stats[Scamby]), "by %V", camera.by);
	snprint(stats[Scambz], sizeof(stats[Scambz]), "bz %V", camera.bz);
	snprint(stats[Sfps], sizeof(stats[Sfps]), "FPS %.0f/%.0f/%.0f/%.0f", !camera.stats.max? 0: 1e9/camera.stats.max, !camera.stats.avg? 0: 1e9/camera.stats.avg, !camera.stats.min? 0: 1e9/camera.stats.min, !camera.stats.v? 0: 1e9/camera.stats.v);
	snprint(stats[Sframes], sizeof(stats[Sframes]), "frame %llud", camera.stats.nframes);
	snprint(stats[Splanet], sizeof(stats[Splanet]), "%s", selplanet == nil? "": selplanet->name);
	for(i = 0; i < Se; i++)
		stringbg(screen, addpt(screen->r.min, Pt(10,10 + i*font->height)), display->black, ZP, font, stats[i], display->white, ZP);
}

void
redraw(void)
{
	int i;

	lockdisplay(display);
	camera.vp->draw(camera.vp, screenb);
	draw(screen, rectaddpt(viewr, screen->r.min), display->black, nil, ZP);
	draw(screen, rectaddpt(viewr, screen->r.min), screenb, nil, ZP);
	draw(screen, rectaddpt(cmdbox.r, screen->r.min), display->white, nil, ZP);
	for(i = 0; i < cmdbox.ncmds; i++){
		border(screen, rectaddpt(cmdbox.cmds[i].r, screen->r.min), 1, display->black, ZP);
		string(screen, addpt(screen->r.min, addpt(cmdbox.cmds[i].r.min, Pt(Cmdpadding,Cmdpadding))), display->black, ZP, font, cmdbox.cmds[i].label);
	}
	if(showhud)
		drawstats();
	flushimage(display, 1);
	unlockdisplay(display);
}

void
renderproc(void *)
{
	uvlong t0, Δt;

	threadsetname("renderproc");

	t0 = nsec();
	for(;;){
		shootcamera(&camera, &shader);
		if(doprof)
		fprint(2, "R %llud %llud\nE %llud %llud\nT %llud %llud\nr %llud %llud\n\n",
			camera.times.R[camera.times.cur-1].t0, camera.times.R[camera.times.cur-1].t1,
			camera.times.E[camera.times.cur-1].t0, camera.times.E[camera.times.cur-1].t1,
			camera.times.Tn[camera.times.cur-1].t0, camera.times.Tn[camera.times.cur-1].t1,
			camera.times.Rn[camera.times.cur-1].t0, camera.times.Rn[camera.times.cur-1].t1);
		Δt = nsec() - t0;
		if(Δt > HZ2MS(60)*1000000ULL){
			nbsend(drawc, nil);
			t0 += Δt;
		}
	}
}

void
drawproc(void *)
{
	threadsetname("drawproc");

	for(;;)
		if(recv(drawc, nil) && canqlock(&drawlk)){
			redraw();
			qunlock(&drawlk);
		}
}

static char *
genplanetmenu(int idx)
{
	if(idx < nelem(planets))
		return planets[idx].name;
	return nil;
}

void
lookat_cmd(Cmdbut *)
{
	static Menu menu = { .gen = genplanetmenu };
	Planet *p;
	int idx;

	qlock(&drawlk);
	idx = menuhit(1, mctl, &menu, _screen);
	if(idx >= 0){
		p = &planets[idx];
		placecamera(&camera, camera.p, p->body->p, cameracfg.up);
	}
	qunlock(&drawlk);
	nbsend(drawc, nil);
}

void
goto_cmd(Cmdbut *)
{
	static Menu menu = { .gen = genplanetmenu };
	int idx;

	qlock(&drawlk);
	idx = menuhit(1, mctl, &menu, _screen);
	if(idx >= 0)
		gotoplanet(&planets[idx]);
	qunlock(&drawlk);
	nbsend(drawc, nil);
}

void
date_cmd(Cmdbut *)
{
	Tm t;
	char buf[16];

	if(museummode)
		return;

	memmove(buf, datestr, sizeof buf);
	qlock(&drawlk);
	if(enter("new date", buf, sizeof buf, mctl, kctl, nil) <= 0)
		goto nodate;
	if(tmparse(&t, datefmt, buf, nil, nil) == nil)
		goto nodate;
	date = t;
	snprint(datestr, sizeof datestr, "%τ", tmfmt(&date, datefmt));
	updateplanets();
nodate:
	qunlock(&drawlk);
	nbsend(drawc, nil);
}

Cmdbut cmds[] = {
	{ .label = "look at", .handler = lookat_cmd },
	{ .label = "go to", .handler = goto_cmd },
	{ .label = datestr, .handler = date_cmd },
};

void
lmb(void)
{
	Point3 p0, u;
	Point mp;
	Planet *p;
	Cmdbut *cmd;
	double lastz, z;
	int i;

	if((om.buttons ^ mctl->buttons) == 0)
		return;

	mp = subpt(mctl->xy, screen->r.min);
	if(ptinrect(mp, viewr)){
		p0 = viewport2world(&camera, Pt3(mp.x,mp.y,1,1));
		u = normvec3(subpt3(p0, camera.p));
		p = nil;
		lastz = Inf(1);

		for(i = 0; i < nelem(planets); i++)
			if(rayXsphere(nil, p0, u, planets[i].body->p, planets[i].scale) > 0){
				z = vec3len(subpt3(planets[i].body->p, camera.p));
				/* select the closest one */
				if(z < lastz){
					lastz = z;
					p = &planets[i];
				}
			}
		if(p != nil)
			selplanet = p;
		return;
	}

	cmd = nil;
	for(i = 0; i < cmdbox.ncmds; i++)
		if(ptinrect(mp, cmdbox.cmds[i].r))
			cmd = &cmdbox.cmds[i];

	if(cmd == nil)
		return;
	cmd->handler(cmd);
}

void
mmb(void)
{
	enum {
		CHGSPEED,
		QUIT,
	};
	static char *items[] = {
	 [CHGSPEED]	"change speed",
	 [QUIT]	"quit",
		nil,
	};
	static Menu menu = { .item = items };
	char buf[128];

	if((om.buttons ^ mctl->buttons) == 0)
		return;

	qlock(&drawlk);
	switch(menuhit(2, mctl, &menu, _screen)){
	case CHGSPEED:
		snprint(buf, sizeof buf, "%g", speed);
		if(enter("speed (km)", buf, sizeof buf, mctl, kctl, nil) > 0)
			speed = fabs(strtod(buf, nil));
		break;
	case QUIT:
		threadexitsall(nil);
	}
	qunlock(&drawlk);
	nbsend(drawc, nil);
}

void
mouse(void)
{
	if((mctl->buttons & 1) != 0)
		lmb();
	if((mctl->buttons & 2) != 0)
		mmb();
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

	if(kdown & 1<<K↑)
		placecamera(&camera, subpt3(camera.p, mulpt3(camera.bz, speed)), camera.bz, camera.by);
	if(kdown & 1<<K↓)
		placecamera(&camera, addpt3(camera.p, mulpt3(camera.bz, speed)), camera.bz, camera.by);
	if(kdown & 1<<K←)
		placecamera(&camera, subpt3(camera.p, mulpt3(camera.bx, speed)), camera.bz, camera.by);
	if(kdown & 1<<K→)
		placecamera(&camera, addpt3(camera.p, mulpt3(camera.bx, speed)), camera.bz, camera.by);
	if(kdown & 1<<Krise)
		placecamera(&camera, addpt3(camera.p, mulpt3(camera.by, speed)), camera.bz, camera.by);
	if(kdown & 1<<Kfall)
		placecamera(&camera, subpt3(camera.p, mulpt3(camera.by, speed)), camera.bz, camera.by);
	if(kdown & 1<<KR↑)
		aimcamera(&camera, qrotate(camera.bz, camera.bx, 1*DEG));
	if(kdown & 1<<KR↓)
		aimcamera(&camera, qrotate(camera.bz, camera.bx, -1*DEG));
	if(kdown & 1<<KR←)
		aimcamera(&camera, qrotate(camera.bz, camera.by, 1*DEG));
	if(kdown & 1<<KR→)
		aimcamera(&camera, qrotate(camera.bz, camera.by, -1*DEG));
	if(kdown & 1<<KR↺)
		placecamera(&camera, camera.p, camera.bz, qrotate(camera.by, camera.bz, 1*DEG));
	if(kdown & 1<<KR↻)
		placecamera(&camera, camera.p, camera.bz, qrotate(camera.by, camera.bz, -1*DEG));
	if(kdown & 1<<Kzoomin)
		zoomin();
	if(kdown & 1<<Kzoomout)
		zoomout();

	if((okdown & 1<<Khud) == 0 && (kdown & 1<<Khud) != 0)
		showhud ^= 1;

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
	fprint(2, "usage: %s [-ms]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Viewport *v;
	Renderer *rctl;
	Channel *keyc;
	Entity *subject;
	Model *model;
	OBJ *obj;
	Point lblsiz;
	int i, j;

	tmfmtinstall();
	GEOMfmtinstall();
	ARGBEGIN{
	case 'm': museummode++; break;
	case 's': showskybox++; break;
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	confproc();

	if((obj = objparse("mdl/planet.obj")) == nil)
		sysfatal("objparse: %r");
	model = newmodel();
	loadobjmodel(model, obj);
	objfree(obj);
	/*
	 * normalize the vertices so that we can scale
	 * each planet based on its radius
	 */
	for(i = 0; i < model->nprims; i++)
		for(j = 0; j < model->prims[i].type+1; j++){
			model->prims[i].v[j].p = normvec3(model->prims[i].v[j].p);
			model->prims[i].v[j].p.w = 1;
		}
	scene = newscene(nil);
	for(i = 0; i < nelem(planets); i++){
		subject = newentity(model);
		scene->addent(scene, subject);
		planets[i].body = subject;
		for(j = 0; j < model->nmaterials; j++)
			if(strcmp(planets[i].name, model->materials[j].name) == 0)
				planets[i].mtl = &model->materials[j];
		if(i == 0){
			subject->p = Pt3(0,0,0,1);
			continue;
		}else if(museummode)
			subject->p.x = planets[i-1].body->p.x + 1.5*planets[i-1].scale + planets[i].scale;
	}
	tmnow(&date, nil);
	snprint(datestr, sizeof datestr, "%τ", tmfmt(&date, datefmt));
	if(!museummode)
		updateplanets();
	if(showskybox)
		scene->skybox = readcubemap(skyboxpaths);

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((rctl = initgraphics()) == nil)
		sysfatal("initgraphics: %r");
	if(initdraw(nil, nil, "solar") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	viewr = rectsubpt(Rpt(screen->r.min, subpt(screen->r.max, Pt(0,Cmdwinht))), screen->r.min);
	cmdbox.r = Rect(viewr.min.x, viewr.max.y, Dx(viewr), Dy(screen->r));
	cmdbox.cmds = cmds;
	cmdbox.ncmds = nelem(cmds);
	for(i = 0; i < nelem(cmds); i++){
		lblsiz = stringsize(font, cmds[i].label);
		cmds[i].r = Rect(0,0,Cmdpadding+lblsiz.x+Cmdpadding,Cmdpadding+lblsiz.y+Cmdpadding);
		if(i == 0)
			cmds[i].r = rectaddpt(cmds[i].r, addpt(cmdbox.r.min, Pt(Cmdmargin,Cmdmargin)));
		else
			cmds[i].r = rectaddpt(cmds[i].r, Pt(cmds[i-1].r.max.x+Cmdmargin,cmds[i-1].r.min.y));
	}
	screenb = eallocimage(display, viewr, XRGB32, 0, DNofill);
	v = mkviewport(screenb->r);
	placecamera(&camera, cameracfg.p, cameracfg.lookat, cameracfg.up);
	configcamera(&camera, v, cameracfg.fov, cameracfg.clipn, cameracfg.clipf, cameracfg.ptype);
	camera.scene = scene;
	camera.rctl = rctl;
	gotoplanet(getplanet("Sol"));

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
