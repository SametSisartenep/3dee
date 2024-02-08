#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "libgraphics/graphics.h"
#include "dat.h"
#include "fns.h"

typedef struct Camcfg Camcfg;
struct Camcfg
{
	Point3 p, lookat, up;
	double fov, clipn, clipf;
	int ptype;
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
 [Kcam0]	= KF|1,
 [Kcam1]	= KF|2,
 [Kcam2]	= KF|3,
 [Kcam3]	= KF|4,
};
char stats[Se][256];
Memimage *screenfb;
Mousectl *mctl;
Channel *drawc;
int kdown;
OBJ *model;
Memimage *modeltex;
Shader *shader;
double θ, ω = 0;

Camera cams[4], *maincam;
Camcfg camcfgs[4] = {
	2,0,-4,1,
	0,0,0,1,
	0,1,0,0,
	0, 0.01, 100, ORTHOGRAPHIC,

	-2,0,-4,1,
	0,0,0,1,
	0,1,0,0,
	120*DEG, 0.01, 100, PERSPECTIVE,

	-2,0,4,1,
	0,0,0,1,
	0,1,0,0,
	0, 0.01, 100, ORTHOGRAPHIC,

	2,0,4,1,
	0,0,0,1,
	0,1,0,0,
	120*DEG, 0.01, 100, PERSPECTIVE
};
Point3 center = {0,0,0,1};
Point3 light = {0,1,1,1};	/* global point light */

static int doprof;
static int inception;

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

//void
//drawaxis(void)
//{
//	Point3	op = Pt3(0,0,0,1),
//		px = Pt3(1,0,0,1),
//		py = Pt3(0,1,0,1),
//		pz = Pt3(0,0,1,1);
//
//	line3(maincam, op, px, 0, Endarrow, display->black);
//	string3(maincam, px, display->black, font, "x");
//	line3(maincam, op, py, 0, Endarrow, display->black);
//	string3(maincam, py, display->black, font, "y");
//	line3(maincam, op, pz, 0, Endarrow, display->black);
//	string3(maincam, pz, display->black, font, "z");
//}

Point3
vertshader(VSparams *sp)
{
	sp->v->n = qrotate(sp->v->n, Vec3(0,1,0), θ+fmod(ω*sp->su->uni_time/1e9, 2*PI));
	sp->su->var_intensity[sp->idx] = fmax(0, dotvec3(sp->v->n, light));
	sp->v->p = qrotate(sp->v->p, Vec3(0,1,0), θ+fmod(ω*sp->su->uni_time/1e9, 2*PI));
	return world2clip(maincam, sp->v->p);
}

Memimage *
gouraudshader(FSparams *sp)
{
	double intens;

	intens = dotvec3(Vec3(sp->su->var_intensity[0], sp->su->var_intensity[1], sp->su->var_intensity[2]), sp->bc);
	sp->cbuf[1] *= intens;
	sp->cbuf[2] *= intens;
	sp->cbuf[3] *= intens;
	memfillcolor(sp->frag, *(ulong*)sp->cbuf);

	return sp->frag;
}

Memimage *
toonshader(FSparams *sp)
{
	double intens;

	intens = dotvec3(Vec3(sp->su->var_intensity[0], sp->su->var_intensity[1], sp->su->var_intensity[2]), sp->bc);
	intens = intens > 0.85? 1: intens > 0.60? 0.80: intens > 0.45? 0.60: intens > 0.30? 0.45: intens > 0.15? 0.30: 0;
	sp->cbuf[1] = 0;
	sp->cbuf[2] = 155*intens;
	sp->cbuf[3] = 255*intens;
	memfillcolor(sp->frag, *(ulong*)sp->cbuf);

	return sp->frag;
}

