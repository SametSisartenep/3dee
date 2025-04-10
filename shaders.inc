
Point3
gouraudvshader(Shaderparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Point3 lightdir, lookdir;
	Material m;
	LightSource *l;
	Color ambient, diffuse, specular, lightc;

	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);

	if(sp->v->mtl != nil)
		m = *sp->v->mtl;
	else{
		memset(&m, 0, sizeof m);
		m.diffuse = sp->v->c;
		m.specular = (Color){1,1,1,1};
		m.shininess = 1;
	}

	lightc = getscenecolor(sp->su->camera->scene, sp->v->p, sp->v->n);
	sp->v->c = ZP3;

	for(l = sp->su->camera->scene->lights.next; l != &sp->su->camera->scene->lights; l = l->next){
		lightdir = normvec3(subpt3(l->p, sp->v->p));

		ambient = mulpt3(lightc, Ka);
		ambient = modulapt3(ambient, m.diffuse);

		Kd = max(0, _Xdotvec3(sp->v->n, lightdir));
		diffuse = mulpt3(lightc, Kd);
		diffuse = modulapt3(diffuse, m.diffuse);

		lookdir = normvec3(subpt3(sp->su->camera->p, sp->v->p));
		lightdir = qrotate(lightdir, sp->v->n, PI); /* reflect */
		spec = pow(max(0, _Xdotvec3(lookdir, lightdir)), m.shininess);
		specular = mulpt3(lightc, spec*Ks);
		specular = modulapt3(specular, m.specular);

		sp->v->c = addpt3(sp->v->c, addpt3(ambient, addpt3(diffuse, specular)));
	}
	sp->v->c.a = m.diffuse.a;
	return world2clip(sp->su->camera, sp->v->p);
}

Color
gouraudshader(Shaderparams *sp)
{
	Color tc;

	if(sp->v->mtl != nil && sp->v->mtl->diffusemap != nil && sp->v->uv.w != 0)
		tc = sampletexture(sp->v->mtl->diffusemap, sp->v->uv, tsampler);
	else
		tc = (Color){1,1,1,1};

	sp->v->n.w = 1;
	sp->toraster(sp, "normals", &sp->v->n);

	return modulapt3(sp->v->c, tc);
}

Point3
phongvshader(Shaderparams *sp)
{
	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	sp->setattr(sp, "pos", VAPoint, &sp->v->p);
	if(sp->v->mtl != nil){
		if(sp->v->mtl->normalmap != nil && sp->v->uv.w != 0)
			sp->v->tangent = model2world(sp->su->entity, sp->v->tangent);

		sp->setattr(sp, "ambient", VAPoint, &sp->v->mtl->ambient);
		sp->setattr(sp, "diffuse", VAPoint, &sp->v->mtl->diffuse);
		sp->setattr(sp, "specular", VAPoint, &sp->v->mtl->specular);
		sp->setattr(sp, "shininess", VANumber, &sp->v->mtl->shininess);
	}
	return world2clip(sp->su->camera, sp->v->p);
}

Color
phongshader(Shaderparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Color ambient, diffuse, specular, lightc;
	Point3 pos, n, lightdir, lookdir;
	Material m;
	LightSource *l;
	RFrame3 TBN;
	Vertexattr *va;

	va = sp->getattr(sp, "pos");
	pos = va->p;

	va = sp->getattr(sp, "ambient");
	m.ambient = va != nil? va->p: (Point3){1,1,1,1};
	va = sp->getattr(sp, "diffuse");
	m.diffuse = va != nil? va->p: sp->v->c;
	va = sp->getattr(sp, "specular");
	m.specular = va != nil? va->p: (Point3){1,1,1,1};
	va = sp->getattr(sp, "shininess");
	m.shininess = va != nil? va->n: 1;

	n = sp->v->n;
	if(sp->v->mtl != nil && sp->v->uv.w != 0){
		/* normal mapping */
		if(sp->v->mtl->normalmap != nil){
			/* TODO implement this on the VS instead and apply Gram-Schmidt here */
			n = sampletexture(sp->v->mtl->normalmap, sp->v->uv, neartexsampler);
			n = normvec3(subpt3(mulpt3(n, 2), Vec3(1,1,1)));

			TBN.p = (Point3){0,0,0,1};
			TBN.bx = sp->v->tangent;		/* T */
			TBN.bz = sp->v->n;			/* N */
			TBN.by = crossvec3(TBN.bz, TBN.bx);	/* B */

			n = normvec3(invrframexform3(n, TBN));
		}

		if(sp->v->mtl->diffusemap != nil)
			m.diffuse = sampletexture(sp->v->mtl->diffusemap, sp->v->uv, tsampler);

		if(sp->v->mtl->specularmap != nil)
			m.specular = sampletexture(sp->v->mtl->specularmap, sp->v->uv, tsampler);
	}

	lightc = getscenecolor(sp->su->camera->scene, pos, n);
	sp->v->c = ZP3;

	for(l = sp->su->camera->scene->lights.next; l != &sp->su->camera->scene->lights; l = l->next){
		lightdir = normvec3(subpt3(l->p, pos));

		ambient = mulpt3(lightc, Ka);
		ambient = modulapt3(ambient, m.diffuse);

		Kd = max(0, _Xdotvec3(n, lightdir));
		diffuse = mulpt3(lightc, Kd);
		diffuse = modulapt3(diffuse, m.diffuse);

		lookdir = normvec3(subpt3(sp->su->camera->p, pos));
		lightdir = qrotate(lightdir, n, PI); /* reflect */
		spec = pow(max(0, _Xdotvec3(lookdir, lightdir)), m.shininess);
		specular = mulpt3(lightc, spec*Ks);
		specular = modulapt3(specular, m.specular);

		sp->v->c = addpt3(sp->v->c, addpt3(ambient, addpt3(diffuse, specular)));
	}
	sp->v->c.a = m.diffuse.a;

	n.w = 1;
	sp->toraster(sp, "normals", &n);

	specular.a = 1;
	sp->toraster(sp, "specular", &specular);

	return sp->v->c;
}

