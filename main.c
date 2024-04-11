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
char stats[Se][256];
Image *screenb;
Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;
int kdown;
Shadertab *shader;
Model *model;
Entity *subject;
Scene *scene;
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
	80*DEG, 0.01, 100, PERSPECTIVE
};
Point3 center = {0,0,0,1};
LightSource light;		/* global point light */

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
gouraudvshader(VSparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Point3 pos, lightdir, lookdir;
	Material m;
	Color a, d, s;
	Color ambient, diffuse, specular;

	sp->v->n = qrotate(sp->v->n, Vec3(0,1,0), θ+fmod(ω*sp->su->uni_time/1e9, 2*PI));
	sp->v->p = qrotate(sp->v->p, Vec3(0,1,0), θ+fmod(ω*sp->su->uni_time/1e9, 2*PI));
	pos = model2world(sp->su->entity, sp->v->p);
	if(sp->v->mtl != nil){
		a.r = sp->v->mtl->Ka.r; a.g = sp->v->mtl->Ka.g; a.b = sp->v->mtl->Ka.b; a.a = 1;
		d.r = sp->v->mtl->Kd.r; d.g = sp->v->mtl->Kd.g; d.b = sp->v->mtl->Kd.b; d.a = 1;
		s.r = sp->v->mtl->Ks.r; s.g = sp->v->mtl->Ks.g; s.b = sp->v->mtl->Ks.b; s.a = 1;
		m.ambient = a;
		m.diffuse = d;
		m.specular = s;
		m.shininess = sp->v->mtl->Ns;

		ambient = mulpt3(light.c, Ka);
		ambient.r *= m.ambient.r;
		ambient.g *= m.ambient.g;
		ambient.b *= m.ambient.b;
		ambient.a *= m.ambient.a;

		lightdir = normvec3(subpt3(light.p, pos));
		Kd = fmax(0, dotvec3(sp->v->n, lightdir));
		diffuse = mulpt3(light.c, Kd);
		diffuse.r *= m.diffuse.r;
		diffuse.g *= m.diffuse.g;
		diffuse.b *= m.diffuse.b;
		diffuse.a *= m.diffuse.a;

		lookdir = normvec3(subpt3(maincam->p, pos));
		lightdir = qrotate(lightdir, sp->v->n, PI);
		spec = pow(fmax(0, dotvec3(lookdir, lightdir)), m.shininess);
		specular = mulpt3(light.c, spec*Ks);
		specular.r *= m.specular.r;
		specular.g *= m.specular.g;
		specular.b *= m.specular.b;
		specular.a *= m.specular.a;

		sp->v->c = addpt3(ambient, addpt3(diffuse, specular));
	}
	return world2clip(maincam, pos);
}

Color
gouraudshader(FSparams *sp)
{
	Color tc, c;

	if(sp->v.mtl != nil && sp->v.mtl->map_Kd != nil && sp->v.uv.w != 0)
		tc = texture(sp->v.mtl->map_Kd, sp->v.uv, tsampler);
	else if(sp->su->entity->mdl->tex != nil && sp->v.uv.w != 0)
		tc = texture(sp->su->entity->mdl->tex, sp->v.uv, tsampler);
	else
		tc = Pt3(1,1,1,1);

	c.a = fclamp(sp->v.c.a*tc.a, 0, 1);
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

	sp->v->n = qrotate(sp->v->n, Vec3(0,1,0), θ+fmod(ω*sp->su->uni_time/1e9, 2*PI));
	sp->v->p = qrotate(sp->v->p, Vec3(0,1,0), θ+fmod(ω*sp->su->uni_time/1e9, 2*PI));
	pos = model2world(sp->su->entity, sp->v->p);
	addvattr(sp->v, "pos", VAPoint, &pos);
	if(sp->v->mtl != nil){
		a.r = sp->v->mtl->Ka.r; a.g = sp->v->mtl->Ka.g; a.b = sp->v->mtl->Ka.b; a.a = 1;
		d.r = sp->v->mtl->Kd.r; d.g = sp->v->mtl->Kd.g; d.b = sp->v->mtl->Kd.b; d.a = 1;
		s.r = sp->v->mtl->Ks.r; s.g = sp->v->mtl->Ks.g; s.b = sp->v->mtl->Ks.b; s.a = 1;
		ss = sp->v->mtl->Ns;
		addvattr(sp->v, "ambient", VAPoint, &a);
		addvattr(sp->v, "diffuse", VAPoint, &d);
		addvattr(sp->v, "specular", VAPoint, &s);
		addvattr(sp->v, "shininess", VANumber, &ss);
	}
	return world2clip(maincam, pos);
}

