SRV_NAME=voleventd
CLT_NAME=xosdvol
CC=gcc
CFLAGS=-W -Wall -g
SRV_LIBS=-lasound
CLT_LIBS=-lxosd

all: client server

client: $(CLT_NAME).c
	$(CC) $(CFLAGS) $(CLT_NAME).c -o $(CLT_NAME) $(CLT_LIBS)

server: $(SRV_NAME).c
	$(CC) -I/lib/modules/`uname -r`/build/include $(CFLAGS) $(SRV_NAME).c \
	-o $(SRV_NAME) $(SRV_LIBS)

clean:
	rm -f $(SRV_NAME) $(CLT_NAME)

install: install-server install-client

install-server:
	mkdir -p $(DESTDIR)/usr/sbin
	cp -f $(SRV_NAME) $(DESTDIR)/usr/sbin/

install-client:
	mkdir -p $(DESTDIR)/usr/bin
	cp -f $(CLT_NAME) $(DESTDIR)/usr/bin/
