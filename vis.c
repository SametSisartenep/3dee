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
Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;
int kdown;
Shadertab *shader;
Model *model;
Scene *scene;
QLock drawlk;
Mouse om;
Quaternion orient = {1,0,0,0};

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
	80*DEG, 0.01, 100, PERSPECTIVE
};
Point3 center = {0,0,0,1};
LightSource light;		/* global point light */

static int showskybox;
static int doprof;
static int inception;
static int showhud;
Color (*tsampler)(Memimage*,Point2);

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

Point3
gouraudvshader(VSparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Point3 pos, lightdir, lookdir;
	Material *m;
	Color ambient, diffuse, specular;

	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	pos = sp->v->p;
	m = sp->v->mtl;

	ambient = mulpt3(light.c, Ka);
	if(m != nil){
		ambient.r *= m->ambient.r;
		ambient.g *= m->ambient.g;
		ambient.b *= m->ambient.b;
		ambient.a *= m->ambient.a;
	}

	lightdir = normvec3(subpt3(light.p, pos));
	Kd = fmax(0, dotvec3(sp->v->n, lightdir));
	diffuse = mulpt3(light.c, Kd);
	if(m != nil){
		diffuse.r *= m->diffuse.r;
		diffuse.g *= m->diffuse.g;
		diffuse.b *= m->diffuse.b;
		diffuse.a *= m->diffuse.a;
	}

	lookdir = normvec3(subpt3(sp->su->camera->p, pos));
	lightdir = qrotate(lightdir, sp->v->n, PI);
	spec = pow(fmax(0, dotvec3(lookdir, lightdir)), m? m->shininess: 1);
	specular = mulpt3(light.c, spec*Ks);
	if(m != nil){
		specular.r *= m->specular.r;
		specular.g *= m->specular.g;
		specular.b *= m->specular.b;
		specular.a *= m->specular.a;
	}

	sp->v->c = addpt3(ambient, addpt3(diffuse, specular));
	return world2clip(sp->su->camera, pos);
}

Color
gouraudshader(FSparams *sp)
{
	Color tc, c;

	if(sp->v.mtl != nil && sp->v.mtl->diffusemap != nil && sp->v.uv.w != 0){
		tc = texture(sp->v.mtl->diffusemap, sp->v.uv, tsampler);
	}else if(sp->su->entity->mdl->tex != nil && sp->v.uv.w != 0)
		tc = texture(sp->su->entity->mdl->tex, sp->v.uv, tsampler);
	else
		tc = Pt3(1,1,1,1);

	c.a = 1;
	c.b = fclamp(sp->v.c.b*tc.b, 0, 1);
	c.g = fclamp(sp->v.c.g*tc.g, 0, 1);
	c.r = fclamp(sp->v.c.r*tc.r, 0, 1);

	return c;
}

Point3
phongvshader(VSparams *sp)
{
	Point3 pos;
	Color a, d, s;
	double ss;

	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	pos = sp->v->p;
	addvattr(sp->v, "pos", VAPoint, &pos);
	if(sp->v->mtl != nil && sp->v->mtl->normalmap != nil && sp->v->uv.w != 0){
		sp->v->tangent = model2world(sp->su->entity, sp->v->tangent);
		addvattr(sp->v, "tangent", VAPoint, &sp->v->tangent);
	}
	if(sp->v->mtl != nil){
		a = sp->v->mtl->ambient;
		d = sp->v->mtl->diffuse;
		s = sp->v->mtl->specular;
		ss = sp->v->mtl->shininess;
		addvattr(sp->v, "ambient", VAPoint, &a);
		addvattr(sp->v, "diffuse", VAPoint, &d);
		addvattr(sp->v, "specular", VAPoint, &s);
		addvattr(sp->v, "shininess", VANumber, &ss);
	}
	return world2clip(sp->su->camera, pos);
}

