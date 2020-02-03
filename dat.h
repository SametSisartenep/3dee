enum {
	K↑,
	K↓,
	K←,
	K→,
	Krise,
	Kfall,
	KR↑,
	KR↓,
	KR←,
	KR→,
	KR↺,
	KR↻,
	Kcam0,
	Kcam1,
	Kcam2,
	Kcam3,
	Kscrshot,
	Ke
};

enum {
	Scamno,
	Sfov,
	Scampos,
	Scambx, Scamby, Scambz,
	Se
};

typedef struct Mesh Mesh;
typedef struct TTriangle3 TTriangle3;

struct Mesh {
	Triangle3 *tris;
	int ntri;
};

struct TTriangle3 {
	Triangle3;
	Image *tx;
};
