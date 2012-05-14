DEFAULT_INTERVAL=2
CPPFLAGS=-DDEFAULT_INTERVAL=$(DEFAULT_INTERVAL)
CFLAGS=-std=c99 -Wall
LDFLAGS=-s -lxcb
PREFIX=/usr/local

.PHONY: all install clean

all: hidcur

hidcur: hidcur.o

install: all
	install -D -m755 hidcur $(DESTDIR)$(PREFIX)/bin/hidcur

clean:
	rm -f hidcur.o hidcur
