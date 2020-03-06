typedef enum {
	Portho,		/* orthographic */
	Ppersp		/* perspective */
} Projection;

typedef struct Vertex Vertex;
typedef struct Viewport Viewport;
typedef struct Camera Camera;

struct Vertex {
	Point3 p;	/* position */
	Point3 n;	/* surface normal */
};

struct Viewport
{
	RFrame;
	Memimage *fb;
};

struct Camera {
	RFrame3;		/* VCS */
	Viewport viewport;
	double fov;		/* vertical FOV */
	double clipn;
	double clipf;
	Matrix3 proj;		/* VCS to NDC xform */
	Projection ptype;

	void (*updatefb)(Camera*, Rectangle, ulong);
};

/* Camera */
Camera *alloccamera(Rectangle, ulong);
void configcamera(Camera*, Image*, double, double, double, Projection);
void placecamera(Camera*, Point3, Point3, Point3);
void aimcamera(Camera*, Point3);
void reloadcamera(Camera*);

/* rendering */
#define FPS2MS(n)		(1000/(n))
#define WORLD2VCS(cp, p)	(rframexform3((p), *(cp)))
#define VCS2NDC(cp, p)		(xform3((p), (cp)->proj))
#define WORLD2NDC(cp, p)	(VCS2NDC((cp), WORLD2VCS((cp), (p))))
int isclipping(Point3);
Point toviewport(Camera*, Point3);
Point2 fromviewport(Camera*, Point);
void perspective(Matrix3, double, double, double, double);
void orthographic(Matrix3, double, double, double, double, double, double);
void line3(Camera*, Point3, Point3, int, int, Image*);
Point string3(Camera*, Point3, Image*, Font*, char*);