Memimage *
triangleshader(FSparams *sp)
{
	Triangle2 t;
	Rectangle bbox;
	Point3 bc;
	uchar cbuf[4];

	t.p0 = Pt2(240,200,1);
	t.p1 = Pt2(400,40,1);
	t.p2 = Pt2(240,40,1);

	bbox = Rect(
		min(min(t.p0.x, t.p1.x), t.p2.x), min(min(t.p0.y, t.p1.y), t.p2.y),
		max(max(t.p0.x, t.p1.x), t.p2.x), max(max(t.p0.y, t.p1.y), t.p2.y)
	);
	if(!ptinrect(sp->p, bbox))
		return nil;

	bc = barycoords(t, Pt2(sp->p.x,sp->p.y,1));
	if(bc.x < 0 || bc.y < 0 || bc.z < 0)
		return nil;

	cbuf[0] = 0xFF;
	cbuf[1] = 0xFF*bc.z;
	cbuf[2] = 0xFF*bc.y;
	cbuf[3] = 0xFF*bc.x;
	memfillcolor(sp->frag, *(ulong*)cbuf);
	return sp->frag;
}

Memimage *
circleshader(FSparams *sp)
{
	Point2 uv;
	double r, d;
	uchar cbuf[4];

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);
//	r = 0.3;
	r = 0.3*fabs(sin(sp->su->uni_time/1e9));
	d = vec2len(subpt2(uv, Vec2(0.5,0.5)));

	if(d > r + r*0.05 || d < r - r*0.05)
		return nil;

	cbuf[0] = 0xFF;
	cbuf[1] = 0;
	cbuf[2] = 0xFF*uv.y;
	cbuf[3] = 0xFF*uv.x;

	memfillcolor(sp->frag, *(ulong*)cbuf);
	return sp->frag;
}

/* some shaping functions from The Book of Shaders, Chapter 5 */
Memimage *
sfshader(FSparams *sp)
{
	Point2 uv;
	double y, pct;
	uchar cbuf[4];

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);
	uv.y = 1 - uv.y;		/* make [0 0] the bottom-left corner */

//	y = step(0.5, uv.x);
//	y = pow(uv.x, 5);
//	y = sin(uv.x);
	y = sin(uv.x*sp->su->uni_time/1e8)/2.0 + 0.5;
//	y = smoothstep(0.1, 0.9, uv.x);
	pct = smoothstep(y-0.02, y, uv.y) - smoothstep(y, y+0.02, uv.y);

	cbuf[0] = 0xFF;
	cbuf[1] = 0xFF*flerp(y, 0, pct);
	cbuf[2] = 0xFF*flerp(y, 1, pct);
	cbuf[3] = 0xFF*flerp(y, 0, pct);

	memfillcolor(sp->frag, *(ulong*)cbuf);
	return sp->frag;
}

Memimage *
boxshader(FSparams *sp)
{
	Point2 uv, p;
	Point2 r;
	uchar cbuf[4];

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);
	r = Vec2(0.2,0.4);

	p = Pt2(fabs(uv.x - 0.5), fabs(uv.y - 0.5), 1);
	p = subpt2(p, r);
	p.x = fmax(p.x, 0);
	p.y = fmax(p.y, 0);

	if(vec2len(p) > 0)
		return nil;

	cbuf[0] = 0xFF;
	cbuf[1] = 0xFF*smoothstep(0,1,uv.x+uv.y);
	cbuf[2] = 0xFF*uv.y;
	cbuf[3] = 0xFF*uv.x;

	memfillcolor(sp->frag, *(ulong*)cbuf);
	return sp->frag;
}

Point3
ivshader(VSparams *sp)
{
	return world2clip(maincam, sp->v->p);
}

Memimage *
identshader(FSparams *sp)
{
	memfillcolor(sp->frag, *(ulong*)sp->cbuf);
	return sp->frag;
}

Shader shadertab[] = {
	{ "triangle", ivshader, triangleshader },
	{ "circle", ivshader, circleshader },
	{ "box", ivshader, boxshader },
	{ "sf", ivshader, sfshader },
	{ "gouraud", vertshader, gouraudshader },
	{ "toon", vertshader, toonshader },
	{ "ident", vertshader, identshader },
};
Shader *
getshader(char *name)
{
	int i;

	for(i = 0; i < nelem(shadertab); i++)
		if(strcmp(shadertab[i].name, name) == 0)
			return &shadertab[i];
	return nil;
}

