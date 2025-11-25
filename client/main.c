#include "include/client_common.h"
#include "ui.h"
#include "net.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    struct sockaddr_in server_addr;
    client.socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_aton(SERVER_IP, &server_addr.sin_addr);

    if (connect(client.socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Connection failed\n");
        return 1;
    }

    printf("Connected to server\n");

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Online Quiz System");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 600, 500);

    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    create_login_screen();

    gtk_widget_show_all(main_window);
    gtk_main();

    close(client.socket_fd);
    return 0;
}
