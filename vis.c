#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>
#include "libgraphics/graphics.h"
#include "dat.h"
#include "fns.h"

#define isdigit(c) ((c) >= '0' && (c) <= '9')

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
 [Kzoomin]	= 'z',
 [Kzoomout]	= 'x',
 [Kcam0]	= KF|1,
 [Kcam1]	= KF|2,
 [Kcam2]	= KF|3,
 [Kcam3]	= KF|4,
 [Khud]		= 'h',
};
char *skyboxpaths[] = {
	"cubemap/skybox/left.pic",
	"cubemap/skybox/right.pic",
	"cubemap/skybox/bottom.pic",
	"cubemap/skybox/top.pic",
	"cubemap/skybox/front.pic",
	"cubemap/skybox/back.pic",
};
char stats[Se][256];
Image *screenb;
Image *clr;
Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;
int kdown;
Shadertab *shader;
Model *model;
Scene *scene;
Mouse om;
Quaternion orient = {1,0,0,0};

Camera *cams[4], *maincam;
Camcfg camcfgs[4] = {
	2,0,-4,1,
	0,0,0,1,
	0,1,0,0,
	0, 0.01, 1000, ORTHOGRAPHIC,

	-2,0,-4,1,
	0,0,0,1,
	0,1,0,0,
	120*DEG, 0.01, 1000, PERSPECTIVE,

	-2,0,4,1,
	0,0,0,1,
	0,1,0,0,
	0, 0.01, 1000, ORTHOGRAPHIC,

	2,0,4,1,
	0,0,0,1,
	0,1,0,0,
	80*DEG, 0.01, 1000, PERSPECTIVE
};
Point3 center = {0,0,0,1};
LightSource lights[2];

static int showskybox;
static int doprof;
static int inception;
static int showhud;
static char *curraster;
static int blendon;
static int depthon;
static int abuffon;
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

void
zoomin(void)
{
	maincam->fov = fclamp(maincam->fov - 1*DEG, 1*DEG, 180*DEG);
	reloadcamera(maincam);
}

void
zoomout(void)
{
	maincam->fov = fclamp(maincam->fov + 1*DEG, 1*DEG, 180*DEG);
	reloadcamera(maincam);
}

void
drawstats(void)
{
	int i, camno;

	camno = -1;
	for(i = 0; i < nelem(cams); i++)
		if(maincam == cams[i])
			camno = i+1;

	snprint(stats[Scamno], sizeof(stats[Scamno]), "CAM %d", camno);
	snprint(stats[Sfov], sizeof(stats[Sfov]), "FOV %g°", maincam->fov/DEG);
	snprint(stats[Scampos], sizeof(stats[Scampos]), "%V", maincam->p);
	snprint(stats[Scambx], sizeof(stats[Scambx]), "bx %V", maincam->bx);
	snprint(stats[Scamby], sizeof(stats[Scamby]), "by %V", maincam->by);
	snprint(stats[Scambz], sizeof(stats[Scambz]), "bz %V", maincam->bz);
	snprint(stats[Sfps], sizeof(stats[Sfps]), "FPS %.0f/%.0f/%.0f/%.0f",
		!maincam->stats.max? 0: 1e9/maincam->stats.max,
		!maincam->stats.avg? 0: 1e9/maincam->stats.avg,
		!maincam->stats.min? 0: 1e9/maincam->stats.min,
		!maincam->stats.v? 0: 1e9/maincam->stats.v);
	snprint(stats[Sframes], sizeof(stats[Sframes]), "frame %llud", maincam->stats.nframes);
	snprint(stats[Sorient], sizeof(stats[Sorient]), "ℍ %V", (Point3)orient);
	snprint(stats[Sextra], sizeof(stats[Sextra]), "blend %s z-buf %s a-buf %s",
		maincam->rendopts & ROBlend? "on": "off",
		maincam->rendopts & RODepth? "on": "off",
		maincam->rendopts & ROAbuff? "on": "off");
	for(i = 0; i < Se; i++)
		stringbg(screen, addpt(screen->r.min, Pt(10,10 + i*font->height)), display->black, ZP, font, stats[i], display->white, ZP);
}

void
redraw(void)
{
	lockdisplay(display);
	draw(screen, screen->r, screenb, nil, ZP);
	if(showhud)
		drawstats();
	flushimage(display, 1);
	unlockdisplay(display);
}

