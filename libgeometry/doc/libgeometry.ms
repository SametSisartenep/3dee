.TL
libgeometry
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
.SH
Introduction
.PP
.I Libgeometry is a computational geometry library that provides all
the utilities anybody working with graphics or scientific simulations
could need.
.NH 1
Data Structures
.NH 2
Point2
.P1
struct Point2 {
	double x, y, w;
};
.P2
.PP
.I Point2
represents a point in two-dimensional projective space, which itself
is an extension of the two-dimensional euclidean space that allows us
to work with vectors and compose affine transformations in a friendly
manner.  A point
.EQ
gfont roman
(x, y, w)
.EN
made out of homogenous coordinates
.I x ,
.I y ,
and
.I w ,
yields a point with cartesian coordinates
.EQ
(x/w, y/w) .
.EN
.NH 2
Point3
.P1
struct Point3 {
	double x, y, z, w;
};
.P2
.PP
.I Point3
is a point in three-dimensional projective space.
.NH 2
Matrix
.P1
typedef double Matrix[3][3];
.P2
.PP
.I Matrix
represents a 3x3 matrix, thought to compose affine transformations to
apply to homogeneous 2D points.
.NH 2
Matrix3
.P1
typedef double Matrix3[4][4];
.P2
.PP
.I Matrix3
represents a 4x4 matrix, thought to compose affine transformations to
apply to homogeneous 3D points.
.NH 2
Quaternion
.P1
struct Quaternion {
	double r, i, j, k;
};
.P2
.PP
.I Quaternions
are a numbering system that extends the complex numbers up to
four-dimensional space, and are used to apply rotations and model
mechanical interactions in 3D space.  Their main advantages with
respect to their matrix relatives are increased computational and
storage performance and gimbal lock avoidance.
.NH 2
RFrame
.P1
struct RFrame {
	Point2 p;
	Point2 bx, by;
};
.P2
.PP
A reference frame (or frame of reference) is
.NH 2
RFrame3
.P1
struct RFrame3 {
	Point3 p;
	Point3 bx, by, bz;
};
.P2
.PP
A reference frame (or frame of reference) is
.NH 1
Algorithms
.NH 2
Point2
.SH
Addition
.P1
Point2 addpt2(Point2 a, Point2 b)
.P2
.EQ
a + b ~=~ left ( x sub a + x sub b ,~ y sub a + y sub b ,~ w sub a + w sub b right )
.EN
.SH
Substraction
.P1
Point2 subpt2(Point2 a, Point2 b)
.P2
.EQ
a - b ~=~ left ( x sub a - x sub b ,~ y sub a - y sub b ,~ w sub a - w sub b right )
.EN
.SH
Multiplication
.P1
Point2 mulpt2(Point2 p, double s)
.P2
.EQ
p * s ~=~ left ( xs,~ ys,~ ws right )
.EN
.SH
Division
.P1
Point2 divpt2(Point2 p, double s)
.P2
.EQ
p / s ~=~ left ( x over s ,~ y over s ,~ w over s right )
.EN
.SH
Vector Dot Product
.P1
double dotvec2(Point2 a, Point2 b)
.P2
.EQ
a vec ~•~ b vec ~=~ x sub a x sub b + y sub a y sub b
.EN
.SH
Vector Magnitude/Length
.P1
double vec2len(Point2 v)
.P2
.EQ
| v vec | ~=~ sqrt { x sup 2 + y sup 2 }
.EN
.SH
Vector Normalization
.P1
Point2 normvec2(Point2 v)
.P2
.EQ
n vec ~=~ left ( x over {| v vec |},~ y over {| v vec |} right )
.EN
.NH 2
Point3
.SH
Addition
.P1
Point3 addpt3(Point3 a, Point3 b)
.P2
.EQ
a + b ~=~ left ( x sub a + x sub b ,~ y sub a + y sub b ,~ z sub a + z sub b ,~ w sub a + w sub b right )
.EN
.SH
Substraction
.P1
Point3 subpt3(Point3 a, Point3 b)
.P2
.EQ
a - b ~=~ left ( x sub a - x sub b ,~ y sub a - y sub b ,~ z sub a - z sub b ,~ w sub a - w sub b right )
.EN
.SH
Multiplication
.P1
Point3 mulpt3(Point3 p, double s)
.P2
.EQ
p * s ~=~ left ( xs,~ ys,~ zs,~ ws right )
.EN
.SH
Division
.P1
Point3 divpt3(Point3 p, double s)
.P2
.EQ
p / s ~=~ left ( x over s ,~ y over s ,~ z over s ,~ w over s right )
.EN
.SH
Vector Dot Product
.P1
double dotvec3(Point3 a, Point3 b)
.P2
.EQ
a vec ~•~ b vec ~=~ x sub a x sub b + y sub a y sub b + z sub a z sub b
.EN
.SH
Vector Cross Product
.P1
double crossvec3(Point3 a, Point3 b)
.P2
.EQ
a vec ~×~ b vec ~=~ left ( y sub a z sub b - z sub a y sub b ,~
			 z sub a x sub b - x sub a z sub b ,~
			 x sub a y sub b - y sub a x sub b right )
