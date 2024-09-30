
Point3
gouraudvshader(Shaderparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Point3 pos, lightdir, lookdir;
	Material m;
	Color ambient, diffuse, specular, lightc;

	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	pos = sp->v->p;

	if(sp->v->mtl != nil)
		m = *sp->v->mtl;
	else{
		memset(&m, 0, sizeof m);
		m.diffuse = sp->v->c;
		m.specular = Pt3(1,1,1,1);
		m.shininess = 1;
	}

	lightdir = normvec3(subpt3(light.p, pos));
	lightc = getlightcolor(&light, lightdir);

	ambient = mulpt3(lightc, Ka);
	ambient = modulapt3(ambient, m.diffuse);

	Kd = max(0, dotvec3(sp->v->n, lightdir));
	diffuse = mulpt3(lightc, Kd);
	diffuse = modulapt3(diffuse, m.diffuse);

	lookdir = normvec3(subpt3(sp->su->camera->p, pos));
	lightdir = qrotate(lightdir, sp->v->n, PI);
	spec = pow(max(0, dotvec3(lookdir, lightdir)), m.shininess);
	specular = mulpt3(lightc, spec*Ks);
	specular = modulapt3(specular, m.specular);

	sp->v->c = addpt3(ambient, addpt3(diffuse, specular));
	sp->v->c.a = m.diffuse.a;
	return world2clip(sp->su->camera, pos);
}

Color
gouraudshader(Shaderparams *sp)
{
	Color tc;

	if(sp->su->entity->mdl->tex != nil && sp->v->uv.w != 0)
		tc = sampletexture(sp->su->entity->mdl->tex, sp->v->uv, tsampler);
	else if(sp->v->mtl != nil && sp->v->mtl->diffusemap != nil && sp->v->uv.w != 0)
		tc = sampletexture(sp->v->mtl->diffusemap, sp->v->uv, tsampler);
	else
		tc = Pt3(1,1,1,1);

	sp->v->n.w = 1;
	sp->toraster(sp, "normals", &sp->v->n);

	return modulapt3(sp->v->c, tc);
}

Point3
phongvshader(Shaderparams *sp)
{
	Point3 pos;
	Color a, d, s;
	double ss;

	sp->v->n = model2world(sp->su->entity, sp->v->n);
	sp->v->p = model2world(sp->su->entity, sp->v->p);
	pos = sp->v->p;
	sp->setattr(sp, "pos", VAPoint, &pos);
	if(sp->v->mtl != nil && sp->v->mtl->normalmap != nil && sp->v->uv.w != 0){
		sp->v->tangent = model2world(sp->su->entity, sp->v->tangent);
		sp->setattr(sp, "tangent", VAPoint, &sp->v->tangent);
	}
	if(sp->v->mtl != nil){
		a = sp->v->mtl->ambient;
		d = sp->v->mtl->diffuse;
		s = sp->v->mtl->specular;
		ss = sp->v->mtl->shininess;
		sp->setattr(sp, "ambient", VAPoint, &a);
		sp->setattr(sp, "diffuse", VAPoint, &d);
		sp->setattr(sp, "specular", VAPoint, &s);
		sp->setattr(sp, "shininess", VANumber, &ss);
	}
	return world2clip(sp->su->camera, pos);
}

Color
phongshader(Shaderparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Color ambient, diffuse, specular, lightc, c;
	Point3 pos, n, lightdir, lookdir;
	Material m;
	RFrame3 TBN;
	Vertexattr *va;

	va = sp->getattr(sp, "pos");
	pos = va->p;
	
	va = sp->getattr(sp, "ambient");
	m.ambient = va != nil? va->p: Pt3(1,1,1,1);
	va = sp->getattr(sp, "diffuse");
	m.diffuse = va != nil? va->p: sp->v->c;
	va = sp->getattr(sp, "specular");
	m.specular = va != nil? va->p: Pt3(1,1,1,1);
	va = sp->getattr(sp, "shininess");
	m.shininess = va != nil? va->n: 1;

	lightdir = normvec3(subpt3(light.p, pos));
	lightc = getlightcolor(&light, lightdir);

	/* normal mapping */
	va = sp->getattr(sp, "tangent");
	if(va == nil)
		n = sp->v->n;
	else{
		/* TODO implement this on the VS instead and apply Gram-Schmidt here */
		n = sampletexture(sp->v->mtl->normalmap, sp->v->uv, neartexsampler);
		n = normvec3(subpt3(mulpt3(n, 2), Vec3(1,1,1)));

		TBN.p = Pt3(0,0,0,1);
		TBN.bx = va->p;				/* T */
		TBN.bz = sp->v->n;			/* N */
		TBN.by = crossvec3(TBN.bz, TBN.bx);	/* B */

		n = normvec3(invrframexform3(n, TBN));
		sp->v->n = n;
	}

	if(sp->su->entity->mdl->tex != nil && sp->v->uv.w != 0)
		m.diffuse = sampletexture(sp->su->entity->mdl->tex, sp->v->uv, tsampler);
	else if(sp->v->mtl != nil && sp->v->mtl->diffusemap != nil && sp->v->uv.w != 0)
		m.diffuse = sampletexture(sp->v->mtl->diffusemap, sp->v->uv, tsampler);

	ambient = mulpt3(lightc, Ka);
	ambient = modulapt3(ambient, m.diffuse);

	Kd = max(0, dotvec3(n, lightdir));
	diffuse = mulpt3(lightc, Kd);
	diffuse = modulapt3(diffuse, m.diffuse);

	if(sp->v->mtl != nil && sp->v->mtl->specularmap != nil && sp->v->uv.w != 0)
		m.specular = sampletexture(sp->v->mtl->specularmap, sp->v->uv, tsampler);

	lookdir = normvec3(subpt3(sp->su->camera->p, pos));
	lightdir = qrotate(lightdir, n, PI);
	spec = pow(max(0, dotvec3(lookdir, lightdir)), m.shininess);
	specular = mulpt3(lightc, spec*Ks);
	specular = modulapt3(specular, m.specular);

	sp->v->n.w = 1;
	sp->toraster(sp, "normals", &sp->v->n);

	c = addpt3(ambient, addpt3(diffuse, specular));
	c.a = m.diffuse.a;

	specular.a = 1;
	sp->toraster(sp, "specular", &specular);

	return c;
}

