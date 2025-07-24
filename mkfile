</$objtype/mkfile

BIN=$home/bin/$objtype
TARG=\
	vis\
	med\
	solar\
	projtest\
	procgen\
	obj\
	toobj\
	stl\
	tostl\
	plot3\
	raymarch\

OFILES=\
	alloc.$O\
	qball.$O\

HFILES=dat.h fns.h

LIB=\
	libgraphics/libgraphics.a$O\
	libobj/libobj.a$O\
	libstl/libstl.a$O\

</sys/src/cmd/mkmany

libgraphics/libgraphics.a$O:
	cd libgraphics
	mk install

libobj/libobj.a$O:
	cd libobj
	mk install

libstl/libstl.a$O:
	cd libstl
	mk install

nuke∅dirs:VQ:
	for(d in libgeometry libobj libgraphics)
		rm -rf $d

pulldeps:VQ: nuke∅dirs
	git/clone git://antares-labs.eu/libgraphics || \
	git/clone git://shithub.us/rodri/libgraphics || \
	git/clone https://github.com/sametsisartenep/libgraphics
	git/clone git://antares-labs.eu/libobj || \
	git/clone git://shithub.us/rodri/libobj || \
	git/clone https://github.com/sametsisartenep/libobj
	git/clone git://antares-labs.eu/libstl || \
	git/clone git://shithub.us/rodri/libstl || \
	git/clone https://github.com/sametsisartenep/libstl

clean nuke:V:
	rm -f *.[$OS] [$OS].out [$OS].^$TARG
	@{cd libgraphics; mk $target}
	@{cd libobj; mk $target}

uninstall:V:
	rm -f $BIN/^$TARG
