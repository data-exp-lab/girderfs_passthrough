CC=gcc
CFLAGS=-g -Wall `pkg-config --cflags fuse3 jansson libcurl`
LIBS=`pkg-config --libs fuse3 jansson libcurl`
DESTDIR=/usr/local

all: passthrough-fuse

passthrough-fuse: girderfs_passthrough.c tree.c
	$(CC) $(CFLAGS) girderfs_passthrough.c tree.c $(LIBS) -o passthrough-fuse

clean:
	rm -f passthrough-fuse

install:
	install -d $(DESTDIR)/bin
	install passthrough-fuse $(DESTDIR)/bin
