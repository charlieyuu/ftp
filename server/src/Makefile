CC = gcc

CFLAGS = -Wall

all: server

server: common.h utils.c main.c server.c handles.c
	$(CC) $(CFLAGS) -o server common.h utils.c main.c server.c handles.c

clean:
	rm -f server