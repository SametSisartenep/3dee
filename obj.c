#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libgraphics/graphics.h"
#include "fns.h"
#include "libobj/obj.h"

static Point3
VGP3(OBJVertex v)
{
	return Pt3(v.x, v.y, v.z, 1);
}

static Point2
VGP2(OBJVertex v)
{
	return Pt2(v.x, v.y, 1);
}

/*
 * fan triangulation.
 *
 * TODO check that the polygon is in fact convex
 * try to adapt if not (by finding a convex
 * vertex), or discard it.
 */
static int
triangulate(OBJElem **newe, OBJElem *e)
{
	OBJIndexArray *newidxtab;
	OBJIndexArray *gidxtab, *idxtab;
	int i;

	gidxtab = &e->indextab[OBJVGeometric];
	for(i = 0; i < gidxtab->nindex-2; i++){
		idxtab = &e->indextab[OBJVGeometric];
		newe[i] = emalloc(sizeof **newe);
		memset(newe[i], 0, sizeof **newe);
		newe[i]->type = OBJEFace;
		newe[i]->mtl = e->mtl;
		newidxtab = &newe[i]->indextab[OBJVGeometric];
		newidxtab->nindex = 3;
		newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
		newidxtab->indices[0] = idxtab->indices[0];
		newidxtab->indices[1] = idxtab->indices[i+1];
		newidxtab->indices[2] = idxtab->indices[i+2];
		idxtab = &e->indextab[OBJVTexture];
		if(idxtab->nindex > 0){
			newidxtab = &newe[i]->indextab[OBJVTexture];
			newidxtab->nindex = 3;
			newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
			newidxtab->indices[0] = idxtab->indices[0];
			newidxtab->indices[1] = idxtab->indices[i+1];
			newidxtab->indices[2] = idxtab->indices[i+2];
		}
		idxtab = &e->indextab[OBJVNormal];
		if(idxtab->nindex > 0){
			newidxtab = &newe[i]->indextab[OBJVNormal];
			newidxtab->nindex = 3;
			newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
			newidxtab->indices[0] = idxtab->indices[0];
			newidxtab->indices[1] = idxtab->indices[i+1];
			newidxtab->indices[2] = idxtab->indices[i+2];
		}
	}

	return i;
}

typedef struct OBJ2MtlEntry OBJ2MtlEntry;
typedef struct OBJ2MtlMap OBJ2MtlMap;

struct OBJ2MtlEntry
{
	OBJMaterial *objmtl;
	ulong idx;
	OBJ2MtlEntry *next;
};

struct OBJ2MtlMap
{
	OBJ2MtlEntry *head;
	Material *mtls;
};

static void
addmtlmap(OBJ2MtlMap *map, OBJMaterial *om, ulong idx)
{
	OBJ2MtlEntry *e;

	if(om == nil)
		return;

	e = emalloc(sizeof *e);
	memset(e, 0, sizeof *e);
	e->objmtl = om;
	e->idx = idx;

	if(map->head == nil){
		map->head = e;
		return;
	}

	e->next = map->head;
	map->head = e;
}

static Material *
getmtlmap(OBJ2MtlMap *map, OBJMaterial *om)
{
	OBJ2MtlEntry *e;

	for(e = map->head; e != nil; e = e->next)
		if(e->objmtl == om)
			return &map->mtls[e->idx];
	return nil;
}

static void
clrmtlmap(OBJ2MtlMap *map)
{
	OBJ2MtlEntry *e, *ne;

	for(e = map->head; e != nil; e = ne){
		ne = e->next;
		free(e);
	}
}

