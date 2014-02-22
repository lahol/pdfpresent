CC=gcc
PKG_CONFIG=pkg-config
INSTALL=install

CFLAGS=-Wall -g `$(PKG_CONFIG) --cflags poppler poppler-glib poppler-cairo glib-2.0 gtk+-2.0 cairo`
LIBS=-lc -lz `$(PKG_CONFIG) --libs poppler poppler-glib poppler-cairo glib-2.0 gthread-2.0 gtk+-2.0 cairo`

PREFIX := /usr

present_SRC := $(wildcard *.c)
present_OBJ := $(present_SRC:.c=.o)
present_HEADERS := $(wildcard *.h)

all: pdfpresent

pdfpresent: $(present_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c $(present_HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

install: pdfpresent
	$(INSTALL) pdfpresent $(PREFIX)/bin

uninstall:
	rm $(PREFIX)/bin/pdfpresent

clean:
	rm -f pdfpresent $(present_OBJ)

.PHONY: all clean install uninstall
