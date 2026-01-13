CC = gcc
CFLAGS = -Wall -g -pthread -I./include
LIBS = -lsqlite3 -lpthread -lcrypto -lssl

SRCS = quiz_server.c db.c auth.c network.c rooms.c questions.c results.c stats.c admin.c timer.c practice.c
OBJS = $(SRCS:.c=.o)

quiz_server: $(OBJS)
	$(CC) $(CFLAGS) -o quiz_server $(OBJS) $(LIBS)

run: quiz_server
	./quiz_server

clean:
	rm -f quiz_server *.o quiz_app.db server.log

.PHONY: run clean
