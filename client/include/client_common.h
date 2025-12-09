#ifndef CLIENT_COMMON_H
#define CLIENT_COMMON_H

#include <gtk/gtk.h>

#define SERVER_IP "127.0.0.1"
// #define SERVER_IP "172.18.37.119"
#define SERVER_PORT 8888
#define BUFFER_SIZE 4096

typedef struct {
    int socket_fd;
    int user_id;
    char username[50];
    int current_room;
    int current_question;
    int selected_answer;
    int answers[100];
} ClientData;

extern ClientData client;
extern GtkWidget *main_window;

#endif // CLIENT_COMMON_H