Color
phongshader(FSparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Color ambient, diffuse, specular, tc, c;
	Point3 pos, n, lightdir, lookdir;
	Material m;
	RFrame3 TBN;
	Vertexattr *va;

	va = getvattr(&sp->v, "pos");
	pos = va->p;
	
	va = getvattr(&sp->v, "ambient");
	m.ambient = va != nil? va->p: Pt3(1,1,1,1);
	va = getvattr(&sp->v, "diffuse");
	m.diffuse = va != nil? va->p: Pt3(1,1,1,1);
	va = getvattr(&sp->v, "specular");
	m.specular = va != nil? va->p: Pt3(1,1,1,1);
	va = getvattr(&sp->v, "shininess");
	m.shininess = va != nil? va->n: 1;

	ambient = mulpt3(light.c, Ka);
	ambient.r *= m.ambient.r;
	ambient.g *= m.ambient.g;
	ambient.b *= m.ambient.b;
	ambient.a *= m.ambient.a;

	/* normal mapping */
	va = getvattr(&sp->v, "tangent");
	if(va == nil)
		n = sp->v.n;
	else{
		/* TODO implement this on the VS instead and apply Gram-Schmidt here */
		n = texture(sp->v.mtl->normalmap, sp->v.uv, neartexsampler);
		n = normvec3(subpt3(mulpt3(n, 2), Vec3(1,1,1)));

		TBN.p = Pt3(0,0,0,1);
		TBN.bx = va->p;				/* T */
		TBN.bz = sp->v.n;			/* N */
		TBN.by = crossvec3(TBN.bz, TBN.bx);	/* B */

		n = normvec3(invrframexform3(n, TBN));
	}

	lightdir = normvec3(subpt3(light.p, pos));
	Kd = fmax(0, dotvec3(n, lightdir));
	diffuse = mulpt3(light.c, Kd);
	diffuse.r *= m.diffuse.r;
	diffuse.g *= m.diffuse.g;
	diffuse.b *= m.diffuse.b;
	diffuse.a *= m.diffuse.a;

	lookdir = normvec3(subpt3(sp->su->camera->p, pos));
	lightdir = qrotate(lightdir, n, PI);
	spec = pow(fmax(0, dotvec3(lookdir, lightdir)), m.shininess);
	specular = mulpt3(light.c, spec*Ks);
	specular.r *= m.specular.r;
	specular.g *= m.specular.g;
	specular.b *= m.specular.b;
	specular.a *= m.specular.a;

	if(sp->v.mtl != nil && sp->v.mtl->diffusemap != nil && sp->v.uv.w != 0)
		tc = texture(sp->v.mtl->diffusemap, sp->v.uv, tsampler);
	else if(sp->su->entity->mdl->tex != nil && sp->v.uv.w != 0)
		tc = texture(sp->su->entity->mdl->tex, sp->v.uv, tsampler);
	else
		tc = Pt3(1,1,1,1);

	c = addpt3(ambient, addpt3(diffuse, specular));
	c.a = 1;
	c.b = fclamp(c.b*tc.b, 0, 1);
	c.g = fclamp(c.g*tc.g, 0, 1);
	c.r = fclamp(c.r*tc.r, 0, 1);

	return c;
}

Point3
identvshader(VSparams *sp)
{
	Point3 pos, lightdir;
	double intens;

	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	pos = sp->v->p;
	lightdir = normvec3(subpt3(light.p, pos));
	intens = fmax(0, dotvec3(sp->v->n, lightdir));
	addvattr(sp->v, "intensity", VANumber, &intens);
	if(sp->v->mtl != nil)
		sp->v->c = sp->v->mtl->diffuse;
	return world2clip(sp->su->camera, pos);
}

Color
toonshader(FSparams *sp)
{
	Vertexattr *va;
	double intens;

	va = getvattr(&sp->v, "intensity");
	intens = va->n;
	intens = intens > 0.85? 1: intens > 0.60? 0.80: intens > 0.45? 0.60: intens > 0.30? 0.45: intens > 0.15? 0.30: 0;

	return Pt3(intens, 0.6*intens, 0, 1);
}

