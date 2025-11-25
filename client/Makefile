
CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -Wall -g -I./include
LIBS = `pkg-config --libs gtk+-3.0` -lpthread

SRCS = main.c ui.c net.c
OBJS = $(SRCS:.c=.o)

quiz_client: $(OBJS)
	$(CC) $(CFLAGS) -o quiz_client $(OBJS) $(LIBS)

run: quiz_client
	./quiz_client

clean:
	rm -f quiz_client *.o

.PHONY: run clean