Color
blinnshader(Shaderparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Color ambient, diffuse, specular, lightc;
	Point3 pos, n, lightdir, lookdir;
	Material m;
	LightSource *l;
	RFrame3 TBN;
	Vertexattr *va;

	va = sp->getattr(sp, "pos");
	pos = va->p;

	va = sp->getattr(sp, "ambient");
	m.ambient = va != nil? va->p: (Point3){1,1,1,1};
	va = sp->getattr(sp, "diffuse");
	m.diffuse = va != nil? va->p: sp->v->c;
	va = sp->getattr(sp, "specular");
	m.specular = va != nil? va->p: (Point3){1,1,1,1};
	va = sp->getattr(sp, "shininess");
	m.shininess = va != nil? va->n: 1;

	n = sp->v->n;
	if(sp->v->mtl != nil && sp->v->uv.w != 0){
		/* normal mapping */
		if(sp->v->mtl->normalmap != nil){
			/* TODO implement this on the VS instead and apply Gram-Schmidt here */
			n = sampletexture(sp->v->mtl->normalmap, sp->v->uv, neartexsampler);
			n = normvec3(subpt3(mulpt3(n, 2), Vec3(1,1,1)));

			TBN.p = (Point3){0,0,0,1};
			TBN.bx = sp->v->tangent;		/* T */
			TBN.bz = sp->v->n;			/* N */
			TBN.by = crossvec3(TBN.bz, TBN.bx);	/* B */

			n = normvec3(invrframexform3(n, TBN));
		}

		if(sp->v->mtl->diffusemap != nil)
			m.diffuse = sampletexture(sp->v->mtl->diffusemap, sp->v->uv, tsampler);

		if(sp->v->mtl->specularmap != nil)
			m.specular = sampletexture(sp->v->mtl->specularmap, sp->v->uv, tsampler);
	}

	lightc = getscenecolor(sp->su->camera->scene, pos, n);
	sp->v->c = ZP3;

	for(l = sp->su->camera->scene->lights.next; l != &sp->su->camera->scene->lights; l = l->next){
		lightdir = normvec3(subpt3(l->p, pos));

		ambient = mulpt3(lightc, Ka);
		ambient = modulapt3(ambient, m.diffuse);

		Kd = max(0, _Xdotvec3(n, lightdir));
		diffuse = mulpt3(lightc, Kd);
		diffuse = modulapt3(diffuse, m.diffuse);

		lookdir = normvec3(subpt3(sp->su->camera->p, pos));
		lightdir = normvec3(addpt3(lookdir, lightdir)); /* half vector */
		spec = pow(max(0, _Xdotvec3(lookdir, lightdir)), m.shininess);
		specular = mulpt3(lightc, spec*Ks);
		specular = modulapt3(specular, m.specular);

		sp->v->c = addpt3(sp->v->c, addpt3(ambient, addpt3(diffuse, specular)));
	}
	sp->v->c.a = m.diffuse.a;

	n.w = 1;
	sp->toraster(sp, "normals", &n);

	specular.a = 1;
	sp->toraster(sp, "specular", &specular);

	return sp->v->c;
}

Point3
toonvshader(Shaderparams *sp)
{
	Point3 lightdir;
	LightSource *l;
	double intens;

	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	intens = 0;
	for(l = sp->su->camera->scene->lights.next; l != &sp->su->camera->scene->lights; l = l->next){
		lightdir = normvec3(subpt3(l->p, sp->v->p));
		intens += max(0, _Xdotvec3(sp->v->n, lightdir));
	}
	sp->setattr(sp, "intensity", VANumber, &intens);
	if(sp->v->mtl != nil)
		sp->v->c = sp->v->mtl->diffuse;
	return world2clip(sp->su->camera, sp->v->p);
}

