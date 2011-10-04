DEFAULT_INTERVAL=4
PREFIX=/usr/local
CFLAGS=-std=c99 -Wall -DDEFAULT_INTERVAL=${DEFAULT_INTERVAL}
LDFLAGS=-s -lxcb

.PHONY: all install clean

all: hidcur

hidcur: hidcur.c
	${CC} ${CFLAGS} ${LDFLAGS} -o hidcur hidcur.c

install:
	install hidcur ${PREFIX}/bin/hidcur

clean:
	rm -f hidcur
