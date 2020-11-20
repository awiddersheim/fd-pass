CC=gcc
CFLAGS=-std=gnu99 -Wall -Wextra -Wunused -Wunreachable-code -Wpedantic -Wno-gnu-folding-constant

all:
	$(CC) $(CFLAGS) -o qb qb.c

clean:
	rm -f qb
