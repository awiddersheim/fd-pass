CC=gcc
CFLAGS=

all:
	$(CC) $(CFLAGS) -o qb qb.c

clean:
	rm -f qb
