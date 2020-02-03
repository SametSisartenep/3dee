#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include "../obj.h"

#undef isspace(c)
#define isspace(c) ((c) == ' ' || (c) == '\t')

typedef struct Line Line;
struct Line
{
	char *file;
	ulong lineno;
};

static Line curline;

static void
error(char *fmt, ...)
{
	va_list va;
	char buf[ERRMAX], *bp;

	va_start(va, fmt);
	bp = seprint(buf, buf + sizeof buf, "%s:%lud ", curline.file, curline.lineno);
	vseprint(bp, buf + sizeof buf, fmt, va);
	va_end(va);
	werrstr("%s\n", buf);
}

static void *
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		sysfatal("malloc: %r");
	memset(p, 0, n);
	setmalloctag(p, getcallerpc(&n));
	return p;
}

static void *
erealloc(void *v, ulong n)
{
	void *nv;

	nv = realloc(v, n);
	if(nv == nil)
		sysfatal("realloc: %r");
	setrealloctag(nv, getcallerpc(&v));
	return nv;
}

static uint
hash(char *s)
{
	uint h;

	h = 0x811c9dc5;
	while(*s != 0)
		h = (h^(uchar)*s++) * 0x1000193;
	return h % OBJHTSIZE;
}

static void
addvertva(OBJVertexArray *va, OBJVertex v)
{
	va->verts = erealloc(va->verts, ++va->nvert*sizeof(OBJVertex));
	va->verts[va->nvert-1] = v;
}

static void
addvert(OBJ *obj, OBJVertex v, int vtype)
{
	addvertva(&obj->vertdata[vtype], v);
}

static void
addelem(OBJObject *o, OBJElem *e)
{
	OBJElem *ep;

	if(o->child == nil){
		o->child = e;
		return;
	}
	for(ep = o->child; ep->next != nil; ep = ep->next)
		;
	ep->next = e;
}

static OBJElem *
allocelem(int t)
{
	OBJElem *e;

	e = emalloc(sizeof(OBJElem));
	e->type = t;
	return e;
}

static void
addelemidx(OBJElem *e, int idx)
{
	e->indices = erealloc(e->indices, ++e->nindex*sizeof(int));
	e->indices[e->nindex-1] = idx;
}

static void
freeelem(OBJElem *e)
{
	free(e->indices);
	free(e);
}

static OBJObject *
alloco(char *n)
{
	OBJObject *o;

	o = emalloc(sizeof(OBJObject));
	o->name = strdup(n);
	return o;
}

static void
freeo(OBJObject *o)
{
	OBJElem *e, *ne;

	free(o->name);
	for(e = o->child; e != nil; e = ne){
		ne = e->next;
		freeelem(e);
	}
	free(o);
}

static void
pusho(OBJ *obj, OBJObject *o)
{
	OBJObject *op, *prev;
	uint h;

	prev = nil;
	h = hash(o->name);
	for(op = obj->objtab[h]; op != nil; prev = op, op = op->next)
		if(strcmp(op->name, o->name) == 0){
			o->next = op->next;
			freeo(op);
			break;
		}
	if(prev == nil){
		obj->objtab[h] = o;
		return;
	}
	prev->next = o;
}

static OBJObject *
geto(OBJ *obj, char *n)
{
	OBJObject *o;
	uint h;

	h = hash(n);
	for(o = obj->objtab[h]; o != nil; o = o->next)
		if(strcmp(o->name, n) == 0)
			break;
	return o;
}

