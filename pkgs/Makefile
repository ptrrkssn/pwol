# Makefile for pkgs

PORTSDIR=/usr/ports
PORTSNAME=sysutils/pwol

all:
	@echo "Use 'make port'"
	@exit 1

port:	Makefile.port pkg-descr pkg-plist
	mkdir -p "$(PORTSDIR)/$(PORTSNAME)"
	cp Makefile.port "$(PORTSDIR)/$(PORTSNAME)/Makefile"
	cp pkg-descr pkg-plist "$(PORTSDIR)/$(PORTSNAME)"
	(cd "$(PORTSDIR)/$(PORTSNAME)" && make distclean && make makesum && portlint -AC && port test "$(PORTSDIR)/$(PORTSNAME)")


# Clean targets
maintainer-clean:	distclean

distclean:		clean
	rm -fr Makefile.port

clean mostlyclean:
	-rm -f *~ \#*