Color
phongshader(FSparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Color ambient, diffuse, specular, tc, c;
	Point3 pos, lookdir, lightdir;
	Material m;
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

	lightdir = normvec3(subpt3(light.p, pos));
	Kd = fmax(0, dotvec3(sp->v.n, lightdir));
	diffuse = mulpt3(light.c, Kd);
	diffuse.r *= m.diffuse.r;
	diffuse.g *= m.diffuse.g;
	diffuse.b *= m.diffuse.b;
	diffuse.a *= m.diffuse.a;

	lookdir = normvec3(subpt3(maincam->p, pos));
	lightdir = qrotate(lightdir, sp->v.n, PI);
	spec = pow(fmax(0, dotvec3(lookdir, lightdir)), m.shininess);
	specular = mulpt3(light.c, spec*Ks);
	specular.r *= m.specular.r;
	specular.g *= m.specular.g;
	specular.b *= m.specular.b;
	specular.a *= m.specular.a;

	if(sp->v.mtl != nil && sp->v.mtl->map_Kd != nil && sp->v.uv.w != 0)
		tc = texture(sp->v.mtl->map_Kd, sp->v.uv, tsampler);
	else if(sp->su->entity->mdl->tex != nil && sp->v.uv.w != 0)
		tc = texture(sp->su->entity->mdl->tex, sp->v.uv, tsampler);
	else
		tc = Pt3(1,1,1,1);

	c = addpt3(ambient, addpt3(diffuse, specular));
	c.a = fclamp(c.a*tc.a, 0, 1);
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

	pos = model2world(sp->su->entity, sp->v->p);
	lightdir = normvec3(subpt3(light.p, pos));
	intens = fmax(0, dotvec3(sp->v->n, lightdir));
	addvattr(sp->v, "intensity", VANumber, &intens);
	if(sp->v->mtl != nil){
		sp->v->c.r = sp->v->mtl->Kd.r;
		sp->v->c.g = sp->v->mtl->Kd.g;
		sp->v->c.b = sp->v->mtl->Kd.b;
		sp->v->c.a = 1;
	}
	return world2clip(maincam, pos);
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

	if(sp->v.mtl != nil && sp->v.mtl->map_Kd != nil && sp->v.uv.w != 0)
		tc = texture(sp->v.mtl->map_Kd, sp->v.uv, tsampler);
	else if(sp->su->entity->mdl->tex != nil && sp->v.uv.w != 0)
		tc = texture(sp->su->entity->mdl->tex, sp->v.uv, tsampler);
	else
		tc = Pt3(1,1,1,1);

	c.a = fclamp(sp->v.c.a*tc.a, 0, 1);
	c.b = fclamp(sp->v.c.b*tc.b, 0, 1);
	c.g = fclamp(sp->v.c.g*tc.g, 0, 1);
	c.r = fclamp(sp->v.c.r*tc.r, 0, 1);

	return c;
}

Point3
ivshader(VSparams *sp)
{
	return world2clip(maincam, model2world(sp->su->entity, sp->v->p));
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
	maincam->fov = fclamp(maincam->fov - 1*DEG, 1*DEG, 359*DEG);
	reloadcamera(maincam);
}

void
zoomout(void)
{
	maincam->fov = fclamp(maincam->fov + 1*DEG, 1*DEG, 359*DEG);
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
//	drawaxis();
	if(showhud)
		drawstats();
	flushimage(display, 1);
	unlockdisplay(display);
}

void
drawproc(void *)
{
	uvlong t0, Δt;
	int fd;

	threadsetname("drawproc");

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
			light.p = qrotate(light.p, Vec3(0,1,0), θ+fmod(ω*Δt/1e9, 2*PI));
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

	switch(menuhit(2, mctl, &menu, _screen)){
	case MOVELIGHT:
		snprint(buf, sizeof buf, "%g %g %g", light.p.x, light.p.y, light.p.z);
		if(enter("light pos", buf, sizeof buf, mctl, kctl, nil) <= 0)
			return;
		nf = tokenize(buf, f, 3);
		if(nf != 3)
			return;
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

	idx = menuhit(3, mctl, &menu, _screen);
	if(idx < 0)
		return;
	shader = &shadertab[idx];
	for(idx = 0; idx < nelem(cams); idx++)
		memset(&cams[idx].stats, 0, sizeof(cams[idx].stats));
	nbsend(drawc, nil);
}

void
mouse(void)
{
	if((mctl->buttons & 2) != 0)
		mmb();
	if((mctl->buttons & 4) != 0)
		rmb();
	if((mctl->buttons & 8) != 0)
		zoomin();
	if((mctl->buttons & 16) != 0)
		zoomout();
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
	fprint(2, "usage: %s [-t texture] [-n normals] [-s shader] [-ω yrot] model...\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Viewport *v;
	Renderer *rctl;
	Channel *keyc;
	char *texpath, *norpath, *sname, *mdlpath;
	int i, fd;

	GEOMfmtinstall();
	texpath = nil;
	norpath = nil;
	sname = "gouraud";
	ARGBEGIN{
	case 't': texpath = EARGF(usage()); break;
	case 'n': norpath = EARGF(usage()); break;
	case 's': sname = EARGF(usage()); break;
	case L'ω': ω = strtod(EARGF(usage()), nil)*DEG; break;
	case L'ι': inception++; break;
	case 'p': doprof++; break;
	default: usage();
	}ARGEND;
	if(argc < 1)
		usage();

	confproc();

	if((shader = getshader(sname)) == nil)
		sysfatal("couldn't find %s shader", sname);

	scene = newscene(nil);
	while(argc--){
		mdlpath = argv[argc];
		model = newmodel();
		subject = newentity(model);
		subject->p.x = argc*4;
		scene->addent(scene, subject);

		if((model->obj = objparse(mdlpath)) == nil)
			sysfatal("objparse: %r");
		if(argc == 0 && texpath != nil){
			fd = open(texpath, OREAD);
			if(fd < 0)
				sysfatal("open: %r");
			if((model->tex = readmemimage(fd)) == nil)
				sysfatal("readmemimage: %r");
			close(fd);
		}
		if(argc == 0 && norpath != nil){
			fd = open(norpath, OREAD);
			if(fd < 0)
				sysfatal("open: %r");
			if((model->nor = readmemimage(fd)) == nil)
				sysfatal("readmemimage: %r");
			close(fd);
		}
		refreshmodel(model);
	}

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
		cams[i].s = scene;
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
