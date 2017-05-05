CC=gcc
CFLAGS= -Wall -std=gnu99

all: cproxy sproxy

client: cproxy.o
	$(CC) $(CFLAGS) -o cproxy cproxy.c

server: sproxy.o
	$(CC) $(CFLAGS) -o sproxy sproxy.c

clean:
	-rm cproxy sproxy
