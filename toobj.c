#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include <pool.h>
#include "libgraphics/graphics.h"
#include "fns.h"
#include "libobj/obj.h"

static OBJVertex
GP2V(Point2 *p)
{
	OBJVertex v;

	v.x = p->x;
	v.y = p->y;
	v.z = p->w;
	v.w = 0;
	return v;
}

static OBJVertex
GP3V(Point3 *p)
{
	OBJVertex v;

	v.x = p->x;
	v.y = p->y;
	v.z = p->z;
	v.w = p->w;
	return v;
}

static int
loadobjmodel(OBJ *obj, Model *m)
{
	Primitive *prim, *lastprim;
	Vertex *v;
	OBJElem *e;
	OBJObject *o;
	OBJMaterial *objmtl;
	Material *mtl;
	int i;

	if(m->nmaterials > 0)
		obj->materials = objallocmtl("main.mtl");
	for(mtl = m->materials; mtl < m->materials + m->nmaterials; mtl++){
		objmtl = objallocmt(mtl->name);

		if(mtl->ambient.a > 0)
			objmtl->Ka = mtl->ambient;
		if(mtl->diffuse.a > 0)
			objmtl->Kd = mtl->diffuse;
		if(mtl->specular.a > 0)
			objmtl->Ks = mtl->specular;
		objmtl->Ns = mtl->shininess;

		/* TODO the default export format should be set by the user */
		if(mtl->diffusemap != nil){
			objmtl->map_Kd = emalloc(sizeof(OBJTexture));
			objmtl->map_Kd->image = dupmemimage(mtl->diffusemap->image);
			objmtl->map_Kd->filename = mtl->diffusemap->file != nil?
				strdup(mtl->diffusemap->file):
				smprint("%s_diffuse.png", mtl->name);
		}

		if(mtl->specularmap != nil){
			objmtl->map_Ks = emalloc(sizeof(OBJTexture));
			objmtl->map_Ks->image = dupmemimage(mtl->specularmap->image);
			objmtl->map_Ks->filename = mtl->specularmap->file != nil?
				strdup(mtl->specularmap->file):
				smprint("%s_specular.png", mtl->name);
		}

		if(mtl->normalmap != nil){
			objmtl->norm = emalloc(sizeof(OBJTexture));
			objmtl->norm->image = dupmemimage(mtl->normalmap->image);
			objmtl->norm->filename = mtl->normalmap->file != nil?
				strdup(mtl->normalmap->file):
				smprint("%s_normal.png", mtl->name);
		}

		objaddmtl(obj->materials, objmtl);
	}

	for(i = 0; i < m->positions->nitems; i++)
		objaddvertex(obj, GP3V(itemarrayget(m->positions, i)), OBJVGeometric);
	for(i = 0; i < m->normals->nitems; i++)
		objaddvertex(obj, GP3V(itemarrayget(m->normals, i)), OBJVNormal);
	for(i = 0; i < m->texcoords->nitems; i++)
		objaddvertex(obj, GP3V(itemarrayget(m->texcoords, i)), OBJVTexture);

	o = objallocobject("default");
	objpushobject(obj, o);
	lastprim = itemarrayget(m->prims, m->prims->nitems-1);
	for(prim = m->prims->items; prim <= lastprim; prim++){
		/*
		 * XXX A Model doesn't have the indexed attribute
		 * structure an OBJ has, so this conversion is very
		 * inefficient without a good deduplication algorithm.
		 */
		switch(prim->type){
		case PPoint:
			e = objallocelem(OBJEPoint);
			break;
		case PLine:
			e = objallocelem(OBJELine);
			break;
		case PTriangle:
			e = objallocelem(OBJEFace);
			break;
		default:
			continue;
		}

		for(i = 0; i < prim->type+1; i++){
			v = itemarrayget(m->verts, prim->v[i]);
			objaddelemidx(e, OBJVGeometric, v->p);
			if(v->n != NaI)
				objaddelemidx(e, OBJVNormal, v->n);
			if(v->uv != NaI)
				objaddelemidx(e, OBJVTexture, v->uv);
		}

		if(prim->mtl != nil)
			e->mtl = objgetmtl(obj->materials, prim->mtl->name);
		objaddelem(o, e);
	}

	return m->prims->nitems;
}

static void
usage(void)
{
	fprint(2, "usage: %s [mdlfile [dstdir]]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Model *m;
	OBJ *obj;
	char *infile, *dstdir;
	int fd;

	/* we could be dealing with some pretty beefy data */
	mainmem->maxsize = (uintptr)~0;

	OBJfmtinstall();
	ARGBEGIN{
	default: usage();
	}ARGEND;
	if(argc > 2)
		usage();

	infile = argc > 0? argv[0]: "/fd/0";
	dstdir = argc == 2? argv[1]: nil;

	fd = open(infile, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	m = readmodel(fd);
	if(m == nil)
		sysfatal("readmodel: %r");
	close(fd);

	obj = emalloc(sizeof *obj);
	memset(obj, 0, sizeof *obj);
	loadobjmodel(obj, m);

	if(dstdir == nil){
		if(fprint(1, "%O", obj) == 0)
			sysfatal("could not write obj model");
	}else{
		if(objexport(dstdir, obj) < 0)
			sysfatal("exportmodel: %r");
	}

	exits(nil);
}
