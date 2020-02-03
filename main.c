#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "geometry.h"
#include "graphics.h"
#include "obj.h"
#include "dat.h"
#include "fns.h"

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
 [Kscrshot]	= KF|12
};

char stats[Se][256];

Mousectl *mctl;
Channel *drawc;
int kdown;
vlong t0, t;
double Δt;
Mesh model;
char *mdlpath = "../threedee/mdl/rocket.obj";

Camera cams[4], *maincam;

#pragma varargck type "v" Point2
int
vfmt(Fmt *f)
{
	Point2 p;

	p = va_arg(f->args, Point2);
	return fmtprint(f, "[%g %g %g]", p.x, p.y, p.w);
}

#pragma varargck type "V" Point3
int
Vfmt(Fmt *f)
{
	Point3 p;

	p = va_arg(f->args, Point3);
	return fmtprint(f, "[%g %g %g %g]", p.x, p.y, p.z, p.w);
}

void *
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		sysfatal("malloc: %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

void *
erealloc(void *p, ulong n)
{
	void *np;

	np = realloc(p, n);
	if(np == nil)
		sysfatal("realloc: %r");
	setrealloctag(np, getcallerpc(&p));
	return np;
}

int
depthcmp(void *a, void *b)
{
	Triangle3 *ta, *tb;
	double za, zb;

	ta = (Triangle3 *)a;
	tb = (Triangle3 *)b;
	za = (ta->p0.z + ta->p1.z + ta->p2.z)/3;
	zb = (tb->p0.z + tb->p1.z + tb->p2.z)/3;
	return zb-za;
}

void
drawaxis(void)
{
	Point3	op = (Point3){0, 0, 0, 1},
		px = (Point3){1, 0, 0, 1},
		py = (Point3){0, 1, 0, 1},
		pz = (Point3){0, 0, 1, 1};

	line3(maincam, op, px, 0, Endarrow, display->black);
	string3(maincam, px, display->black, font, "x");
	line3(maincam, op, py, 0, Endarrow, display->black);
	string3(maincam, py, display->black, font, "y");
	line3(maincam, op, pz, 0, Endarrow, display->black);
	string3(maincam, pz, display->black, font, "z");
}

void
drawstats(void)
{
	int i;

	snprint(stats[Scamno], sizeof(stats[Scamno]), "CAM %lld", maincam-cams+1);
	snprint(stats[Sfov], sizeof(stats[Sfov]), "FOV %g°", maincam->fov);
	snprint(stats[Scampos], sizeof(stats[Scampos]), "%V", maincam->p);
	snprint(stats[Scambx], sizeof(stats[Scambx]), "bx %V", maincam->bx);
	snprint(stats[Scamby], sizeof(stats[Scamby]), "by %V", maincam->by);
	snprint(stats[Scambz], sizeof(stats[Scambz]), "bz %V", maincam->bz);
	for(i = 0; i < Se; i++)
		stringn(maincam->viewport, addpt(screen->r.min, Pt(10, 10 + i*font->height)), display->black, ZP, font, stats[i], sizeof(stats[i]));
}

void
redraw(void)
{
	Triangle3 tmp;
	static TTriangle3 *vistris;
	static int nallocvistri;
	Triangle trit;
	Point3 n;
	u8int c;
	int i, nvistri;

	nvistri = 0;
	if(nallocvistri == 0 && vistris == nil){
		nallocvistri = model.ntri/2;
		vistris = emalloc(nallocvistri*sizeof(TTriangle3));
	}
	for(i = 0; i < model.ntri; i++){
		/* world to camera */
		tmp.p0 = WORLD2VCS(maincam, model.tris[i].p0);
		tmp.p1 = WORLD2VCS(maincam, model.tris[i].p1);
		tmp.p2 = WORLD2VCS(maincam, model.tris[i].p2);
		/* back-face culling */
		n = normvec3(crossvec3(subpt3(tmp.p1, tmp.p0), subpt3(tmp.p2, tmp.p0)));
		if(dotvec3(n, mulpt3(tmp.p0, -1)) <= 0)
			continue;
		/* camera to projected ndc */
		tmp.p0 = VCS2NDC(maincam, tmp.p0);
		tmp.p1 = VCS2NDC(maincam, tmp.p1);
		tmp.p2 = VCS2NDC(maincam, tmp.p2);
		/* clipping */
		/*
		 * no clipping for now, the whole triangle is ignored
		 * if any of its vertices gets outside the fustrum.
		 */
		if(isclipping(tmp.p0) || isclipping(tmp.p1) || isclipping(tmp.p2))
			continue;
		if(nvistri >= nallocvistri){
			nallocvistri += model.ntri/3;
			vistris = erealloc(vistris, nallocvistri*sizeof(TTriangle3));
		}
		vistris[nvistri] = (TTriangle3)tmp;
		c = 0xff*fabs(dotvec3(n, Vec3(0, 0, 1)));
		vistris[nvistri].tx = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, c<<24|c<<16|c<<8|0xff);
		nvistri++;
	}
	qsort(vistris, nvistri, sizeof(TTriangle3), depthcmp);
	lockdisplay(display);
	draw(maincam->viewport, maincam->viewport->r, display->white, nil, ZP);
	drawaxis();
	for(i = 0; i < nvistri; i++){
		/* ndc to screen */
		trit.p0 = toviewport(maincam, vistris[i].p0);
		trit.p1 = toviewport(maincam, vistris[i].p1);
		trit.p2 = toviewport(maincam, vistris[i].p2);
		filltriangle(maincam->viewport, trit, vistris[i].tx, ZP);
		triangle(maincam->viewport, trit, 0, display->black, ZP);
		freeimage(vistris[i].tx);
	}
	drawstats();
	flushimage(display, 1);
	unlockdisplay(display);
}

