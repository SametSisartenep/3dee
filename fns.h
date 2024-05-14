void *emalloc(ulong);
void *erealloc(void*, ulong);
Image *eallocimage(Display*, Rectangle, ulong, int, ulong);
Memimage *eallocmemimage(Rectangle, ulong);
void qb(Rectangle, Point, Point, Quaternion*, Quaternion*);