Color
identshader(FSparams *sp)
{
	Color tc, c;

	if(sp->v.mtl != nil && sp->v.mtl->diffusemap != nil && sp->v.uv.w != 0)
		tc = texture(sp->v.mtl->diffusemap, sp->v.uv, tsampler);
	else if(sp->su->entity->mdl->tex != nil && sp->v.uv.w != 0)
		tc = texture(sp->su->entity->mdl->tex, sp->v.uv, tsampler);
	else
		tc = Pt3(1,1,1,1);

	c.a = 1;
	c.b = fclamp(sp->v.c.b*tc.b, 0, 1);
	c.g = fclamp(sp->v.c.g*tc.g, 0, 1);
	c.r = fclamp(sp->v.c.r*tc.r, 0, 1);

	return c;
}

Point3
ivshader(VSparams *sp)
{
	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	return world2clip(sp->su->camera, sp->v->p);
}

Color
triangleshader(FSparams *sp)
{
	Triangle2 t;
	Rectangle bbox;
	Point3 bc;

	t.p0 = Pt2(240,200,1);
	t.p1 = Pt2(400,40,1);
	t.p2 = Pt2(240,40,1);

	bbox = Rect(
		min(min(t.p0.x, t.p1.x), t.p2.x), min(min(t.p0.y, t.p1.y), t.p2.y),
		max(max(t.p0.x, t.p1.x), t.p2.x), max(max(t.p0.y, t.p1.y), t.p2.y)
	);
	if(!ptinrect(sp->p, bbox))
		return Vec3(0,0,0);

	bc = barycoords(t, Pt2(sp->p.x,sp->p.y,1));
	if(bc.x < 0 || bc.y < 0 || bc.z < 0)
		return Vec3(0,0,0);

	return Pt3(bc.x, bc.y, bc.z, 1);
}

Color
circleshader(FSparams *sp)
{
	Point2 uv;
	double r, d;

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);
//	r = 0.3;
	r = 0.3*fabs(sin(sp->su->uni_time/1e9));
	d = vec2len(subpt2(uv, Vec2(0.5,0.5)));

	if(d > r + r*0.05 || d < r - r*0.05)
		return Vec3(0,0,0);

	return Pt3(uv.x, uv.y, 0, 1);
}

/* some shaping functions from The Book of Shaders, Chapter 5 */
Color
sfshader(FSparams *sp)
{
	Point2 uv;
	double y, pct;

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

	return Pt3(flerp(y, 0, pct), flerp(y, 1, pct), flerp(y, 0, pct), 1);
}

Color
boxshader(FSparams *sp)
{
	Point2 uv, p;
	Point2 r;

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);
	r = Vec2(0.2,0.4);

	p = Pt2(fabs(uv.x - 0.5), fabs(uv.y - 0.5), 1);
	p = subpt2(p, r);
	p.x = fmax(p.x, 0);
	p.y = fmax(p.y, 0);

	if(vec2len(p) > 0)
		return Vec3(0,0,0);

	return Pt3(uv.x, uv.y, smoothstep(0,1,uv.x+uv.y), 1);
}

