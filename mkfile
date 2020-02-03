</$objtype/mkfile

BIN=$home/$objtype/bin
TARG=3d
OFILES=\
	main.$O\

HFILES=geometry.h graphics.h obj.h dat.h fns.h

LIB=\
	libgeometry.a$O\
	libgraphics.a$O\
	libobj.a$O

</sys/src/cmd/mkone

libgeometry.a$O:
	cd libgeometry
	mk install

libgraphics.a$O:
	cd libgraphics
	mk install

libobj.a$O:
	cd libobj
	mk install

clean nuke:V:
	rm -f *.[$OS] [$OS].out y.tab.? y.debug y.output $TARG
	@{cd libgeometry; mk $target}
	@{cd libgraphics; mk $target}
	@{cd libobj; mk $target}
