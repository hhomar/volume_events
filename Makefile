SRV_NAME=voleventd
CLT_NAME=xosd_vol
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

install:
	cp -f $(SRV_NAME) $(CLT_NAME) /usr/bin/
