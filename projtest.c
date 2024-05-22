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
#include "fns.h"

Camera cam;

void
usage(void)
{
	fprint(2, "usage: %s\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Point3 np, fp;
	Framebuf *fb;

	GEOMfmtinstall();
	ARGBEGIN{
	default: usage();
	}ARGEND
	if(argc != 0)
		usage();

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");

	placecamera(&cam, Pt3(0,0,1,1), Vec3(0,0,0), Vec3(0,1,0));
	configcamera(&cam, mkviewport(Rect(0,0,640,480)), 40*DEG, 0.01, 10, PERSPECTIVE);

	fb = cam.vp->getfb(cam.vp);
	np = Pt3(0,0,-0.01,1);
	fp = Pt3(0,0,-10,1);
	fprint(2, "near %V\nfar %V\n", np, fp);
	np = vcs2clip(&cam, np);
	fp = vcs2clip(&cam, fp);
	fprint(2, "E → C\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);
	np = clip2ndc(np);
	fp = clip2ndc(fp);
	fprint(2, "C → N\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);
	np = ndc2viewport(fb, np);
	fp = ndc2viewport(fb, fp);
	fprint(2, "N → V\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);
	np = viewport2ndc(fb, np);
	fp = viewport2ndc(fb, fp);
	fprint(2, "V → N\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);
	np = ndc2vcs(&cam, np);
	fp = ndc2vcs(&cam, fp);
	fprint(2, "N → E\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);

	fprint(2, "\n");
	
	np = Pt3(Dx(fb->r)/2.0,Dy(fb->r)/2.0,1,1);
	fp = Pt3(Dx(fb->r)/2.0,Dy(fb->r)/2.0,0,1);
	fprint(2, "near %V\nfar %V\n", np, fp);
	np = viewport2ndc(fb, np);
	fp = viewport2ndc(fb, fp);
	fprint(2, "V → N\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);
	np = ndc2vcs(&cam, np);
	fp = ndc2vcs(&cam, fp);
	fprint(2, "N → E\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);
	np = vcs2clip(&cam, np);
	fp = vcs2clip(&cam, fp);
	fprint(2, "E → C\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);
	np = clip2ndc(np);
	fp = clip2ndc(fp);
	fprint(2, "C → N\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);
	np = ndc2viewport(fb, np);
	fp = ndc2viewport(fb, fp);
	fprint(2, "N → V\n");
	fprint(2, "near %V\n", np);
	fprint(2, "far %V\n", fp);

	exits(nil);
}
