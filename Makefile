# Makefile for pwol

CC=gcc -Wall
CFLAGS=-g

ALL=pwol
PWOLOBJS=pwol.o

all: $(ALL)

pwol: $(PWOLOBJS)
	$(CC) -g -o pwol $(PWOLOBJS) # -lnsl -lsocket

clean:
	-rm -f *~ core *.o \#* $(ALL)