OBJ *
objparse(char *file)
{
	Biobuf *bin;
	OBJ *obj;
	OBJObject *o;
	OBJElem *e;
	OBJVertex v;
	double *d;
	char c, buf[256], *p;
	int vtype, idx, sign;

	o = nil;
	bin = Bopen(file, OREAD);
	if(bin == nil)
		sysfatal("Bopen: %r");
	curline.file = file;
	curline.lineno = 1;
	obj = emalloc(sizeof(OBJ));
	while((c = Bgetc(bin)) != Beof){
		switch(c){
		case 'v':
			d = (double*)&v;
			c = Bgetc(bin);
			vtype = OBJVGeometric;
			switch(c){
			case 't': vtype = OBJVTexture; break;
			case 'p': vtype = OBJVParametric; break;
			case 'n': vtype = OBJVNormal; break;
			default:
				if(!isspace(c)){
					error("wrong vertex type");
					goto error;
				}
			}
			while(c = Bgetc(bin), c != Beof && c != '\n' && d-(double*)&v < 4){
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					continue;
				}
				if(c != '-' && !isdigit(c)){
					error("unexpected character '%c'", c);
					goto error;
				}
				Bungetc(bin);
				Bgetd(bin, d++);
			}
			switch(vtype){
			case OBJVGeometric:
				if(d-(double*)&v < 3){
					error("not enough coordinates");
					goto error;
				}
				if(d-(double*)&v < 4)
					*d = 1;	/* default w value */
				break;
			case OBJVTexture:
				if(d-(double*)&v < 1){
					error("not enough coordinates");
					goto error;
				}
				while(d-(double*)&v < 3)
					*d++ = 0;	/* default v and w values */
				break;
			case OBJVParametric:
				if(d-(double*)&v < 2){
					error("not enough coordinates");
					goto error;
				}
				if(d-(double*)&v < 3)
					*d = 1;	/* default w value */
				break;
			case OBJVNormal:
				if(d-(double*)&v < 3){
					error("not enough coordinates");
					goto error;
				}
			}
			addvert(obj, v, vtype);
			break;
		case 'o':
			p = buf;
			c = Bgetc(bin);
			if(!isspace(c)){
				error("syntax error");
				goto error;
			}
			while(isspace(c))
				c = Bgetc(bin);
			if(!isalnum(c)){
				error("unexpected character '%c'", c);
				goto error;
			}
			do{
				*p++ = c;
			}while(c = Bgetc(bin), isalnum(c) && p-buf < sizeof(buf)-1);
			*p = 0;
			o = geto(obj, buf);
			if(o == nil){
				o = alloco(buf);
				pusho(obj, o);
			}
			break;
		case 'g':
		case 's':
			/* element and smoothing groups ignored for now */
			while(c != '\n')
				c = Bgetc(bin);
			break;
		case 'p':
			c = Bgetc(bin);
			if(!isspace(c)){
				error("syntax error");
				goto error;
			}
			while(c = Bgetc(bin), c != '\n'){
				idx = 0;
				sign = 0;
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					continue;
				}
				if(c != '-' && !isdigit(c)){
					error("unexpected character '%c'", c);
					goto error;
				}
				if(c == '-'){
					sign = 1;
					c = Bgetc(bin);
					if(!isdigit(c)){
						error("unexpected character '%c'", c);
						goto error;
					}
				}
				do{
					idx = idx*10 + c-'0';
				}while(c = Bgetc(bin), isdigit(c));
				Bungetc(bin);
				idx = sign ? obj->vertdata[OBJVGeometric].nvert-idx : idx-1;
				if(idx+1 > obj->vertdata[OBJVGeometric].nvert){
					error("not enough vertices");
					goto error;
				}
				e = allocelem(OBJEPoint);
				addelemidx(e, idx);
				if(o == nil){
					o = alloco("default");
					pusho(obj, o);
				}
				addelem(o, e);
			}
			break;
		case 'l':
			c = Bgetc(bin);
			if(!isspace(c)){
				error("syntax error");
				goto error;
			}
			while(c = Bgetc(bin), c != '\n'){
				idx = 0;
				sign = 0;
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					continue;
				}
				if(c != '-' && !isdigit(c)){
					error("unexpected character '%c'", c);
					goto error;
				}
				if(c == '-'){
					sign = 1;
					c = Bgetc(bin);
					if(!isdigit(c)){
						error("unexpected character '%c'", c);
						goto error;
					}
				}
				do{
					idx = idx*10 + c-'0';
				}while(c = Bgetc(bin), isdigit(c));
				idx = sign ? obj->vertdata[OBJVGeometric].nvert-idx : idx-1;
				if(idx+1 > obj->vertdata[OBJVGeometric].nvert){
					error("not enough vertices");
					goto error;
				}
				e = allocelem(OBJELine);
				addelemidx(e, idx);
Line2:
				idx = 0;
				sign = 0;
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					c = Bgetc(bin);
					goto Line2;
				}
				if(c != '-' && !isdigit(c)){
					freeelem(e);
					error("unexpected character '%c'", c);
					goto error;
				}
				if(c == '-'){
					sign = 1;
					c = Bgetc(bin);
					if(!isdigit(c)){
						freeelem(e);
						error("unexpected character '%c'", c);
						goto error;
					}
				}
				do{
					idx = idx*10 + c-'0';
				}while(c = Bgetc(bin), isdigit(c));
				Bungetc(bin);
				idx = sign ? obj->vertdata[OBJVGeometric].nvert-idx : idx-1;
				if(idx+1 > obj->vertdata[OBJVGeometric].nvert){
					freeelem(e);
					error("not enough vertices");
					goto error;
				}
				addelemidx(e, idx);
				if(o == nil){
					o = alloco("default");
					pusho(obj, o);
				}
				addelem(o, e);
			}
			break;
		case 'f':
			e = allocelem(OBJEFace);
			c = Bgetc(bin);
			if(!isspace(c)){
				freeelem(e);
				error("syntax error");
				goto error;
			}
			while(c = Bgetc(bin), c != '\n'){
				idx = 0;
				sign = 0;
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					continue;
				}
				if(c != '-' && !isdigit(c)){
					freeelem(e);
					error("unexpected character '%c'", c);
					goto error;
				}
				if(c == '-'){
					sign = 1;
					c = Bgetc(bin);
					if(!isdigit(c)){
						freeelem(e);
						error("unexpected character '%c'", c);
						goto error;
					}
				}
				do{
					idx = idx*10 + c-'0';
				}while(c = Bgetc(bin), isdigit(c));
				Bungetc(bin);
				idx = sign ? obj->vertdata[OBJVGeometric].nvert-idx : idx-1;
				if(idx+1 > obj->vertdata[OBJVGeometric].nvert){
					freeelem(e);
					error("not enough vertices");
					goto error;
				}
				addelemidx(e, idx);
			}
			if(o == nil){
				o = alloco("default");
				pusho(obj, o);
			}
			addelem(o, e);
			break;
		case 'm':
		case 'u':
			p = buf;
			do{
				*p++ = c;
			}while(c = Bgetc(bin), isalpha(c) && p-buf < sizeof(buf)-1);
			*p = 0;
			if(strcmp(buf, "mtllib") != 0 && strcmp(buf, "usemtl") != 0){
				error("syntax error");
				goto error;
			}
			while(c != '\n')
				c = Bgetc(bin);
			break;
		case '#':
			while(c != '\n')
				c = Bgetc(bin);
			break;
		}
		do{
			if(c == '\n'){
				curline.lineno++;
				break;
			}
			if(!isspace(c)){
				error("syntax error");
				goto error;
			}
		}while((c = Bgetc(bin)) != Beof);
	}
	Bterm(bin);
	return obj;
