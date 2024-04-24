</$objtype/mkfile

BIN=$home/bin/$objtype
TARG=\
	vis\

OFILES=\
	alloc.$O\

HFILES=dat.h fns.h

LIB=\
	libobj/libobj.a$O\
	libgraphics/libgraphics.a$O\

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
	@{cd libgraphics; mk $target}

clean nuke:V:
	rm -f *.[$OS] [$OS].out $TARG
	@{cd libgraphics; mk $target}
	@{cd libobj; mk $target}

uninstall:V:
	rm -f $BIN/^$TARG
