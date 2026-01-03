#include "ui.h"
#include "ui_utils.h"
#include "auth_ui.h"
#include "room_ui.h"
#include "stats_ui.h"
#include "admin_ui.h"
#include "practice_ui.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <cairo.h>

// Global variables
ClientData client;
GtkWidget *main_window;
GtkWidget *current_view;
GtkWidget *question_label;
GtkWidget *option_buttons[4];
GtkWidget *timer_label;
GtkWidget *rooms_list;
GtkWidget *status_label;
GtkWidget *selected_room_label;
GtkWidget *room_list; 

int time_remaining = 0;
int test_active = 0;
int selected_room_id = -1;
int current_user_id = -1;

// Logout callback - send LOGOUT message to server
void on_logout_clicked(GtkWidget *widget, gpointer data)
{
    printf("[CLIENT] Sending LOGOUT message\n");
    send_message("LOGOUT\n");
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0) {
        printf("[CLIENT] Logout response: %s", buffer);
    }
    
    // Clear client data
    current_user_id = -1;
    memset(client.username, 0, sizeof(client.username));
    memset(client.role, 0, sizeof(client.role));
    
    // Go to login screen
    create_login_screen();
}

void create_main_menu()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top(vbox, 25);
    gtk_widget_set_margin_start(vbox, 30);
    gtk_widget_set_margin_end(vbox, 30);
    gtk_widget_set_margin_bottom(vbox, 25);

    // Check role to determine which menu to show
    int is_admin = (strcmp(client.role, "admin") == 0);

    GtkWidget *welcome_label = gtk_label_new(NULL);
    gchar *welcome_text = g_strdup_printf("%s Welcome, <b>%s</b>!",
                                          is_admin ? "👑" : "👤",
                                          client.username[0] ? client.username : "User");
    gtk_label_set_markup(GTK_LABEL(welcome_label), welcome_text);
    g_free(welcome_text);
    gtk_box_pack_start(GTK_BOX(vbox), welcome_label, FALSE, FALSE, 5);

    // Role indicator
    GtkWidget *role_label = gtk_label_new(NULL);
    gchar *role_text = g_strdup_printf("<span foreground='%s' size='small'>%s</span>",
                                       is_admin ? "#e74c3c" : "#3498db",
                                       is_admin ? "Administrator" : "Student");
    gtk_label_set_markup(GTK_LABEL(role_label), role_text);
    g_free(role_text);
    gtk_box_pack_start(GTK_BOX(vbox), role_label, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    if (is_admin) {
        // ========== ADMIN MENU ==========
        GtkWidget *create_room_btn = gtk_button_new_with_label("Create Exam Room");
        GtkWidget *create_practice_btn = gtk_button_new_with_label("Create Practice Room");
        GtkWidget *add_question_btn = gtk_button_new_with_label("Add Questions");
        GtkWidget *manage_rooms_btn = gtk_button_new_with_label("Manage Rooms");
        GtkWidget *logout_btn = gtk_button_new_with_label("Logout");

        style_button(create_room_btn, "#27ae60");
        style_button(create_practice_btn, "#9b59b6");
        style_button(add_question_btn, "#16a085");
        style_button(manage_rooms_btn, "#3498db");
        style_button(logout_btn, "#95a5a6");

        gtk_box_pack_start(GTK_BOX(vbox), create_room_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), create_practice_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), add_question_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), manage_rooms_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), logout_btn, FALSE, FALSE, 20);

        // Admin actions: create room directly without test mode screen
        g_signal_connect(create_room_btn, "clicked", G_CALLBACK(on_admin_create_room_clicked), NULL);
        g_signal_connect(create_practice_btn, "clicked", G_CALLBACK(on_admin_create_practice_room_clicked), NULL);
        g_signal_connect(add_question_btn, "clicked", G_CALLBACK(create_question_bank_screen), NULL);
        g_signal_connect(manage_rooms_btn, "clicked", G_CALLBACK(create_admin_panel), NULL);
        g_signal_connect(logout_btn, "clicked", G_CALLBACK(on_logout_clicked), NULL);
    } else {
        // ========== USER MENU ==========
        GtkWidget *test_mode_btn = gtk_button_new_with_label("Test Mode");
        GtkWidget *practice_btn = gtk_button_new_with_label("Practice Mode");
        GtkWidget *stats_btn = gtk_button_new_with_label("My Statistics");
        GtkWidget *leaderboard_btn = gtk_button_new_with_label("Leaderboard");
        GtkWidget *logout_btn = gtk_button_new_with_label("Logout");

        style_button(test_mode_btn, "#3498db");
        style_button(practice_btn, "#9b59b6");
        style_button(stats_btn, "#e74c3c");
        style_button(leaderboard_btn, "#f39c12");
        style_button(logout_btn, "#95a5a6");

        gtk_box_pack_start(GTK_BOX(vbox), test_mode_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), practice_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), stats_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), leaderboard_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), logout_btn, FALSE, FALSE, 20);

        g_signal_connect(test_mode_btn, "clicked", G_CALLBACK(create_test_mode_screen), NULL);
        g_signal_connect(practice_btn, "clicked", G_CALLBACK(create_practice_screen), NULL);
        g_signal_connect(stats_btn, "clicked", G_CALLBACK(create_stats_screen), NULL);
        g_signal_connect(leaderboard_btn, "clicked", G_CALLBACK(create_leaderboard_screen), NULL);
        g_signal_connect(logout_btn, "clicked", G_CALLBACK(on_logout_clicked), NULL);
    }

    show_view(vbox);
}