Color
toonshader(Shaderparams *sp)
{
	Vertexattr *va;
	double intens;

	va = sp->getattr(sp, "intensity");
	intens = va->n;
	intens = intens > 0.85? 1:
		 intens > 0.60? 0.80:
		 intens > 0.45? 0.60:
		 intens > 0.30? 0.45:
		 intens > 0.15? 0.30: 0.15;

	sp->v->n.w = 1;
	sp->toraster(sp, "normals", &sp->v->n);

	return (Point3){intens, 0.6*intens, 0, 1};
}

Point3
identvshader(Shaderparams *sp)
{
	if(sp->v->mtl != nil)
		sp->v->c = sp->v->mtl->diffuse;
	return world2clip(sp->su->camera, model2world(sp->su->entity, sp->v->p));
}

Color
identshader(Shaderparams *sp)
{
	Color tc;

	if(sp->v->mtl != nil && sp->v->mtl->diffusemap != nil && sp->v->uv.w != 0)
		tc = sampletexture(sp->v->mtl->diffusemap, sp->v->uv, tsampler);
	else
		tc = (Color){1,1,1,1};

	sp->v->n.w = 1;
	sp->toraster(sp, "normals", &sp->v->n);

	return modulapt3(sp->v->c, tc);
}

Point3
ivshader(Shaderparams *sp)
{
	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	return world2clip(sp->su->camera, sp->v->p);
}

Color
triangleshader(Shaderparams *sp)
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
		return ZP3;

	bc = barycoords(t, Pt2(sp->p.x,sp->p.y,1));
	if(bc.x < 0 || bc.y < 0 || bc.z < 0)
		return ZP3;

	/* wireframe version */
	enum { THICKNESS = 8 };
	Point2 p01, p12, p20;
	double d01, d12, d20, d;
	Point2 p, v, r;

	p01 = subpt2(t.p1, t.p0);
	p12 = subpt2(t.p2, t.p1);
	p20 = subpt2(t.p0, t.p2);

	p = Pt2(sp->p.x, sp->p.y, 1);
	r = subpt2(t.p0, p);
	v = Vec2(p01.y, -p01.x);
	d01 = fabs(dotvec2(v, r));
	r = subpt2(t.p1, p);
	v = Vec2(p12.y, -p12.x);
	d12 = fabs(dotvec2(v, r));
	r = subpt2(t.p2, p);
	v = Vec2(p20.y, -p20.x);
	d20 = fabs(dotvec2(v, r));

	d = min(d01, min(d12, d20));
	if(d > THICKNESS)
		return ZP3;

	return Pt3(bc.x, bc.y, bc.z, 1);
}

Color
circleshader(Shaderparams *sp)
{
	Vertexattr *va;
	Point2 uv;
	double r, d, time;

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);

	va = sp->getuniform(sp, "time");
	time = va == nil? 0: va->n;

//	r = 0.3;
	r = 0.3*fabs(sin(time/1e9));
	d = vec2len(subpt2(uv, Vec2(0.5,0.5)));

	if(d > r + r*0.05 || d < r - r*0.05)
		return ZP3;

	return Pt3(uv.x, uv.y, 0, 1);
}

/* some shaping functions from The Book of Shaders, Chapter 5 */
Color
sfshader(Shaderparams *sp)
{
	Vertexattr *va;
	Point2 uv;
	double y, pct, time;

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);
	uv.y = 1 - uv.y;		/* make [0 0] the bottom-left corner */

	va = sp->getuniform(sp, "time");
	time = va == nil? 0: va->n;

//	y = step(0.5, uv.x);
//	y = pow(uv.x, 5);
//	y = sin(uv.x);
	y = sin(uv.x*time/1e8)/2.0 + 0.5;
//	y = smoothstep(0.1, 0.9, uv.x);
	pct = smoothstep(y-0.02, y, uv.y) - smoothstep(y, y+0.02, uv.y);

	return Pt3(flerp(y, 0, pct), flerp(y, 1, pct), flerp(y, 0, pct), 1);
}

Color
boxshader(Shaderparams *sp)
{
	Point2 uv, p;
	Point2 r;

	uv = Pt2(sp->p.x,sp->p.y,1);
	uv.x /= Dx(sp->su->fb->r);
	uv.y /= Dy(sp->su->fb->r);
	r = Vec2(0.2,0.4);

	p = Pt2(fabs(uv.x - 0.5), fabs(uv.y - 0.5), 1);
	p = subpt2(p, r);
	p.x = max(p.x, 0);
	p.y = max(p.y, 0);

	if(vec2len(p) > 0)
		return ZP3;

	return Pt3(uv.x, uv.y, smoothstep(0,1,uv.x+uv.y), 1);
}

Shadertab shadertab[] = {
	{ "triangle", ivshader, triangleshader },
	{ "circle", ivshader, circleshader },
	{ "box", ivshader, boxshader },
	{ "sf", ivshader, sfshader },
	{ "toon", toonvshader, toonshader },
	{ "ident", identvshader, identshader },
	{ "gouraud", gouraudvshader, gouraudshader },
	{ "phong", phongvshader, phongshader },
	{ "blinn", phongvshader, blinnshader },
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
