NAME=voleventd
CFLAGS = -W -Wall -g
LIBS = -lasound

all:
	gcc $(CFLAGS) -I/lib/modules/`uname -r`/build/include -g -o $(NAME) $(NAME).c $(LIBS)

clean:
	rm -f $(NAME)

install:
	cp -f $(NAME) /usr/bin/