void
drawstats(void)
{
	int i;

	snprint(stats[Scamno], sizeof(stats[Scamno]), "CAM %lld", maincam-cams+1);
	snprint(stats[Sfov], sizeof(stats[Sfov]), "FOV %g°", maincam->fov/DEG);
	snprint(stats[Scampos], sizeof(stats[Scampos]), "%V", maincam->p);
	snprint(stats[Scambx], sizeof(stats[Scambx]), "bx %V", maincam->bx);
	snprint(stats[Scamby], sizeof(stats[Scamby]), "by %V", maincam->by);
	snprint(stats[Scambz], sizeof(stats[Scambz]), "bz %V", maincam->bz);
	snprint(stats[Sfps], sizeof(stats[Sfps]), "FPS %.0f/%.0f/%.0f/%.0f", !maincam->stats.max? 0: 1e9/maincam->stats.max, !maincam->stats.avg? 0: 1e9/maincam->stats.avg, !maincam->stats.min? 0: 1e9/maincam->stats.min, !maincam->stats.v? 0: 1e9/maincam->stats.v);
	for(i = 0; i < Se; i++)
		string(screen, addpt(screen->r.min, Pt(10,10 + i*font->height)), display->black, ZP, font, stats[i]);
}

void
redraw(void)
{
	memfillcolor(screenfb, DWhite);
	maincam->vp->fbctl->draw(maincam->vp->fbctl, screenfb);

	lockdisplay(display);
	loadimage(screen, rectaddpt(screenfb->r, screen->r.min), byteaddr(screenfb, screenfb->r.min), bytesperline(screenfb->r, screenfb->depth)*Dy(screenfb->r));
//	drawaxis();
	drawstats();
	flushimage(display, 1);
	unlockdisplay(display);
}