Shadertab shadertab[] = {
	{ "triangle", ivshader, triangleshader },
	{ "circle", ivshader, circleshader },
	{ "box", ivshader, boxshader },
	{ "sf", ivshader, sfshader },
	{ "toon", identvshader, toonshader },
	{ "ident", identvshader, identshader },
	{ "gouraud", gouraudvshader, gouraudshader },
	{ "phong", phongvshader, phongshader },
};
Shadertab *
getshader(char *name)
{
	int i;

	for(i = 0; i < nelem(shadertab); i++)
		if(strcmp(shadertab[i].name, name) == 0)
			return &shadertab[i];
	return nil;
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
	int i;

	snprint(stats[Scamno], sizeof(stats[Scamno]), "CAM %lld", maincam-cams+1);
	snprint(stats[Sfov], sizeof(stats[Sfov]), "FOV %g°", maincam->fov/DEG);
	snprint(stats[Scampos], sizeof(stats[Scampos]), "%V", maincam->p);
	snprint(stats[Scambx], sizeof(stats[Scambx]), "bx %V", maincam->bx);
	snprint(stats[Scamby], sizeof(stats[Scamby]), "by %V", maincam->by);
	snprint(stats[Scambz], sizeof(stats[Scambz]), "bz %V", maincam->bz);
	snprint(stats[Sfps], sizeof(stats[Sfps]), "FPS %.0f/%.0f/%.0f/%.0f", !maincam->stats.max? 0: 1e9/maincam->stats.max, !maincam->stats.avg? 0: 1e9/maincam->stats.avg, !maincam->stats.min? 0: 1e9/maincam->stats.min, !maincam->stats.v? 0: 1e9/maincam->stats.v);
	snprint(stats[Sframes], sizeof(stats[Sframes]), "frame %llud", maincam->stats.nframes);
	snprint(stats[Sorient], sizeof(stats[Sorient]), "ℍ %V", (Point3)orient);
	for(i = 0; i < Se; i++)
		stringbg(screen, addpt(screen->r.min, Pt(10,10 + i*font->height)), display->black, ZP, font, stats[i], display->white, ZP);
}

void
redraw(void)
{
	static Image *bg;

	if(bg == nil)
		bg = eallocimage(display, UR, RGB24, 1, 0x888888FF);

	lockdisplay(display);
	maincam->vp->draw(maincam->vp, screenb);
	draw(screen, screen->r, bg, nil, ZP);
	draw(screen, screen->r, screenb, nil, ZP);
	if(showhud)
		drawstats();
	flushimage(display, 1);
	unlockdisplay(display);
}

