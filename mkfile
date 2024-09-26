</$objtype/mkfile

BIN=$home/bin/$objtype
TARG=\
	vis\
	med\
	solar\
	projtest\
	procgen\
	obj\

OFILES=\
	alloc.$O\
	qball.$O\

HFILES=dat.h fns.h

LIB=\
	libgraphics/libgraphics.a$O\
	libobj/libobj.a$O\

</sys/src/cmd/mkmany

libgraphics/libgraphics.a$O:
	cd libgraphics
	mk install

libobj/libobj.a$O:
	cd libobj
	mk install

nuke∅dirs:VQ:
	for(d in libgeometry libobj libgraphics)
		rm -rf $d

pulldeps:VQ: nuke∅dirs
	git/clone git://antares-labs.eu/libobj || \
	git/clone git://shithub.us/rodri/libobj || \
	git/clone https://github.com/sametsisartenep/libobj
	git/clone git://antares-labs.eu/libgraphics || \
	git/clone https://github.com/sametsisartenep/libgraphics

clean nuke:V:
	rm -f *.[$OS] [$OS].out [$OS].^$TARG
	@{cd libgraphics; mk $target}
	@{cd libobj; mk $target}

uninstall:V:
	rm -f $BIN/^$TARG