void
drawproc(void *)
{
	threadsetname("drawproc");
	for(;;){
		send(drawc, nil);
		sleep(FPS2MS(60));
	}
}

void
screenshot(void)
{
	int fd;
	static char buf[128];

	enter("Path", buf, sizeof buf, mctl, nil, nil);
	if(buf[0] == 0)
		return;
	fd = create(buf, OWRITE, 0644);
	if(fd < 0)
		sysfatal("open: %r");
	if(writeimage(fd, screen, 1) < 0)
		sysfatal("writeimage: %r");
	close(fd);
}

void
mouse(void)
{
	if((mctl->buttons & 1) != 0)
		fprint(2, "%v\n", fromviewport(maincam, mctl->xy));
	if((mctl->buttons & 8) != 0){
		maincam->fov -= 5;
		if(maincam->fov < 1)
			maincam->fov = 1;
		reloadcamera(maincam);
	}
	if((mctl->buttons & 16) != 0){
		maincam->fov += 5;
		if(maincam->fov > 359)
			maincam->fov = 359;
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
handlekeys(void)
{
	if(kdown & 1<<K↑)
		placecamera(maincam, subpt3(maincam->p, maincam->bz), maincam->bz, maincam->by);
	if(kdown & 1<<K↓)
		placecamera(maincam, addpt3(maincam->p, maincam->bz), maincam->bz, maincam->by);
	if(kdown & 1<<K←)
		placecamera(maincam, subpt3(maincam->p, maincam->bx), maincam->bz, maincam->by);
	if(kdown & 1<<K→)
		placecamera(maincam, addpt3(maincam->p, maincam->bx), maincam->bz, maincam->by);
	if(kdown & 1<<Krise)
		placecamera(maincam, addpt3(maincam->p, maincam->by), maincam->bz, maincam->by);
	if(kdown & 1<<Kfall)
		placecamera(maincam, subpt3(maincam->p, maincam->by), maincam->bz, maincam->by);
	if(kdown & 1<<KR↑)
		aimcamera(maincam, qrotate(maincam->bz, maincam->bx, 5*DEG));
	if(kdown & 1<<KR↓)
		aimcamera(maincam, qrotate(maincam->bz, maincam->bx, -5*DEG));
	if(kdown & 1<<KR←)
		aimcamera(maincam, qrotate(maincam->bz, maincam->by, 5*DEG));
	if(kdown & 1<<KR→)
		aimcamera(maincam, qrotate(maincam->bz, maincam->by, -5*DEG));
	if(kdown & 1<<KR↺)
		placecamera(maincam, maincam->p, maincam->bz, qrotate(maincam->by, maincam->bz, 5*DEG));
	if(kdown & 1<<KR↻)
		placecamera(maincam, maincam->p, maincam->bz, qrotate(maincam->by, maincam->bz, -5*DEG));
	if(kdown & 1<<Kcam0)
		maincam = &cams[0];
	if(kdown & 1<<Kcam1)
		maincam = &cams[1];
	if(kdown & 1<<Kcam2)
		maincam = &cams[2];
	if(kdown & 1<<Kcam3)
		maincam = &cams[3];
	if(kdown & 1<<Kscrshot)
		screenshot();
}

void
resize(void)
{
	lockdisplay(display);
	if(getwindow(display, Refnone) < 0)
		fprint(2, "can't reattach to window\n");
	unlockdisplay(display);
	maincam->viewport = screen;
	reloadcamera(maincam);
}

void
usage(void)
{
	fprint(2, "usage: %s [-l objmdl]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	OBJ *objmesh;

	fmtinstall('V', Vfmt);
	fmtinstall('v', vfmt);
	ARGBEGIN{
	default: usage();
	case 'l':
		mdlpath = EARGF(usage());
		break;
	}ARGEND;
	if(argc != 0)
		usage();
	if(initdraw(nil, nil, "3d") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	placecamera(&cams[0], Pt3(2, 0, -4, 1), Pt3(0, 0, 0, 1), Vec3(0, 1, 0));
	configcamera(&cams[0], screen, 90, 0.1, 100, Ppersp);
	placecamera(&cams[1], Pt3(-2, 0, -4, 1), Pt3(0, 0, 0, 1), Vec3(0, 1, 0));
	configcamera(&cams[1], screen, 120, 0.1, 100, Ppersp);
	placecamera(&cams[2], Pt3(-2, 0, 4, 1), Pt3(0, 0, 0, 1), Vec3(0, 1, 0));
	configcamera(&cams[2], screen, 90, 0.1, 100, Ppersp);
	placecamera(&cams[3], Pt3(2, 0, 4, 1), Pt3(0, 0, 0, 1), Vec3(0, 1, 0));
	configcamera(&cams[3], screen, 120, 0.1, 100, Ppersp);
	maincam = &cams[0];
	if((objmesh = objparse(mdlpath)) == nil)
		sysfatal("objparse: %r");
	drawc = chancreate(1, 0);
	display->locking = 1;
	unlockdisplay(display);
	proccreate(drawproc, nil, mainstacksize);
	proccreate(kbdproc, nil, mainstacksize);
	t0 = nsec();
	for(;;){
		enum {MOUSE, RESIZE, DRAW};
		Alt a[] = {
			{mctl->c, &mctl->Mouse, CHANRCV},
			{mctl->resizec, nil, CHANRCV},
			{drawc, nil, CHANRCV},
			{nil, nil, CHANNOBLK}
		};
		switch(alt(a)){
		case MOUSE: mouse(); break;
		case RESIZE: resize(); break;
		case DRAW: redraw(); break;
		}
		t = nsec();
		Δt = (t-t0)/1e9;
		handlekeys();
		t0 = t;
		sleep(16);
	}
}