error:
	objfree(obj);
	Bterm(bin);
	return nil;
}

void
objfree(OBJ *obj)
{
	OBJObject *o, *no;
	int i;

	if(obj == nil)
		return;
	for(i = 0; i < nelem(obj->vertdata); i++)
		free(obj->vertdata[i].verts);
	for(i = 0; i < nelem(obj->objtab); i++)
		for(o = obj->objtab[i]; o != nil; o = no){
			no = o->next;
			freeo(o);
		}
	free(obj);
}

int
OBJfmt(Fmt *f)
{
	OBJ *obj;
	OBJObject *o;
	OBJElem *e;
	OBJVertex v;
	int i, j, r, pack;

	r = pack = 0;
	obj = va_arg(f->args, OBJ*);
	for(i = 0; i < nelem(obj->vertdata); i++)
		for(j = 0; j < obj->vertdata[i].nvert; j++){
			v = obj->vertdata[i].verts[j];
			switch(i){
			case OBJVGeometric:
				r += fmtprint(f, "v %g %g %g %g\n", v.x, v.y, v.z, v.w);
				break;
			case OBJVTexture:
				r += fmtprint(f, "vt %g %g %g\n", v.u, v.v, v.vv);
				break;
			case OBJVNormal:
				r += fmtprint(f, "vn %g %g %g\n", v.i, v.j, v.k);
				break;
			case OBJVParametric:
				r += fmtprint(f, "vp %g %g %g\n", v.u, v.v, v.vv);
				break;
			}
		}
	for(i = 0; i < nelem(obj->objtab); i++)
		for(o = obj->objtab[i]; o != nil; o = o->next){
			if(strcmp(o->name, "default") != 0)
				r += fmtprint(f, "o %s\n", o->name);
			for(e = o->child; e != nil; e = e->next){
				switch(e->type){
				case OBJEPoint:
					if(pack == 0)
						r += fmtprint(f, "p");
					pack = pack > 0 ? --pack : 8-1;
					break;
				case OBJELine:
					r += fmtprint(f, "l");
					break;
				case OBJEFace:
					r += fmtprint(f, "f");
					break;
				//case OBJECurve:
				//case OBJECurve2:
				//case OBJESurface:
				}
				for(j = 0; j < e->nindex; j++)
					r += fmtprint(f, " %d", e->indices[j]+1);
				if(e->type != OBJEPoint || pack == 0)
					r += fmtprint(f, "\n");
			}
		}
	return r;
}

void
OBJfmtinstall(void)
{
	fmtinstall('O', OBJfmt);
}