.EN
.SH
Vector Magnitude/Length
.P1
double vec3len(Point3 v)
.P2
.EQ
| v vec | ~=~ sqrt { x sup 2 + y sup 2 + z sup 2 }
.EN
.SH
Vector Normalization
.P1
Point3 normvec3(Point3 v)
.P2
.EQ
n vec ~=~ left ( x over {| v vec |},~ y over {| v vec |},~ z over {| v vec |} right )
.EN
.NH 2
Matrix
.SH
Addition
.P1
void addm(Matrix A, Matrix B)
.P2
.EQ
( bold A + bold B ) sub {i,j} ~=~ bold A sub {i,j} + bold B sub {i,j}
.EN
.SH
Substraction
.P1
void subm(Matrix A, Matrix B)
.P2
.EQ
( bold A - bold B ) sub {i,j} ~=~ bold A sub {i,j} - bold B sub {i,j}
.EN
.SH
Multiplication
.P1
void mulm(Matrix A, Matrix B)
.P2
.EQ
left [ bold A bold B right ] sub {i,j} ~=~ sum from {k = 0} to 3-1 bold A sub {i,k} bold B sub {k,j}
.EN
.SH
Transpose
.P1
void transposem(Matrix M)
.P2
.EQ
( bold M sup T ) sub {i,j} ~=~ bold A sub {j,i}
.EN
.SH
Identity
.P1
void identity(Matrix M)
.P2
.EQ
bold M ~=~ left [ rpile {
	1 ~ 0 ~ 0
above	0 ~ 1 ~ 0
above	0 ~ 0 ~ 1
} right ]
.EN
.SH
Determinant
.P1
double detm(Matrix M)
.P2
.EQ
det( bold M ) ~=~ lpile {
			m sub 00 ( m sub 11 m sub 22 - m sub 12 m sub 21 ) +
above			m sub 01 ( m sub 12 m sub 20 - m sub 10 m sub 22 ) +
above			m sub 02 ( m sub 10 m sub 21 - m sub 11 m sub 20 )
}
.EN
.NH 2
Matrix3
.SH
Addition
.P1
void addm3(Matrix3 A, Matrix3 B)
.P2
.EQ
( bold A + bold B ) sub {i,j} ~=~ bold A sub {i,j} + bold B sub {i,j}
.EN
.SH
Substraction
.P1
void subm3(Matrix3 A, Matrix3 B)
.P2
.EQ
( bold A - bold B ) sub {i,j} ~=~ bold A sub {i,j} - bold B sub {i,j}
.EN
.SH
Multiplication
.P1
void mulm3(Matrix3 A, Matrix3 B)
.P2
.EQ
left [ bold A bold B right ] sub {i,j} ~=~ sum from {k = 0} to 4-1 bold A sub {i,k} bold B sub {k,j}
.EN
.SH
Transpose
.P1
void transposem3(Matrix3 M)
.P2
.EQ
( bold M sup T ) sub {i,j} ~=~ bold A sub {j,i}
.EN
.SH
Identity
.P1
void identity3(Matrix3 M)
.P2
.EQ
bold M ~=~ left [ rpile {
	1 ~ 0 ~ 0 ~ 0
above	0 ~ 1 ~ 0 ~ 0
above	0 ~ 0 ~ 1 ~ 0
above	0 ~ 0 ~ 0 ~ 1
} right ]
.EN
.SH
Determinant
.P1
double detm3(Matrix3 M)
.P2
.EQ
det( bold M ) ~=~ rpile {
		  m sub 00 ( m sub 11 ( m sub 22 m sub 33 - m sub 23 m sub 32 ) +
			     m sub 12 ( m sub 23 m sub 31 - m sub 21 m sub 33 ) +
			     m sub 13 ( m sub 21 m sub 32 - m sub 22 m sub 31 ) )
above		 -m sub 01 ( m sub 10 ( m sub 22 m sub 33 - m sub 23 m sub 32 ) +
			     m sub 12 ( m sub 23 m sub 30 - m sub 20 m sub 33 ) +
			     m sub 13 ( m sub 20 m sub 32 - m sub 22 m sub 30 ) )
above		 +m sub 02 ( m sub 10 ( m sub 21 m sub 33 - m sub 23 m sub 31 ) +
			     m sub 11 ( m sub 23 m sub 30 - m sub 20 m sub 33 ) +
			     m sub 13 ( m sub 20 m sub 31 - m sub 21 m sub 30 ) )
above		 -m sub 03 ( m sub 10 ( m sub 21 m sub 32 - m sub 22 m sub 31 ) +
			     m sub 11 ( m sub 22 m sub 30 - m sub 20 m sub 32 ) +
			     m sub 12 ( m sub 20 m sub 31 - m sub 21 m sub 30 ) )
}
.EN
.NH 2
Quaternion
.SH
Addition
.P1
Quaternion addq(Quaternion q, Quaternion r)
.P2
.EQ
q + r ~=~ ( r sub q + r sub r ,~ i sub q + i sub r ,~ j sub q + j sub r ,~ k sub q + k sub r )
.EN
.SH
Substraction
.P1
Quaternion subq(Quaternion q, Quaternion r)
.P2
.EQ
q - r ~=~ ( r sub q - r sub r ,~ i sub q - i sub r ,~ j sub q - j sub r ,~ k sub q - k sub r )
.EN
.SH
Multiplication
.P1
Quaternion mulq(Quaternion q, Quaternion r)
.P2
.EQ
q ~=~ [ r sub q ,~ v vec sub q ]
r ~=~ [ r sub r ,~ v vec sub r ]
qr ~=~ [ r sub q r sub r - v vec sub q • v vec sub r ,~ v vec sub r r sub q + v vec sub q r sub r + v vec sub q X v vec sub r ]
.EN
.SH
Scalar Multiplication
.P1
Quaternion smulq(Quaternion q, double s)
.P2
.EQ
qs ~=~ [ r sub q s ,~ i sub q s ,~ j sub q s ,~ k sub q s ]
.EN
.SH
Inverse
.P1
Quaternion invq(Quaternion q)
.P2
.EQ
q sup -1 ~=~ left ( r over {| q | sup 2} ,~ -i over {| q | sup 2} ,~ -j over {| q | sup 2} ,~ -k over {| q | sup 2} right )
.EN
.SH
Magnitude/Length
.P1
double qlen(Quaternion q)
.P2
.EQ
| q | ~=~ sqrt { r sup 2 + i sup 2 + j sup 2 + k sup 2 }
.EN
.NH 2
RFrame
.SH
Change of reference
.P1
Point2 rframexform(Point2 p, RFrame rf)
.P2
.NH 2
RFrame3
.SH
Change of reference
.P1
Point3 rframexform3(Point3 p, RFrame3 rf)
.P2
