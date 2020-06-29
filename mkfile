</$objtype/mkfile

BIN=$home/$objtype/bin
TARG=3d
OFILES=\
	alloc.$O\
	main.$O\

HFILES=libgeometry/geometry.h\
	libgraphics/graphics.h\
	libobj/obj.h\
	dat.h\
	fns.h\

LIB=\
	libobj/libobj.a$O\
	libgraphics/libgraphics.a$O\
	libgeometry/libgeometry.a$O\

CFLAGS=$CFLAGS -I. -Ilibgeometry -Ilibgraphics -Ilibobj

</sys/src/cmd/mkone

libgeometry/libgeometry.a$O:
	cd libgeometry
	mk install

libgraphics/libgraphics.a$O:
	cd libgraphics
	mk install

libobj/libobj.a$O:
	cd libobj
	mk install

clean nuke:V:
	rm -f *.[$OS] [$OS].out $TARG
	@{cd libgeometry; mk $target}
	@{cd libgraphics; mk $target}
	@{cd libobj; mk $target}
