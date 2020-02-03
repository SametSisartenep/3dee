#include <u.h>
#include <libc.h>
#include "../../obj.h"

static char fd0[] = "/fd/0";

void
usage(void)
{
	fprint(2, "usage: %s [file]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	OBJ *obj;
	char *f;

	f = fd0;
	OBJfmtinstall();
	ARGBEGIN{
	default: usage();
	}ARGEND;
	if(argc > 1)
		usage();
	if(argc == 1)
		f = argv[0];
	obj = objparse(f);
	if(obj == nil)
		sysfatal("objparse: %r");
	print("%O\n", obj);
	objfree(obj);
	exits(0);
}
