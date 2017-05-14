# Makefile for pwol

DEST=
CC=gcc -Wall
CFLAGS=-g -O -DVERSION="\"$(VERSION)\""

PACKAGE=pwol
VERSION=1.2

ETCDIR=$(DEST)/etc
BINDIR=$(DEST)/bin
MANDIR=$(DEST)/share/man

BINOBJS=pwol.o

LIBS=

# Needed for Solaris:
#LIBS=-lnsl -lsocket


all: pwol

pwol: $(BINOBJS)
	$(CC) -g -o pwol $(BINOBJS) $(LIBS)

clean:
	-rm -f *~ core *.core *.o \#* pwol

distclean: clean
	-rm -f pwol

install: pwol
	$(INSTALL) -o root -g wheel -m 0444 pwol $(BINDIR)
	$(INSTALL) -o root -g wheel -m 0444 pwol.1 $(MANDIR)/man1

push:	distclean
	git commit -a && git push

dist:	distclean
	(mkdir -p ../dist && cd ../dist && ln -sf ../$(PACKAGE) $(PACKAGE)-$(VERSION) && tar zcf $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE)-$(VERSION)/* && rm $(PACKAGE)-$(VERSION))
