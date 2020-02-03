.TL
libgraphics
.AU
Rodrigo G. López
.sp
rgl@antares-labs.eu
.AI
Antares Telecom Laboratories
Albatera, Alicante
.FS
ACHTUNG! this is a
.B "WORK IN PROGRESS"
.FE
.NH 1
Data Structures
.NH 2
Camera
.P1
struct Camera {
	RFrame3;		/* VCS */
	Image *viewport;
	double fov;		/* vertical FOV */
	struct {
		double n, f;	/* near and far clipping planes */
	} clip;
	Projection ptype;
	Matrix3 proj;		/* VCS to viewport xform */
};
.P2
.PP
A camera is an image capturing entity, analog to the real world device
we all know, that allows us to see the virtual 3-D world by projecting
it into a viewport we can attach to a screen or window for real-time
visualization or write out into a file.
