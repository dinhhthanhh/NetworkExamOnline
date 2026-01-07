
CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -Wall -g -I./include
LIBS = `pkg-config --libs gtk+-3.0` -lpthread

SRCS = main.c ui.c net.c ui_utils.c auth_ui.c room_ui.c stats_ui.c admin_ui.c practice_ui.c exam_ui.c broadcast.c password_ui.c question_ui.c
OBJS = $(SRCS:.c=.o)

quiz_client: $(OBJS)
	$(CC) $(CFLAGS) -o quiz_client $(OBJS) $(LIBS)

run: quiz_client
	./quiz_client

clean:
	rm -f quiz_client *.o

.PHONY: run clean