#include "include/client_common.h"
#include "ui.h"
#include "net.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define WINDOW_WIDTH   600
#define WINDOW_HEIGHT  500

static int connect_to_server();
static void show_connection_error_dialog();
int sock_fd = 0;  


int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    if (connect_to_server() < 0) {
        show_connection_error_dialog();
        return 1;
    }

    // 👉 Đặt timeout sau khi kết nối thành công
    net_set_timeout(client.socket_fd);

    /* Connection established; keep UI message minimal */

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Online Quiz System");
    gtk_window_set_default_size(GTK_WINDOW(main_window),
                                WINDOW_WIDTH, WINDOW_HEIGHT);

    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    create_login_screen();

    gtk_widget_show_all(main_window);
    gtk_main();

    close(client.socket_fd);
    return 0;
}

static int connect_to_server() {
    struct sockaddr_in server_addr;

    client.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client.socket_fd < 0) {
        perror("socket()");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_PORT);

    if (!inet_aton(SERVER_IP, &server_addr.sin_addr)) {
        fprintf(stderr, "Invalid server IP address.\n");
        return -1;
    }

    if (connect(client.socket_fd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect()");
        return -1;
    }

    return 0;
}

static void show_connection_error_dialog() {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "Không thể kết nối đến server!\n"
        "Vui lòng kiểm tra lại:\n"
        "• Server đã chạy chưa?\n"
        "• IP & PORT trong net.h đã đúng chưa?"
    );

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
