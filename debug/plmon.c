#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>

enum {
	Graphoff = 40,
	Slotht = 10,
};

enum {
	CMain,
	CBack,
	CTdelim,
	NCOLOR,
};

typedef struct Slot Slot;
typedef struct Task Task;
typedef struct Schedule Schedule;

struct Slot
{
	uvlong t0, t1;
};

struct Task
{
	char *name;
	Slot *times;
	ulong ntime;
};

struct Schedule
{
	Task *tasks;
	ulong ntask;
};

Rectangle UR = {0,0,1,1};

Image *pal[NCOLOR];
RFrame graphrf;
Schedule sched;
int scale;
Slot *curts;
Channel *drawc;
char *units[] = { "ns", "µs", "ms", "s" };
double mag;
int Δx;

void redraw(void);

static Image*
eallocimage(Display *d, Rectangle r, ulong chan, int repl, ulong col)
{
	Image *i;

	i = allocimage(d, r, chan, repl, col);
	if(i == nil)
		sysfatal("allocimage: %r");
	return i;
}

static void
addt(char *n, Slot s)
{
	Task *t;
	int i;

	t = nil;

	for(i = 0; i < sched.ntask; i++)
		if(strcmp(n, sched.tasks[i].name) == 0){
			t = &sched.tasks[i];
			break;
		}
	if(t == nil){
		sched.tasks = realloc(sched.tasks, ++sched.ntask*sizeof(*sched.tasks));
		t = &sched.tasks[sched.ntask-1];
		memset(t, 0, sizeof *t);
		t->name = strdup(n);
	}
	t->times = realloc(t->times, ++t->ntime*sizeof(*t->times));
	t->times[t->ntime-1] = s;
}

static void
printsched(void)
{
	Task *t;
	Slot *s;

	for(t = sched.tasks; t && t < sched.tasks + sched.ntask; t++)
		for(s = t->times; s < t->times + t->ntime; s++)
			print("%s\t%llud\t%llud\n", t->name, s->t0, s->t1);
}

static void
initcolors(void)
{
	pal[CMain] = display->black;
	pal[CBack] = display->white;
	pal[CTdelim] = eallocimage(display, UR, screen->chan, 1, 0xEEEEEEff);
}

void
lmb(Mousectl *mc)
{
	Mouse m;

	for(;;){
		m = mc->Mouse;
		if(readmouse(mc) < 0)
			break;
		if((mc->buttons & 7) != 1)
			break;
		graphrf.p.x += mc->xy.x - m.xy.x;
		if(graphrf.p.x > Graphoff)
			graphrf.p.x = Graphoff;
		redraw();
	}
}

void
rmb(Mousectl *mc)
{
	Task *t;
	Slot *s;
	Rectangle r;
	Point2 p;
	int dy;

	dy = (Dy(screen->r) - font->height)/sched.ntask;
	for(t = sched.tasks; t < sched.tasks+sched.ntask; t++){
		graphrf.p.y = (t - sched.tasks)*dy+dy;
		for(s = t->times; s < t->times+t->ntime; s++){
			p = invrframexform(Pt2(s->t0,0,1), graphrf);
			r.min = Pt(p.x,p.y-Slotht);
			p = invrframexform(Pt2(s->t1,0,1), graphrf);
			r.max = Pt(p.x+1,p.y);
			if(r.min.x < Graphoff)
				r.min.x = Graphoff;
			if(ptinrect(subpt(mc->xy, screen->r.min), r)){
				curts = s;
				nbsend(drawc, nil);
				return;
			}
		}
	}
}

void
zoomin(void)
{
	Point2 op;

	if(scale == 1)
		return;
	op = rframexform(Pt2(Graphoff,0,1), graphrf);
	graphrf.bx.x = pow10(++scale);
	op = invrframexform(op, graphrf);
	graphrf.p.x -= op.x-Graphoff;
	mag = pow10(abs(scale)%3);
	nbsend(drawc, nil);
}

void
zoomout(void)
{
	Point2 op;

	if(abs(scale) == 3*(nelem(units)-1))
		return;
	else if(scale == 1)
		scale = 0;
	op = rframexform(Pt2(Graphoff,0,1), graphrf);
	graphrf.bx.x = pow10(--scale);
	op = invrframexform(op, graphrf);
	graphrf.p.x -= op.x-Graphoff;
	mag = pow10(abs(scale)%3);
	nbsend(drawc, nil);
}

void
mouse(Mousectl *mc)
{
	if(mc->buttons & 1)
		lmb(mc);
	if(mc->buttons & 4)
		rmb(mc);
	if(mc->buttons & 8)
		zoomin();
	if(mc->buttons & 16)
		zoomout();
}

void
resized(void)
{
	lockdisplay(display);
	if(getwindow(display, Refnone) < 0)
		sysfatal("resize failed");
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
	case Kleft:
		graphrf.p.x -= 10;
		nbsend(drawc, nil);
		break;
	case Kright:
		graphrf.p.x += 10;
		if(graphrf.p.x > Graphoff)
			graphrf.p.x = Graphoff;
		nbsend(drawc, nil);
		break;
	}
}

