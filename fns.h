void *emalloc(ulong);
void *erealloc(void*, ulong);
char *estrdup(char*);
Image *eallocimage(Display*, Rectangle, ulong, int, ulong);
Memimage *eallocmemimage(Rectangle, ulong);
void qball(Rectangle, Point, Point, Quaternion*, Quaternion*);