Color
blinnshader(Shaderparams *sp)
{
	static double Ka = 0.1;	/* ambient factor */
	static double Ks = 0.5;	/* specular factor */
	double Kd;		/* diffuse factor */
	double spec;
	Color ambient, diffuse, specular, lightc, c;
	Point3 pos, n, lightdir, lookdir;
	Material m;
	RFrame3 TBN;
	Vertexattr *va;

	va = sp->getattr(sp, "pos");
	pos = va->p;
	
	va = sp->getattr(sp, "ambient");
	m.ambient = va != nil? va->p: Pt3(1,1,1,1);
	va = sp->getattr(sp, "diffuse");
	m.diffuse = va != nil? va->p: sp->v->c;
	va = sp->getattr(sp, "specular");
	m.specular = va != nil? va->p: Pt3(1,1,1,1);
	va = sp->getattr(sp, "shininess");
	m.shininess = va != nil? va->n: 1;

	lightdir = normvec3(subpt3(light.p, pos));
	lightc = getlightcolor(&light, lightdir);

	/* normal mapping */
	va = sp->getattr(sp, "tangent");
	if(va == nil)
		n = sp->v->n;
	else{
		/* TODO implement this on the VS instead and apply Gram-Schmidt here */
		n = sampletexture(sp->v->mtl->normalmap, sp->v->uv, neartexsampler);
		n = normvec3(subpt3(mulpt3(n, 2), Vec3(1,1,1)));

		TBN.p = Pt3(0,0,0,1);
		TBN.bx = va->p;				/* T */
		TBN.bz = sp->v->n;			/* N */
		TBN.by = crossvec3(TBN.bz, TBN.bx);	/* B */

		n = normvec3(invrframexform3(n, TBN));
		sp->v->n = n;
	}

	if(sp->su->entity->mdl->tex != nil && sp->v->uv.w != 0)
		m.diffuse = sampletexture(sp->su->entity->mdl->tex, sp->v->uv, tsampler);
	else if(sp->v->mtl != nil && sp->v->mtl->diffusemap != nil && sp->v->uv.w != 0)
		m.diffuse = sampletexture(sp->v->mtl->diffusemap, sp->v->uv, tsampler);

	ambient = mulpt3(lightc, Ka);
	ambient = modulapt3(ambient, m.diffuse);

	Kd = max(0, dotvec3(n, lightdir));
	diffuse = mulpt3(lightc, Kd);
	diffuse = modulapt3(diffuse, m.diffuse);

	if(sp->v->mtl != nil && sp->v->mtl->specularmap != nil && sp->v->uv.w != 0)
		m.specular = sampletexture(sp->v->mtl->specularmap, sp->v->uv, tsampler);

	lookdir = normvec3(subpt3(sp->su->camera->p, pos));
	lightdir = normvec3(addpt3(lookdir, lightdir));	/* half vector */
	spec = pow(max(0, dotvec3(n, lightdir)), m.shininess);
	specular = mulpt3(lightc, spec*Ks);
	specular = modulapt3(specular, m.specular);

	sp->v->n.w = 1;
	sp->toraster(sp, "normals", &sp->v->n);

	c = addpt3(ambient, addpt3(diffuse, specular));
	c.a = m.diffuse.a;

	specular.a = 1;
	sp->toraster(sp, "specular", &specular);

	return c;
}

Point3
toonvshader(Shaderparams *sp)
{
	Point3 pos, lightdir;
	double intens;

	sp->v->n = model2world(sp->su->entity, sp->v->n);
	pos = model2world(sp->su->entity, sp->v->p);
	lightdir = normvec3(subpt3(light.p, pos));
	intens = max(0, dotvec3(sp->v->n, lightdir));
	sp->setattr(sp, "intensity", VANumber, &intens);
	if(sp->v->mtl != nil)
		sp->v->c = sp->v->mtl->diffuse;
	return world2clip(sp->su->camera, pos);
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

	return Pt3(intens, 0.6*intens, 0, 1);
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

	if(sp->su->entity->mdl->tex != nil && sp->v->uv.w != 0)
		tc = sampletexture(sp->su->entity->mdl->tex, sp->v->uv, tsampler);
	else if(sp->v->mtl != nil && sp->v->mtl->diffusemap != nil && sp->v->uv.w != 0)
		tc = sampletexture(sp->v->mtl->diffusemap, sp->v->uv, tsampler);
	else
		tc = Pt3(1,1,1,1);

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
		return Vec3(0,0,0);

	bc = barycoords(t, Pt2(sp->p.x,sp->p.y,1));
	if(bc.x < 0 || bc.y < 0 || bc.z < 0)
		return Vec3(0,0,0);

	return Pt3(bc.x, bc.y, bc.z, 1);
}

Color
circleshader(Shaderparams *sp)
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
sfshader(Shaderparams *sp)
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
		return Vec3(0,0,0);

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
