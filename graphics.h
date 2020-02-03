typedef enum {
	Portho,		/* orthographic */
	Ppersp		/* perspective */
} Projection;

typedef struct Vertex Vertex;
typedef struct Triangle Triangle;
typedef struct Viewport Viewport;
typedef struct Camera Camera;

struct Vertex {
	Point3 p;	/* position */
	Point3 n;	/* surface normal */
	//Image tx;	/* (?) */
};

struct Triangle {
	Point p0, p1, p2;
};

struct Camera {
	RFrame3;		/* VCS */
	Image *viewport;
	double fov;		/* vertical FOV */
	struct {
		double n, f;	/* near and far clipping planes */
	} clip;
	Matrix3 proj;		/* VCS to NDC xform */
	Projection ptype;
};

/* Triangle */
Triangle Trian(int, int, int, int, int, int);
Triangle Trianpt(Point, Point, Point);
void triangle(Image *, Triangle, int, Image *, Point);
void filltriangle(Image *, Triangle, Image *, Point);

/* Camera */
void perspective(Matrix3, double, double, double, double);
void orthographic(Matrix3, double, double, double, double, double, double);
void configcamera(Camera*, Image*, double, double, double, Projection);
void placecamera(Camera*, Point3, Point3, Point3);
void aimcamera(Camera*, Point3);
void reloadcamera(Camera*);

/* rendering */
#define FPS2MS(n)	(1000/(n))
#define WORLD2VCS(cp, p)	(rframexform3((p), *(cp)))
#define VCS2NDC(cp, p)	(xform3((p), (cp)->proj))
#define WORLD2NDC(cp, p)	(VCS2NDC((cp), WORLD2VCS((cp), (p))))
int isclipping(Point3);
Point toviewport(Camera*, Point3);
Point2 fromviewport(Camera*, Point);
void line3(Camera *c, Point3 p0, Point3 p1, int end0, int end1, Image *src);
Point string3(Camera *c, Point3 p, Image *src, Font *f, char *s);
