typedef struct	Camcfg Camcfg;
typedef struct	Stats Stats;

struct Camcfg
{
	Point3	p;
	Point3	lookat;
	Point3	up;
	double	fov;
	double	clipn;
	double	clipf;
	int	ptype;
};

struct Stats
{
	uvlong	min, avg, max, acc, n, v;
	uvlong	nframes;
};
