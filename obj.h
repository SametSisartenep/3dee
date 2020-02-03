/* vertex types */
enum {
	OBJVGeometric,
	OBJVTexture,
	OBJVNormal,
	OBJVParametric,
	OBJNVERT
};
/* element types */
enum {
	OBJEPoint,
	OBJELine,
	OBJEFace,
	OBJECurve,
	OBJECurve2,
	OBJESurface
};
/* grouping types */
enum {
	OBJGGlobal,
	OBJGSmoothing,
	OBJGMerging
};
/* object hash table size */
enum {
	OBJHTSIZE = 17
};

typedef struct OBJVertex OBJVertex;
typedef struct OBJVertexArray OBJVertexArray;
typedef struct OBJElem OBJElem;
//typedef struct OBJGroup OBJGroup;
typedef struct OBJObject OBJObject;
typedef struct OBJ OBJ;

#pragma varargck type "O" OBJ*

struct OBJVertex
{
	union {
		struct { double x, y, z, w; };	/* geometric */
		struct { double u, v, vv; };	/* texture and parametric */
		struct { double i, j, k; };	/* normal */
	};
};

struct OBJVertexArray
{
	OBJVertex *verts;
	int nvert;
};

struct OBJElem
{
	int *indices;
	int nindex;
	int type;
	OBJElem *next;
};

//struct OBJGroup
//{
//	char *name;
//	int type;
//	OBJElem *elem0;
//	OBJGroup *next;
//};
//struct OBJObject
//{
//	char *name;
//	OBJGroup *grptab[OBJHTSIZE];
//	OBJObject *next;
//};

struct OBJObject
{
	char *name;
	OBJElem *child;
	OBJObject *next;
};

struct OBJ
{
	OBJVertexArray vertdata[OBJNVERT];
	OBJObject *objtab[OBJHTSIZE];
};

OBJ *objparse(char*);
void objfree(OBJ*);

int OBJfmt(Fmt*);
void OBJfmtinstall(void);
