CC=gcc
CFLAGS=-Wall -g `pkg-config --cflags poppler poppler-glib poppler-cairo glib-2.0 gtk+-2.0 cairo`
LIBS=-lc -lz `pkg-config --libs poppler poppler-glib poppler-cairo glib-2.0 gthread-2.0 gtk+-2.0 cairo`

all: pdfpresent

pdfpresent: main.o page-cache.o presentation.o utils.o
	$(CC) $(CFLAGS) -o pdfpresent main.o page-cache.o presentation.o utils.o $(LIBS)

main.o: main.c utils.h page-cache.h presentation.h
	$(CC) $(CFLAGS) -c -o main.o main.c

page-cache.o: page-cache.c page-cache.h utils.h
	$(CC) $(CFLAGS) -c -o page-cache.o page-cache.c

presentation.o: presentation.c presentation.h page-cache.h utils.h
	$(CC) $(CFLAGS) -c -o presentation.o presentation.c

utils.o: utils.h utils.c
	$(CC) $(CFLAGS) -c -o utils.o utils.c

clean:
	rm -f pdfpresent
	rm -f *.o