void
renderproc(void *)
{
	Material *mtl;
	Primitive *prim;
	uvlong t0, Δt;
	int fd;
	double time;

	threadsetname("renderproc");

	fd = -1;
	if(inception){
		fd = open("/dev/screen", OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		mtl = newmaterial("ιnception");
		mtl->diffusemap = alloctexture(sRGBTexture, nil);
		if((mtl->diffusemap->image = readmemimage(fd)) == nil)
			sysfatal("readmemimage: %r");
		model->addmaterial(model, *mtl);
		free(mtl);
		mtl = &model->materials[model->nmaterials-1];
		for(prim = model->prims; prim < model->prims+model->nprims; prim++)
			prim->mtl = mtl;
	}

	t0 = nanosec();
	for(;;){
		time = t0;
		setuniform(shader, "time", VANumber, &time);

		shootcamera(maincam, shader);

		Δt = nanosec() - t0;
		if(Δt > HZ2MS(60)*1000000ULL){
			lockdisplay(display);
			draw(screenb, screenb->r, clr, nil, ZP);
			maincam->view->draw(maincam->view, screenb, curraster);
			unlockdisplay(display);

			nbsend(drawc, nil);
			t0 += Δt;

			if(inception){
				freememimage(mtl->diffusemap->image);
				seek(fd, 0, 0);
				if((mtl->diffusemap->image = readmemimage(fd)) == nil)
					sysfatal("readmemimage: %r");
			}
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
	Quaternion Δorient;
	Entity *e;

	if((om.buttons^mctl->buttons) == 0){
		Δorient = orient;
		qball(screen->r, om.xy, mctl->xy, &orient, nil);
		Δorient = mulq(orient, invq(Δorient));

		for(e = scene->ents.next; e != &scene->ents; e = e->next){
			e->bx = vcs2world(maincam, qsandwichpt3(Δorient, world2vcs(maincam, e->bx)));
			e->by = vcs2world(maincam, qsandwichpt3(Δorient, world2vcs(maincam, e->by)));
			e->bz = vcs2world(maincam, qsandwichpt3(Δorient, world2vcs(maincam, e->bz)));
		}
	}else{	/* DBG only */
		Framebuf *fb;
		Viewport *v;
		Raster *cr, *zr, *nr;
		Point2 p₂;
		Point p;
		Color c, n;
		double z;
//		Abuf *abuf;
//		Astk *astk;
//		int i;

		v = maincam->view;
		p = subpt(mctl->xy, screen->r.min);
		p₂ = Pt2(p.x, p.y, 1);
		p₂ = rframexform(p₂, *v);
		p = Pt(p₂.x, p₂.y);
		if(!ptinrect(p, v->r))
			return;
		qlock(v->fbctl);
		fb = v->getfb(v);
		cr = v->fetchraster(v, nil);
		zr = v->fetchraster(v, "z-buffer");
		nr = v->fetchraster(v, "normals");
		c = ul2col(cr->data[p.y*Dx(fb->r) + p.x]);
		n = nr != nil? ul2col(nr->data[p.y*Dx(fb->r) + p.x]): Vec3(0,0,0);
		z = *(float*)&zr->data[p.y*Dx(fb->r) + p.x];
//		abuf = &fb->abuf;
//		if(abuf->stk != nil){
//			astk = &abuf->stk[p.y*Dx(fb->r) + p.x];
//			if(astk->active){
//				fprint(2, "p %P nfrags %lud\n", p, astk->size);
//				for(i = 0; i < astk->size; i++)
//					fprint(2, "\t%d: %V %g\n", i, astk->items[i].c, astk->items[i].z);
//			}
//		}
		qunlock(v->fbctl);
		snprint(stats[Spixcol], sizeof(stats[Spixcol]), "c %V z %g", c, z);
		snprint(stats[Snorcol], sizeof(stats[Snorcol]), "n %V", n);
	}
}

void
mmb(void)
{
	enum {
		MOVELIGHT,
		SP0,
		TSNEAREST,
		TSBILINEAR,
		SP1,
		SHOWNORMALS,
		SHOWSPECULAR,
		SHOWZBUFFER,
		SP2,
		SETCLRCOL,
		SP3,
		CULLFRONT,
		CULLBACK,
		CULLNO,
		SP4,
		TGLBLEND,
		TGLDEPTH,
		TGLABUFF,
	};
	static char *items[] = {
	 [MOVELIGHT]	"move light",
			"",
	 [TSNEAREST]	"use nearest sampler",
	 [TSBILINEAR]	"use bilinear sampler",
			"",
	 [SHOWNORMALS]	"show normals",
	 [SHOWSPECULAR]	"show specular",
	 [SHOWZBUFFER]	"show z-buffer",
			"",
	 [SETCLRCOL]	"set clear color",
			"",
	 [CULLFRONT]	"cull front faces",
	 [CULLBACK]	"cull back faces",
	 [CULLNO]	"no culling",
			"",
	 [TGLBLEND]	"toggle blending",
	 [TGLDEPTH]	"toggle depth testing",
	 [TGLABUFF]	"toggle the A-buffer",
		nil,
	};
	static Menu menu = { .item = items };
	char buf[256], *f[3];
	int nf;
	ulong clrcol;

	lockdisplay(display);
	switch(menuhit(2, mctl, &menu, _screen)){
	case MOVELIGHT:
		snprint(buf, sizeof buf, "%g %g %g", lights[0].p.x, lights[0].p.y, lights[0].p.z);
		if(enter("light pos", buf, sizeof buf, mctl, kctl, nil) <= 0)
			break;
		nf = tokenize(buf, f, 3);
		if(nf != 3)
			break;
		lights[0].p.x = strtod(f[0], nil);
		lights[0].p.y = strtod(f[1], nil);
		lights[0].p.z = strtod(f[2], nil);
		break;
	case TSNEAREST:
		tsampler = neartexsampler;
		break;
	case TSBILINEAR:
		tsampler = bilitexsampler;
		break;
	case SHOWNORMALS:
		curraster = curraster && strcmp(curraster, "normals") == 0? nil: "normals";
		break;
	case SHOWSPECULAR:
		curraster = curraster && strcmp(curraster, "specular") == 0? nil: "specular";
		break;
	case SHOWZBUFFER:
		curraster = curraster && strcmp(curraster, "z-buffer") == 0? nil: "z-buffer";
		break;
	case SETCLRCOL:
		if(unloadimage(clr, UR, (uchar*)&clrcol, 4) != 4)
			break;
		clrcol = clrcol<<8 | 0xFF;	/* xrgb2rgba */
		snprint(buf, sizeof buf, "0x%08lux", clrcol);
		if(enter("clear color", buf, sizeof buf, mctl, kctl, nil) <= 0)
			break;
		nf = tokenize(buf, f, 1);
		if(nf != 1)
			break;
		clrcol = strtoul(buf, nil, 0);
		freeimage(clr);
		clr = eallocimage(display, UR, XRGB32, 1, clrcol);
		break;
	case CULLFRONT:
		maincam->cullmode = CullFront;
		break;
	case CULLBACK:
		maincam->cullmode = CullBack;
		break;
	case CULLNO:
		maincam->cullmode = CullNone;
		break;
	case TGLBLEND:
		maincam->rendopts ^= ROBlend;
		break;
	case TGLDEPTH:
		maincam->rendopts ^= RODepth;
		break;
	case TGLABUFF:
		maincam->rendopts ^= ROAbuff;
		break;
	}
	unlockdisplay(display);
	nbsend(drawc, nil);
}

static char *
genrmbmenuitem(int idx)
{
	if(idx < nelem(shadertab))
		return shadertab[idx].name;
	return nil;
}

void
rmb(void)
{
	static Menu menu = { .gen = genrmbmenuitem };
	int idx;

	lockdisplay(display);
	idx = menuhit(3, mctl, &menu, _screen);
	if(idx >= 0){
		shader = &shadertab[idx];
		for(idx = 0; idx < nelem(cams); idx++)
			memset(&cams[idx]->stats, 0, sizeof(cams[idx]->stats));
	}
//	if(om.buttons == mctl->buttons){
//		Point p;
//
//		p = subpt(mctl->xy, om.xy);
//		maincam->view->p.x += p.x;
//		maincam->view->p.y += p.y;
//	}
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

	if(kdown & 1<<K↑)
		movecamera(maincam, mulpt3(maincam->bz, -0.1));
	if(kdown & 1<<K↓)
		movecamera(maincam, mulpt3(maincam->bz, 0.1));
	if(kdown & 1<<K←)
		movecamera(maincam, mulpt3(maincam->bx, -0.1));
	if(kdown & 1<<K→)
		movecamera(maincam, mulpt3(maincam->bx, 0.1));
	if(kdown & 1<<Krise)
		movecamera(maincam, mulpt3(maincam->by, 0.1));
	if(kdown & 1<<Kfall)
		movecamera(maincam, mulpt3(maincam->by, -0.1));
	if(kdown & 1<<KR↑)
		rotatecamera(maincam, maincam->bx, 1*DEG);
	if(kdown & 1<<KR↓)
		rotatecamera(maincam, maincam->bx, -1*DEG);
	if(kdown & 1<<KR←)
		rotatecamera(maincam, maincam->by, 1*DEG);
	if(kdown & 1<<KR→)
		rotatecamera(maincam, maincam->by, -1*DEG);
	if(kdown & 1<<KR↺)
		rotatecamera(maincam, maincam->bz, 1*DEG);
	if(kdown & 1<<KR↻)
		rotatecamera(maincam, maincam->bz, -1*DEG);
	if(kdown & 1<<Kzoomin)
		zoomin();
	if(kdown & 1<<Kzoomout)
		zoomout();
	if(kdown & 1<<Kcam0)
		maincam = cams[0];
	if(kdown & 1<<Kcam1)
		maincam = cams[1];
	if(kdown & 1<<Kcam2)
		maincam = cams[2];
	if(kdown & 1<<Kcam3)
		maincam = cams[3];

	if((okdown & 1<<Khud) == 0 && (kdown & 1<<Khud) != 0)
		showhud ^= 1;

	okdown = kdown;
}

static void
mkblendtestscene(void)
{
	static Color cols[] = {{1,0,0,0.5}, {0,1,0,0.5}, {0,0,1,0.5}};
	Entity *ent;
	Model *mdl;
	Primitive t[2];
	Point3 p, v1, v2;
	int i, j, k;

	memset(t, 0, sizeof t);
	t[0].type = t[1].type = PTriangle;

	/* build the first face/quad, facing the positive z axis */
	p = Vec3(-0.5,-0.5,0);
	v1 = Vec3(1,0,0);
	v2 = Vec3(0,1,0);
	t[0].v[0].p = addpt3(center, p);
	t[0].v[1].p = addpt3(center, addpt3(p, v1));
	t[0].v[2].p = addpt3(center, addpt3(p, addpt3(v1, v2)));
	t[0].v[0].n = t[0].v[1].n = t[0].v[2].n = Vec3(0,0,1);
	t[1].v[0] = t[0].v[0];
	t[1].v[1] = t[0].v[2];
	t[1].v[2].p = addpt3(center, addpt3(p, v2));
	t[1].v[2].n = Vec3(0,0,1);

	for(i = 0; i < nelem(cols); i++){
		for(j = 0; j < 2; j++)
			for(k = 0; k < 3; k++){
				if(i != 0){
					t[j].v[k].p = qrotate(t[j].v[k].p, Vec3(0,1,0), PI/nelem(cols));
					t[j].v[k].n = qrotate(t[j].v[k].n, Vec3(0,1,0), PI/nelem(cols));
				}
				t[j].v[k].c = cols[i];
			}

		mdl = newmodel();
		mdl->addprim(mdl, t[0]);
		mdl->addprim(mdl, t[1]);
		ent = newentity(nil, mdl);
		scene->addent(scene, ent);
	}
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
//	fprint(fd, "fixedpri 15");
//	fprint(fd, "wired 0\n");
//	setfcr(getfcr() & ~FPINVAL);

	close(fd);
}

void
usage(void)
{
	fprint(2, "usage: %s [-s] [-t texture] [-g wxh[xs]] model...\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Renderer *rctl;
	Viewport *v;
	Channel *keyc;
	Entity *subject;
	Material *tmpmtl;
	Primitive *prim;
	char *texpath, *mdlpath, *s;
	int i, fd, fbw, fbh, scale;
	int blendtest = 0;

	GEOMfmtinstall();
	texpath = nil;
	fbw = fbh = 0;
	scale = 1;
	ARGBEGIN{
	case 's': showskybox++; break;
	case 't': texpath = EARGF(usage()); break;
	case 'g':
		s = EARGF(usage());
		fbw = strtoul(s, &s, 10);
		if(fbw == 0 || *s++ != 'x')
			usage();
		fbh = strtoul(s, &s, 10);
		if(fbh == 0)
			usage();
		if(*s++ == 'x' && isdigit(*s))
			scale = strtoul(s, nil, 10);
		break;
	case L'ι': inception++; break;
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc < 1)
		blendtest++;

	confproc();

	if((shader = getshader("gouraud")) == nil)
		sysfatal("couldn't find gouraud shader");

	scene = newscene(nil);
	if(blendtest)
		mkblendtestscene();
	else
	while(argc--){
		mdlpath = argv[argc];
		fd = open(mdlpath, OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		model = readmodel(fd);
		if(model == nil)
			sysfatal("readmodel: %r");
		close(fd);
		subject = newentity(mdlpath, model);
//		subject->p.z = -argc*4;
		scene->addent(scene, subject);

		if(argc == 0 && texpath != nil){
			fd = open(texpath, OREAD);
			if(fd < 0)
				sysfatal("open: %r");
			tmpmtl = newmaterial("__tmp");
			tmpmtl->diffusemap = alloctexture(sRGBTexture, nil);
			if((tmpmtl->diffusemap->image = readmemimage(fd)) == nil)
				sysfatal("readmemimage: %r");
			close(fd);
			model->addmaterial(model, *tmpmtl);
			free(tmpmtl);
			tmpmtl = &model->materials[model->nmaterials-1];
			for(prim = model->prims; prim < model->prims+model->nprims; prim++)
				prim->mtl = tmpmtl;
		}
	}
	if(showskybox)
		scene->skybox = readcubemap(skyboxpaths);

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	if((rctl = initgraphics()) == nil)
		sysfatal("initgraphics: %r");
	if(initdraw(nil, nil, "3d") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	rctl->doprof = doprof;

	clr = eallocimage(display, UR, XRGB32, 1, 0x888888FF);
	screenb = eallocimage(display, rectsubpt(screen->r, screen->r.min), XRGB32, 0, DNofill);
fprint(2, "screen %R\n", screenb->r);

	v = mkviewport(fbw == 0 || fbh == 0? screenb->r: Rect(0,0,fbw,fbh));
	v->setscale(v, scale, scale);
	v->createraster(v, "normals", COLOR32);
	v->createraster(v, "specular", COLOR32);
	v->p.x = (Dx(screenb->r) - v->getwidth(v))/2;
	v->p.y = (Dy(screenb->r) - v->getheight(v))/2;
	if(scale == 2)
		v->setscalefilter(v, UFScale2x);
	else if(scale == 3)
		v->setscalefilter(v, UFScale3x);
fprint(2, "view off %v scalex %g scaley %g\n", v->p, v->bx.x, v->by.y);

	for(i = 0; i < nelem(cams); i++){
		cams[i] = Camv(v, rctl,
				camcfgs[i].ptype, camcfgs[i].fov, camcfgs[i].clipn, camcfgs[i].clipf);
		if(cams[i] == nil)
			sysfatal("Camv: %r");
		placecamera(cams[i], scene, camcfgs[i].p, camcfgs[i].lookat, camcfgs[i].up);
	}
	maincam = cams[3];
	lights[0].p = Pt3(0,100,100,1);
	lights[0].c = Pt3(1,1,1,1);
	lights[0].type = LightPoint;
	lights[0].cutoff = 3000;
	lights[1].p = Pt3(0,100,-100,1);
	lights[1].c = Pt3(1,1,1,1);
	lights[1].type = LightPoint;
	lights[1].cutoff = 3000;
	/* to test spotlights */
//	lights[0].dir = Vec3(0,-1,0);
//	lights[0].type = LightSpot;
//	lights[0].θu = 30*DEG;
//	lights[0].θp = 5*DEG;
	scene->addlight(scene, &lights[0]);
	scene->addlight(scene, &lights[1]);
	tsampler = neartexsampler;

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