void
drawproc(void *)
{
	Memimage *scrtex;
	uvlong t0, Δt;
	int fd;

	threadsetname("drawproc");

	scrtex = nil;
	fd = -1;
	if(inception){
		fd = open("/dev/screen", OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		if((scrtex = readmemimage(fd)) == nil)
			sysfatal("readmemimage: %r");
	}

	t0 = nsec();
	for(;;){
		shootcamera(maincam, model, inception? scrtex: modeltex, shader);
		Δt = nsec() - t0;
		if(Δt > HZ2MS(60)*1000000ULL){
			nbsend(drawc, nil);
			t0 += Δt;
			if(inception){
				freememimage(scrtex);
				seek(fd, 0, 0);
				if((scrtex = readmemimage(fd)) == nil)
					sysfatal("readmemimage: %r");
			}
		}
	}
}

void
mouse(void)
{
	if((mctl->buttons & 8) != 0){
		maincam->fov = fclamp(maincam->fov - 1*DEG, 1*DEG, 359*DEG);
		reloadcamera(maincam);
	}
	if((mctl->buttons & 16) != 0){
		maincam->fov = fclamp(maincam->fov + 1*DEG, 1*DEG, 359*DEG);
		reloadcamera(maincam);
	}
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
			if(utfrune(buf, Kdel)){
				close(fd);
				threadexitsall(nil);
			}
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
	if(kdown & 1<<K↑)
		placecamera(maincam, subpt3(maincam->p, mulpt3(maincam->bz, 0.1)), maincam->bz, maincam->by);
	if(kdown & 1<<K↓)
		placecamera(maincam, addpt3(maincam->p, mulpt3(maincam->bz, 0.1)), maincam->bz, maincam->by);
	if(kdown & 1<<K←)
		placecamera(maincam, subpt3(maincam->p, mulpt3(maincam->bx, 0.1)), maincam->bz, maincam->by);
	if(kdown & 1<<K→)
		placecamera(maincam, addpt3(maincam->p, mulpt3(maincam->bx, 0.1)), maincam->bz, maincam->by);
	if(kdown & 1<<Krise)
		placecamera(maincam, addpt3(maincam->p, mulpt3(maincam->by, 0.1)), maincam->bz, maincam->by);
	if(kdown & 1<<Kfall)
		placecamera(maincam, subpt3(maincam->p, mulpt3(maincam->by, 0.1)), maincam->bz, maincam->by);
	if(kdown & 1<<KR↑)
		aimcamera(maincam, qrotate(maincam->bz, maincam->bx, 1*DEG));
	if(kdown & 1<<KR↓)
		aimcamera(maincam, qrotate(maincam->bz, maincam->bx, -1*DEG));
	if(kdown & 1<<KR←)
		aimcamera(maincam, qrotate(maincam->bz, maincam->by, 1*DEG));
	if(kdown & 1<<KR→)
		aimcamera(maincam, qrotate(maincam->bz, maincam->by, -1*DEG));
	if(kdown & 1<<KR↺)
		placecamera(maincam, maincam->p, maincam->bz, qrotate(maincam->by, maincam->bz, 1*DEG));
	if(kdown & 1<<KR↻)
		placecamera(maincam, maincam->p, maincam->bz, qrotate(maincam->by, maincam->bz, -1*DEG));
	if(kdown & 1<<Kcam0)
		maincam = &cams[0];
	if(kdown & 1<<Kcam1)
		maincam = &cams[1];
	if(kdown & 1<<Kcam2)
		maincam = &cams[2];
	if(kdown & 1<<Kcam3)
		maincam = &cams[3];
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
//	fprint(fd, "pri 15\n");
//	fprint(fd, "wired 0\n");

	close(fd);
}

void
usage(void)
{
	fprint(2, "usage: %s [-t texture] [-s shader] [-ω yrot] model\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Viewport *v;
	Channel *keyc;
	char *mdlpath, *texpath, *sname;
	int i, fd;

	GEOMfmtinstall();
	texpath = nil;
	sname = "gouraud";
	ARGBEGIN{
	case 't': texpath = EARGF(usage()); break;
	case 's': sname = EARGF(usage()); break;
	case L'ω': ω = strtod(EARGF(usage()), nil)*DEG; break;
	case L'σ': inception++; break;
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc != 1)
		usage();

	mdlpath = argv[0];

	confproc();

	if((shader = getshader(sname)) == nil)
		sysfatal("couldn't find %s shader", sname);

	if((model = objparse(mdlpath)) == nil)
		sysfatal("objparse: %r");
	if(texpath != nil){
		fd = open(texpath, OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		if((modeltex = readmemimage(fd)) == nil)
			sysfatal("readmemimage: %r");
		close(fd);
	}

	if(initdraw(nil, nil, "3d") < 0)
		sysfatal("initdraw: %r");
	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	screenfb = eallocmemimage(rectsubpt(screen->r, screen->r.min), screen->chan);
	for(i = 0; i < nelem(cams); i++){
		v = mkviewport(screenfb->r);
		placecamera(&cams[i], camcfgs[i].p, camcfgs[i].lookat, camcfgs[i].up);
		configcamera(&cams[i], v, camcfgs[i].fov, camcfgs[i].clipn, camcfgs[i].clipf, camcfgs[i].ptype);
	}
	maincam = &cams[0];
	light = normvec3(subpt3(light, center));

	keyc = chancreate(sizeof(void*), 1);
	drawc = chancreate(sizeof(void*), 1);
	display->locking = 1;
	unlockdisplay(display);

	proccreate(kbdproc, nil, mainstacksize);
	proccreate(keyproc, keyc, mainstacksize);
	proccreate(drawproc, nil, mainstacksize);

	for(;;){
		enum {MOUSE, RESIZE, KEY, DRAW};
		Alt a[] = {
			{mctl->c, &mctl->Mouse, CHANRCV},
			{mctl->resizec, nil, CHANRCV},
			{keyc, nil, CHANRCV},
			{drawc, nil, CHANRCV},
			{nil, nil, CHANEND}
		};
		switch(alt(a)){
		case MOUSE: mouse(); break;
		case RESIZE: resize(); break;
		case KEY: handlekeys(); break;
		case DRAW: redraw(); break;
		}
	}
}
