#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libgraphics/graphics.h"
#include "fns.h"
#include "libobj/obj.h"

static OBJVertex
GP2V(Point2 p)
{
	OBJVertex v;

	v.x = p.x;
	v.y = p.y;
	v.z = p.w;
	v.w = 0;
	return v;
}

static OBJVertex
GP3V(Point3 p)
{
	OBJVertex v;

	v.x = p.x;
	v.y = p.y;
	v.z = p.z;
	v.w = p.w;
	return v;
}

static int
loadobjmodel(OBJ *obj, Model *m)
{
	Primitive *prim;
	OBJVertex v;
	OBJElem *e;
	OBJObject *o;
	OBJMaterial *objmtl;
	Material *mtl;
	int i, idx;

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

	o = objallocobject("default");
	objpushobject(obj, o);
	for(prim = m->prims; prim < m->prims + m->nprims; prim++){
		/*
		 * XXX A Model doesn't have the indexed attribute
		 * structure an OBJ has, so this conversion is very
		 * inefficient without a good deduplication algorithm.
		 */
		switch(prim->type){
		case PPoint:
			e = objallocelem(OBJEPoint);

			v = GP3V(prim->v[0].p);
			idx = objaddvertex(obj, v, OBJVGeometric);
			objaddelemidx(e, OBJVGeometric, idx);

			if(memcmp(&prim->v[0].n, &ZP3, sizeof(Point3)) != 0){
				v = GP3V(prim->v[0].n);
				idx = objaddvertex(obj, v, OBJVNormal);
				objaddelemidx(e, OBJVNormal, idx);
			}

			if(memcmp(&prim->v[0].uv, &ZP2, sizeof(Point2)) != 0){
				v = GP2V(prim->v[0].uv);
				idx = objaddvertex(obj, v, OBJVTexture);
				objaddelemidx(e, OBJVTexture, idx);
			}

			if(prim->mtl != nil)
				e->mtl = objgetmtl(obj->materials, prim->mtl->name);
			objaddelem(o, e);
			break;
		case PLine:
			e = objallocelem(OBJELine);

			for(i = 0; i < prim->type+1; i++){
				v = GP3V(prim->v[i].p);
				idx = objaddvertex(obj, v, OBJVGeometric);
				objaddelemidx(e, OBJVGeometric, idx);

				if(memcmp(&prim->v[i].n, &ZP3, sizeof(Point3)) != 0){
					v = GP3V(prim->v[i].n);
					idx = objaddvertex(obj, v, OBJVNormal);
					objaddelemidx(e, OBJVNormal, idx);
				}

				if(memcmp(&prim->v[i].uv, &ZP2, sizeof(Point2)) != 0){
					v = GP2V(prim->v[i].uv);
					idx = objaddvertex(obj, v, OBJVTexture);
					objaddelemidx(e, OBJVTexture, idx);
				}
			}

			if(prim->mtl != nil)
				e->mtl = objgetmtl(obj->materials, prim->mtl->name);
			objaddelem(o, e);
			break;
		case PTriangle:
			e = objallocelem(OBJEFace);

			for(i = 0; i < prim->type+1; i++){
				v = GP3V(prim->v[i].p);
				idx = objaddvertex(obj, v, OBJVGeometric);
				objaddelemidx(e, OBJVGeometric, idx);

				if(memcmp(&prim->v[i].n, &ZP3, sizeof(Point3)) != 0){
					v = GP3V(prim->v[i].n);
					idx = objaddvertex(obj, v, OBJVNormal);
					objaddelemidx(e, OBJVNormal, idx);
				}

				if(memcmp(&prim->v[i].uv, &ZP2, sizeof(Point2)) != 0){
					v = GP2V(prim->v[i].uv);
					idx = objaddvertex(obj, v, OBJVTexture);
					objaddelemidx(e, OBJVTexture, idx);
				}
			}

			if(prim->mtl != nil)
				e->mtl = objgetmtl(obj->materials, prim->mtl->name);
			objaddelem(o, e);
			break;
		}
	}

	return m->nprims;
}

static void
usage(void)
{
	fprint(2, "usage: %s [mdlfile [dstdir]]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Model *m;
	OBJ *obj;
	char *infile, *dstdir;
	int fd;

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
