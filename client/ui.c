#include "ui.h"
#include "ui_utils.h"
#include "auth_ui.h"
#include "room_ui.h"
#include "stats_ui.h"
#include "admin_ui.h"
#include "practice_ui.h"
#include "password_ui.h"
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
    gchar *welcome_text = g_strdup_printf("Welcome, <b>%s</b>!",
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
        
        // Section 1: Exam Management
        GtkWidget *exam_section = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(exam_section), 
                    "<span foreground='#3498db' weight='bold' size='14000'>EXAM MANAGEMENT</span>");
        gtk_label_set_xalign(GTK_LABEL(exam_section), 0);
        gtk_box_pack_start(GTK_BOX(vbox), exam_section, FALSE, FALSE, 8);
        
                GtkWidget *exam_manage_btn = gtk_button_new_with_label("Manage Exam Rooms");
        style_button(exam_manage_btn, "#3498db");
        gtk_box_pack_start(GTK_BOX(vbox), exam_manage_btn, FALSE, FALSE, 3);
        
        GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(vbox), sep1, FALSE, FALSE, 10);
        
        // Section 2: Practice Management
        GtkWidget *practice_section = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(practice_section), 
                    "<span foreground='#9b59b6' weight='bold' size='14000'>PRACTICE MANAGEMENT</span>");
        gtk_label_set_xalign(GTK_LABEL(practice_section), 0);
        gtk_box_pack_start(GTK_BOX(vbox), practice_section, FALSE, FALSE, 8);
        
                GtkWidget *practice_manage_btn = gtk_button_new_with_label("Manage Practice Rooms");
        style_button(practice_manage_btn, "#9b59b6");
        gtk_box_pack_start(GTK_BOX(vbox), practice_manage_btn, FALSE, FALSE, 3);
        
        GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 10);
        
        // Change Password button
        GtkWidget *change_pass_btn = gtk_button_new_with_label("Change Password");
        style_button(change_pass_btn, "#f39c12");
        gtk_box_pack_start(GTK_BOX(vbox), change_pass_btn, FALSE, FALSE, 3);
        
        // Logout button
        GtkWidget *logout_btn = gtk_button_new_with_label("Logout");
        style_button(logout_btn, "#e74c3c");
        gtk_box_pack_start(GTK_BOX(vbox), logout_btn, FALSE, FALSE, 10);

        // Admin actions - Connect signals
        g_signal_connect(exam_manage_btn, "clicked", G_CALLBACK(create_admin_panel), NULL);
        g_signal_connect(practice_manage_btn, "clicked", G_CALLBACK(show_manage_practice_rooms), NULL);
        g_signal_connect(change_pass_btn, "clicked", G_CALLBACK(show_change_password_dialog), NULL);
        g_signal_connect(logout_btn, "clicked", G_CALLBACK(on_admin_logout_clicked), NULL);
    } else {
        // ========== USER MENU ==========
        GtkWidget *test_mode_btn = gtk_button_new_with_label("Test Mode");
        GtkWidget *practice_btn = gtk_button_new_with_label("Practice Mode");
        GtkWidget *stats_btn = gtk_button_new_with_label("My Statistics");
        GtkWidget *leaderboard_btn = gtk_button_new_with_label("Leaderboard");
        GtkWidget *change_pass_btn = gtk_button_new_with_label("Change Password");
        GtkWidget *logout_btn = gtk_button_new_with_label("Logout");

        style_button(test_mode_btn, "#3498db");
        style_button(practice_btn, "#9b59b6");
        style_button(stats_btn, "#e74c3c");
        style_button(leaderboard_btn, "#f39c12");
        style_button(change_pass_btn, "#f39c12");
        style_button(logout_btn, "#95a5a6");

        gtk_box_pack_start(GTK_BOX(vbox), test_mode_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), practice_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), stats_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), leaderboard_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), change_pass_btn, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), logout_btn, FALSE, FALSE, 20);

        g_signal_connect(test_mode_btn, "clicked", G_CALLBACK(create_test_mode_screen), NULL);
        g_signal_connect(practice_btn, "clicked", G_CALLBACK(show_practice_list_screen), NULL);
        g_signal_connect(stats_btn, "clicked", G_CALLBACK(create_stats_screen), NULL);
        g_signal_connect(leaderboard_btn, "clicked", G_CALLBACK(create_leaderboard_screen), NULL);
        g_signal_connect(change_pass_btn, "clicked", G_CALLBACK(show_change_password_dialog), NULL);
        g_signal_connect(logout_btn, "clicked", G_CALLBACK(on_user_logout_clicked), NULL);
    }

    show_view(vbox);
}

// Logout handler for admin
void on_admin_logout_clicked(GtkWidget *widget, gpointer data) {
    // Send logout message to server
    char msg[64];
    snprintf(msg, sizeof(msg), "LOGOUT\n");
    send_message(msg);
    
    // Wait for response
    char recv_buf[256];
    receive_message(recv_buf, sizeof(recv_buf));
    
    // Clear client data
    memset(&client, 0, sizeof(client));
    client.socket_fd = -1;
    client.user_id = -1;
    current_user_id = -1;
    
    // Reconnect to server for next login
    if (reconnect_to_server() < 0) {
        show_error_dialog("Failed to reconnect to server. Please restart the application.");
        gtk_main_quit();
        return;
    }
    
    // Show success message
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK,
                                               "Logout Successful");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "You have been logged out successfully.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    // Go back to login screen
    create_login_screen();
}

// Logout handler for user
void on_user_logout_clicked(GtkWidget *widget, gpointer data) {
    // Send logout message to server
    char msg[64];
    snprintf(msg, sizeof(msg), "LOGOUT\n");
    send_message(msg);
    
    // Wait for response
    char recv_buf[256];
    receive_message(recv_buf, sizeof(recv_buf));
    
    // Clear client data
    memset(&client, 0, sizeof(client));
    client.socket_fd = -1;
    client.user_id = -1;
    current_user_id = -1;
    
    // Reconnect to server for next login
    if (reconnect_to_server() < 0) {
        show_error_dialog("Failed to reconnect to server. Please restart the application.");
        gtk_main_quit();
        return;
    }
    
    // Show success message
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK,
                                               "Logout Successful");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "See you next time!");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    // Go back to login screen
    create_login_screen();
}

// Admin create practice room wrapper
void on_admin_create_practice_clicked(GtkWidget *widget, gpointer data) {
    show_create_practice_dialog();
}
