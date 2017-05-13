# Makefile for pwol

CC=gcc -Wall
CFLAGS=-g -O -DVERSION="\"$(VERSION)\""

DEST=

PACKAGE=pwol
VERSION=1.1

BIN=pwol
BINOBJS=pwol.o

MAN=pwol.1

LIBS=

# Needed for Solaris:
#LIBS=-lnsl -lsocket


all: $(BIN)

$(BIN): $(BINOBJS)
	$(CC) -g -o $(BIN) $(BINOBJS) $(LIBS)

clean:
	-rm -f *~ core *.core *.o \#* $(BIN)

distclean: clean
	-rm -f $(LIB) $(BIN)

install: $(BIN)
	$(INSTALL) -o root -g wheel -m 0444 $(BIN) $(DEST)/bin
	$(INSTALL) -o root -g wheel -m 0444 $(MAN) $(DEST)/share/man/man1

push:	distclean
	git commit -a && git push

dist:	distclean
	(mkdir -p ../dist && cd ../dist && ln -sf ../$(PACKAGE) $(PACKAGE)-$(VERSION) && tar zcf $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE)-$(VERSION)/* && rm $(PACKAGE)-$(VERSION))
