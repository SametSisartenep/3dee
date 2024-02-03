</$objtype/mkfile

BIN=$home/bin/$objtype
TARG=3d
OFILES=\
	alloc.$O\
	main.$O\

HFILES=dat.h fns.h

LIB=\
	libobj/libobj.a$O\
	libgraphics/libgraphics.a$O\

</sys/src/cmd/mkone

libgraphics/libgraphics.a$O:
	cd libgraphics
	mk pulldeps
	mk install

libobj/libobj.a$O:
	cd libobj
	mk install

pulldeps:VQ:
	git/clone git://antares-labs.eu/libobj || \
	git/clone git://shithub.us/rodri/libobj || \
	git/clone https://github.com/sametsisartenep/libobj
	git/clone git://antares-labs.eu/libgraphics || \
	git/clone https://github.com/sametsisartenep/libgraphics

clean nuke:V:
	rm -f *.[$OS] [$OS].out $TARG
	@{cd libgraphics; mk $target}
	@{cd libobj; mk $target}

uninstall:V:
	rm -f $BIN/$TARG