static int
loadobjmodel(Model *m, OBJ *obj)
{
	Primitive prim;
	Vertex v, *vp;
	OBJVertex *pverts, *tverts, *nverts;		/* geometric, texture and normals vertices */
	OBJElem **trielems, *e, *ne;
	OBJObject *o;
	OBJIndexArray *idxtab;
	OBJ2MtlMap mtlmap;
	OBJMaterial *objmtl;
	Material *mtl;
	int i, idx, nt, maxnt, hastexcoords, neednormal, gottaclean;
	int defcolidx, nidx;				/* default color and normal indices */

	pverts = obj->vertdata[OBJVGeometric].verts;
	tverts = obj->vertdata[OBJVTexture].verts;
	nverts = obj->vertdata[OBJVNormal].verts;
	trielems = nil;
	maxnt = 0;

	mtlmap.head = nil;
	for(i = 0; obj->materials != nil && i < nelem(obj->materials->mattab); i++)
		for(objmtl = obj->materials->mattab[i]; objmtl != nil; objmtl = objmtl->next){
			mtlmap.mtls = m->materials = erealloc(m->materials, ++m->nmaterials*sizeof(*m->materials));
			mtl = &m->materials[m->nmaterials-1];
			memset(mtl, 0, sizeof *mtl);

			mtl->name = strdup(objmtl->name);
			if(mtl->name == nil)
				sysfatal("strdup: %r");

			if(objmtl->Ka.a > 0)
				mtl->ambient = objmtl->Ka;
			if(objmtl->Kd.a > 0)
				mtl->diffuse = objmtl->Kd;
			if(objmtl->Ks.a > 0)
				mtl->specular = objmtl->Ks;
			mtl->shininess = objmtl->Ns;

			if(objmtl->map_Kd != nil){
				mtl->diffusemap = alloctexture(sRGBTexture, nil);
				mtl->diffusemap->image = dupmemimage(objmtl->map_Kd->image);
			}

			if(objmtl->map_Ks != nil){
				mtl->specularmap = alloctexture(sRGBTexture, nil);
				mtl->specularmap->image = dupmemimage(objmtl->map_Ks->image);
			}

			if(objmtl->norm != nil){
				mtl->normalmap = alloctexture(RAWTexture, nil);
				mtl->normalmap->image = dupmemimage(objmtl->norm->image);
			}

			addmtlmap(&mtlmap, objmtl, m->nmaterials-1);
		}

	for(i = 0; i < obj->vertdata[OBJVGeometric].nvert; i++)
		m->addposition(m, VGP3(pverts[i]));
	for(i = 0; i < obj->vertdata[OBJVNormal].nvert; i++)
		m->addnormal(m, VGP3(nverts[i]));
	for(i = 0; i < obj->vertdata[OBJVTexture].nvert; i++)
		m->addtexcoord(m, VGP2(tverts[i]));
	defcolidx = m->addcolor(m, Pt3(1,1,1,1));

	for(i = 0; i < nelem(obj->objtab); i++)
		for(o = obj->objtab[i]; o != nil; o = o->next)
			for(e = o->child; e != nil; e = ne){
				ne = e->next;

				switch(e->type){
				case OBJEPoint:
					prim = mkprim(PPoint);
					prim.mtl = getmtlmap(&mtlmap, e->mtl);

					v = mkvert();
					idxtab = &e->indextab[OBJVGeometric];
					v.p = idxtab->indices[0];
					v.c = defcolidx;

					idxtab = &e->indextab[OBJVTexture];
					if(idxtab->nindex == 1)
						v.uv = idxtab->indices[0];

					prim.v[0] = m->addvert(m, v);
					m->addprim(m, prim);
					break;
				case OBJELine:
					prim = mkprim(PLine);
					prim.mtl = getmtlmap(&mtlmap, e->mtl);

					for(idx = 0; idx < 2; idx++){
						v = mkvert();
						idxtab = &e->indextab[OBJVGeometric];
						v.p = idxtab->indices[idx];
						v.c = defcolidx;

						idxtab = &e->indextab[OBJVTexture];
						if(idxtab->nindex == 2)
							v.uv = idxtab->indices[idx];
						prim.v[idx] = m->addvert(m, v);
					}
					m->addprim(m, prim);
					break;
				case OBJEFace:
					idxtab = &e->indextab[OBJVGeometric];
					assert(idxtab->nindex >= 3);
					gottaclean = 0;

					/* it takes n-2 triangles to fill any given n-gon */
					nt = idxtab->nindex-2;
					if(nt > maxnt){
						maxnt = nt;
						trielems = erealloc(trielems, maxnt*sizeof(*trielems));
					}
					if(nt > 1){
						assert(triangulate(trielems, e) == nt);
						gottaclean = 1;
					}else
						trielems[0] = e;

					while(nt-- > 0){
						e = trielems[nt];
						hastexcoords = 0;
						neednormal = 0;

						prim = mkprim(PTriangle);
						prim.mtl = getmtlmap(&mtlmap, e->mtl);

						for(idx = 0; idx < 3; idx++){
							v = mkvert();
							idxtab = &e->indextab[OBJVGeometric];
							v.p = idxtab->indices[idx];
							v.c = defcolidx;

							idxtab = &e->indextab[OBJVNormal];
							if(idxtab->nindex == 3)
								v.n = idxtab->indices[idx];
							else
								neednormal = 1;

							idxtab = &e->indextab[OBJVTexture];
							if(idxtab->nindex == 3){
								hastexcoords = 1;
								v.uv = idxtab->indices[idx];
							}
							prim.v[idx] = m->addvert(m, v);
						}

						if(hastexcoords){
							Point3 *p[3], e0, e1, tangent;
							Point2 *uv[3], Δuv0, Δuv1;
							double det;

							for(idx = 0; idx < 3; idx++){
								vp = itemarrayget(m->verts, prim.v[idx]);
								p[idx] = itemarrayget(m->positions, vp->p);
								uv[idx] = itemarrayget(m->texcoords, vp->uv);
							}

							e0 = subpt3(*p[1], *p[0]);
							e1 = subpt3(*p[2], *p[0]);
							Δuv0 = subpt2(*uv[1], *uv[0]);
							Δuv1 = subpt2(*uv[2], *uv[0]);

							det = Δuv0.x * Δuv1.y - Δuv1.x * Δuv0.y;
							det = det == 0? 0: 1.0/det;

							tangent.x = det*(Δuv1.y * e0.x - Δuv0.y * e1.x);
							tangent.y = det*(Δuv1.y * e0.y - Δuv0.y * e1.y);
							tangent.z = det*(Δuv1.y * e0.z - Δuv0.y * e1.z);
							tangent.w = 0;
							tangent = normvec3(tangent);

							prim.tangent = m->addtangent(m, tangent);
						}

						if(neednormal){
							Point3 *p[3], n;

							for(idx = 0; idx < 3; idx++){
								vp = itemarrayget(m->verts, prim.v[idx]);
								p[idx] = itemarrayget(m->positions, vp->p);
							}

							n = normvec3(crossvec3(subpt3(*p[1], *p[0]), subpt3(*p[2], *p[0])));
							nidx = m->addnormal(m, n);
							for(idx = 0; idx < 3; idx++){
								vp = itemarrayget(m->verts, prim.v[idx]);
								vp->n = nidx;
							}
						}

						m->addprim(m, prim);

						if(gottaclean){
							free(e->indextab[OBJVGeometric].indices);
							free(e->indextab[OBJVNormal].indices);
							free(e->indextab[OBJVTexture].indices);
							free(e);
						}
					}
					break;
				default: continue;
				}
			}

	free(trielems);
	clrmtlmap(&mtlmap);
	return m->prims->nitems;
}

static Model *
readobjmodel(char *path)
{
	Model *m;
	OBJ *obj;

	m = newmodel();
	if((obj = objparse(path)) == nil)
		sysfatal("objparse: %r");
	loadobjmodel(m, obj);
	objfree(obj);
	return m;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-d] [objfile [dstdir]]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Model *m;
	char *infile, *dstdir;
	int dedup;

	dedup = 1;
	ARGBEGIN{
	case 'd': dedup--; break;	/* TODO waiting for a Model compaction routine */
	default: usage();
	}ARGEND;
	if(argc > 2)
		usage();

	infile = argc > 0? argv[0]: "/fd/0";
	dstdir = argc == 2? argv[1]: nil;

	m = readobjmodel(infile);
	if(m == nil)
		sysfatal("readobjmodel: %r");

	if(dstdir == nil){
		if(writemodel(1, m) == 0)
			sysfatal("writemodel: %r");
	}else{
		if(exportmodel(dstdir, m) < 0)
			sysfatal("exportmodel: %r");
	}

	exits(nil);
}
