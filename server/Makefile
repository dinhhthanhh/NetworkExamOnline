CC = gcc
CFLAGS = -Wall -g -pthread -I./include -I../third_party/cjson
LIBS = -lsqlite3 -lpthread -lcrypto -lssl

SRCS = quiz_server.c db.c auth.c network.c rooms.c questions.c results.c stats.c admin.c timer.c practice.c ../third_party/cjson/cJSON.c
OBJS = $(SRCS:.c=.o)

quiz_server: $(OBJS)
	$(CC) $(CFLAGS) -o quiz_server $(OBJS) $(LIBS)

run: quiz_server
	./quiz_server

clean:
	rm -f quiz_server *.o quiz_app.db

.PHONY: run clean