void
redraw(void)
{
	Rectangle r;
	Slot *s;
	Point2 p;
	char info[128];
	int i, dy, yoff, xoff;

	lockdisplay(display);

	draw(screen, screen->r, pal[CBack], nil, ZP);

	/* time axis (horizontal) */
	xoff = fmod(graphrf.p.x, Δx);
	for(i = xoff; i < Dx(screen->r); i += Δx){
		if(i < Graphoff)
			continue;
		line(screen, addpt(screen->r.min, Pt(i,Graphoff/2)), addpt(screen->r.min, Pt(i,Graphoff)), 0, 0, 0, pal[CMain], ZP);
		line(screen, addpt(screen->r.min, Pt(i,Graphoff)), Pt(screen->r.min.x+i,screen->r.max.y), 0, 0, 0, pal[CTdelim], ZP);
		p = rframexform(Pt2(i,0,1), graphrf);
		snprint(info, sizeof info, "%.0f", p.x*mag*pow10(scale));
		string(screen, addpt(screen->r.min, Pt(i+2,Graphoff/2-2)), pal[CMain], ZP, font, info);
	}
//	snprint(info, sizeof info, "t(%s) %.0f/px", units[abs(scale)/3], mag);
	snprint(info, sizeof info, "t(%s)", units[abs(scale)/3]);
	if(curts != nil)
		snprint(info+strlen(info), sizeof(info)-strlen(info), " t0 %llud t1 %llud", curts->t0, curts->t1);
	string(screen, addpt(screen->r.min, Pt(Graphoff+2,0)), pal[CMain], ZP, font, info);
	line(screen, addpt(screen->r.min, Pt(0, Graphoff)), addpt(screen->r.min, Pt(Dx(screen->r), Graphoff)), 0, 0, 0, pal[CMain], ZP);

	/* tasks axis (vertical) */
	dy = (Dy(screen->r) - font->height)/sched.ntask;
	for(i = 0; i < sched.ntask; i++){
		yoff = i*dy+dy;
		string(screen, addpt(screen->r.min, Pt(0,yoff)), pal[CMain], ZP, font, sched.tasks[i].name);
		line(screen, addpt(screen->r.min, Pt(Graphoff/2,yoff+font->height)), addpt(screen->r.min, Pt(Graphoff,yoff+font->height)), 0, 0, 0, pal[CMain], ZP);

		graphrf.p.y = yoff;
		for(s = sched.tasks[i].times; s < sched.tasks[i].times + sched.tasks[i].ntime; s++){
			p = invrframexform(Pt2(s->t0,0,1), graphrf);
			if(p.x > Dx(screen->r))
				break;
			r.min = Pt(p.x,p.y-Slotht);
			p = invrframexform(Pt2(s->t1,0,1), graphrf);
			if(p.x < Graphoff)
				continue;
			r.max = Pt(p.x+1,p.y);
			if(r.min.x < Graphoff)
				r.min.x = Graphoff;
			draw(screen, rectaddpt(r, screen->r.min), pal[CMain], nil, ZP);
		}
	}
	line(screen, addpt(screen->r.min, Pt(Graphoff, 0)), addpt(screen->r.min, Pt(Graphoff, Dy(screen->r))), 0, 0, 0, pal[CMain], ZP);

	flushimage(display, 1);
	unlockdisplay(display);
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
	Mousectl *mc;
	Keyboardctl *kc;
	Biobuf *bin;
	Slot s;
	Rune r;
	char *line, *f[3];
	ulong nf;

	ARGBEGIN{
	default: usage();
	}ARGEND
	if(argc != 0)
		usage();

	bin = Bfdopen(0, OREAD);
	if(bin == nil)
		sysfatal("Bfdopen: %r");
	while((line = Brdline(bin, '\n')) != nil){
		line[Blinelen(bin)-1] = 0;
		nf = tokenize(line, f, 3);
		if(nf != 3)
			continue;
		s.t0 = strtoul(f[1], nil, 10);
		s.t1 = strtoul(f[2], nil, 10);
		if(s.t0 >= s.t1)
			continue;
		addt(f[0], s);
	}

	if(initdraw(nil, nil, "plmon") < 0)
		sysfatal("initdraw: %r");
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	initcolors();

	scale = -3; /* µs */
	graphrf.p = Pt2(Graphoff,Graphoff,1);
	graphrf.bx = Vec2(pow10(scale),0);
	graphrf.by = Vec2(0,1);
	Δx = 100;
	mag = pow10(abs(scale)%3);
	drawc = chancreate(sizeof(void*), 1);

	display->locking = 1;
	unlockdisplay(display);
	nbsend(drawc, nil);

	enum { MOUSE, RESIZE, KEY, DRAW };
	Alt a[] = {
		{mc->c, &mc->Mouse, CHANRCV},
		{mc->resizec, nil, CHANRCV},
		{kc->c, &r, CHANRCV},
		{drawc, nil, CHANRCV},
		{nil, nil, CHANEND},
	};
	for(;;)
		switch(alt(a)){
		case MOUSE: mouse(mc); break;
		case RESIZE: resized(); break;
		case KEY: key(r); break;
		case DRAW: redraw(); break;
		}
}
