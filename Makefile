CC = gcc
CFLAGS = -Wall -pedantic -O2 -std=gnu99

all: crsh

crsh: crsh.c
	$(CC) $(CFLAGS) crsh.c -o crsh

clean: FORCE
	rm -rf crsh

FORCE:
