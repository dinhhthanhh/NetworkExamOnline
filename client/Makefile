
CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -Wall -g -I. -I./include -I../third_party/cjson
LIBS = `pkg-config --libs gtk+-3.0` -lpthread

# Compile main, network and utilities + everything under screens/
SRCS = main.c net.c ui_utils.c admin_ui.c auth_ui.c exam_ui.c practice_ui.c room_ui.c stats_ui.c ui.c ../third_party/cjson/cJSON.c
OBJS = $(patsubst %.c,%.o,$(SRCS))

quiz_client: $(OBJS)
	$(CC) $(CFLAGS) -o quiz_client $(OBJS) $(LIBS)

run: quiz_client
	./quiz_client

clean:
	rm -f quiz_client *.o

.PHONY: run clean