void
renderproc(void *)
{
	uvlong t0, Δt;
	int fd;

	threadsetname("renderproc");

	fd = -1;
	if(inception){
		fd = open("/dev/screen", OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		freememimage(model->tex);
		if((model->tex = readmemimage(fd)) == nil)
			sysfatal("readmemimage: %r");
	}

	t0 = nsec();
	for(;;){
		shootcamera(maincam, shader);
		if(doprof)
		fprint(2, "R %llud %llud\nE %llud %llud\nT %llud %llud\nr %llud %llud\n\n",
			maincam->times.R[maincam->times.cur-1].t0, maincam->times.R[maincam->times.cur-1].t1,
			maincam->times.E[maincam->times.cur-1].t0, maincam->times.E[maincam->times.cur-1].t1,
			maincam->times.Tn[maincam->times.cur-1].t0, maincam->times.Tn[maincam->times.cur-1].t1,
			maincam->times.Rn[maincam->times.cur-1].t0, maincam->times.Rn[maincam->times.cur-1].t1);
		Δt = nsec() - t0;
		if(Δt > HZ2MS(60)*1000000ULL){
			nbsend(drawc, nil);
			t0 += Δt;
			if(inception){
				freememimage(model->tex);
				seek(fd, 0, 0);
				if((model->tex = readmemimage(fd)) == nil)
					sysfatal("readmemimage: %r");
			}
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

void
lmb(void)
{
	Entity *e;
	Quaternion Δorient;

	if((om.buttons^mctl->buttons) == 0){
		Δorient = orient;
		qball(screen->r, om.xy, mctl->xy, &orient, nil);
		Δorient = mulq(orient, invq(Δorient));

		for(e = scene->ents.next; e != &scene->ents; e = e->next){
			e->bx = Vecquat(mulq(mulq(Δorient, Quatvec(0, e->bx)), invq(Δorient)));
			e->by = Vecquat(mulq(mulq(Δorient, Quatvec(0, e->by)), invq(Δorient)));
			e->bz = Vecquat(mulq(mulq(Δorient, Quatvec(0, e->bz)), invq(Δorient)));
		}
	}
}

void
mmb(void)
{
	enum {
		MOVELIGHT,
		TSNEAREST,
		TSBILINEAR,
	};
	static char *items[] = {
	 [MOVELIGHT]	"move light",
	 [TSNEAREST]	"use nearest sampler",
	 [TSBILINEAR]	"use bilinear sampler",
		nil,
	};
	static Menu menu = { .item = items };
	char buf[256], *f[3];
	int nf;

	qlock(&drawlk);
	switch(menuhit(2, mctl, &menu, _screen)){
	case MOVELIGHT:
		snprint(buf, sizeof buf, "%g %g %g", light.p.x, light.p.y, light.p.z);
		if(enter("light pos", buf, sizeof buf, mctl, kctl, nil) <= 0)
			break;
		nf = tokenize(buf, f, 3);
		if(nf != 3)
			break;
		light.p.x = strtod(f[0], nil);
		light.p.y = strtod(f[1], nil);
		light.p.z = strtod(f[2], nil);
		break;
	case TSNEAREST:
		tsampler = neartexsampler;
		break;
	case TSBILINEAR:
		tsampler = bilitexsampler;
		break;
	}
	qunlock(&drawlk);
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

	qlock(&drawlk);
	idx = menuhit(3, mctl, &menu, _screen);
	if(idx >= 0){
		shader = &shadertab[idx];
		for(idx = 0; idx < nelem(cams); idx++)
			memset(&cams[idx].stats, 0, sizeof(cams[idx].stats));
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
	if(kdown & 1<<Kzoomin)
		zoomin();
	if(kdown & 1<<Kzoomout)
		zoomout();
	if(kdown & 1<<Kcam0)
		maincam = &cams[0];
	if(kdown & 1<<Kcam1)
		maincam = &cams[1];
	if(kdown & 1<<Kcam2)
		maincam = &cams[2];
	if(kdown & 1<<Kcam3)
		maincam = &cams[3];

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
//	fprint(fd, "pri 15\n");
//	fprint(fd, "wired 0\n");
//	setfcr(getfcr() & ~FPINVAL);

	close(fd);
}

void
usage(void)
{
	fprint(2, "usage: %s [-s] [-t texture] model...\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Viewport *v;
	Renderer *rctl;
	Channel *keyc;
	Entity *subject;
	OBJ *obj;
	char *texpath, *mdlpath;
	int i, fd;

	GEOMfmtinstall();
	texpath = nil;
	ARGBEGIN{
	case 's': showskybox++; break;
	case 't': texpath = EARGF(usage()); break;
	case L'ι': inception++; break;
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc < 1)
		usage();

	confproc();

	if((shader = getshader("gouraud")) == nil)
		sysfatal("couldn't find gouraud shader");

	scene = newscene(nil);
	while(argc--){
		mdlpath = argv[argc];
		model = newmodel();
		subject = newentity(model);
//		subject->p.x = argc*4;
		scene->addent(scene, subject);

		if((obj = objparse(mdlpath)) == nil)
			sysfatal("objparse: %r");
		loadobjmodel(model, obj);
		objfree(obj);

		if(argc == 0 && texpath != nil){
			fd = open(texpath, OREAD);
			if(fd < 0)
				sysfatal("open: %r");
			if((model->tex = readmemimage(fd)) == nil)
				sysfatal("readmemimage: %r");
			close(fd);
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

	screenb = eallocimage(display, rectsubpt(screen->r, screen->r.min), RGBA32, 0, DNofill);
	for(i = 0; i < nelem(cams); i++){
		v = mkviewport(screenb->r);
		placecamera(&cams[i], camcfgs[i].p, camcfgs[i].lookat, camcfgs[i].up);
		configcamera(&cams[i], v, camcfgs[i].fov, camcfgs[i].clipn, camcfgs[i].clipf, camcfgs[i].ptype);
		cams[i].scene = scene;
		cams[i].rctl = rctl;
	}
	maincam = &cams[3];
	light.p = Pt3(0,100,100,1);
	light.c = Pt3(1,1,1,1);
	light.type = LIGHT_POINT;
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
