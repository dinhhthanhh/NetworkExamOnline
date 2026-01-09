#include "auth_ui.h"
#include "ui_utils.h"
#include "net.h"
#include "ui.h"
#include <string.h>
#include <stdlib.h>

// Handle LOGIN button click: validate input, send LOGIN, route to main menu
void on_login_clicked(GtkWidget *widget, gpointer data)
{
    LoginEntries *entries = (LoginEntries *)data;
    const char *username = gtk_entry_get_text(GTK_ENTRY(entries->user_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(entries->pass_entry));

    if (strlen(username) == 0 || strlen(password) == 0)
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                                   "Please enter username and password");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    char cmd[200];
    snprintf(cmd, sizeof(cmd), "LOGIN|%s|%s\n", username, password);
    send_message(cmd);

    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strstr(buffer, "LOGIN_OK"))
    {
        // Parse response: LOGIN_OK|user_id|token|role
        char *token_start = strchr(buffer, '|');
        if (token_start) {
            token_start++; // skip first '|'
            current_user_id = atoi(token_start);
            
            // Parse token (skip to second '|')
            token_start = strchr(token_start, '|');
            if (token_start) {
                token_start++; // skip second '|'
                
                // Parse role (skip to third '|')
                char *role_start = strchr(token_start, '|');
                if (role_start) {
                    role_start++; // skip third '|'
                    // Remove newline from role
                    char *newline = strchr(role_start, '\n');
                    if (newline) *newline = '\0';
                    
                    strncpy(client.role, role_start, sizeof(client.role) - 1);
                    client.role[sizeof(client.role) - 1] = '\0';
                    
                } else {
                    // Default role if not provided
                    strcpy(client.role, "user");
                }
            }
        }
        
        strncpy(client.username, username, sizeof(client.username) - 1);
        create_main_menu();
    }
    else
    {
        const char *error_msg = "Login failed";
        if (n > 0) {
            if (strstr(buffer, "USER_NOT_FOUND")) {
                error_msg = "Account does not exist";
            } else if (strstr(buffer, "WRONG_PASSWORD")) {
                error_msg = "Incorrect password";
            } else if (strstr(buffer, "User is already logged in")) {
                error_msg = "User is already logged in from another device";
            }
        }

        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "%s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

// Handle REGISTER button click: basic validation then send REGISTER
void on_register_clicked(GtkWidget *widget, gpointer data)
{
    LoginEntries *entries = (LoginEntries *)data;
    const char *username = gtk_entry_get_text(GTK_ENTRY(entries->user_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(entries->pass_entry));

    if (strlen(username) < 3 || strlen(password) < 3)
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                                   "Min 3 characters");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    char cmd[200];
    snprintf(cmd, sizeof(cmd), "REGISTER|%s|%s\n", username, password);
    send_message(cmd);

    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strstr(buffer, "REGISTER_OK"))
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                   "Registered! Please login.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    else
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "Registration failed");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

// Build and display the login screen (username/password + buttons)
void create_login_screen()
{
    extern GtkWidget *current_view;
    
    if (current_view)
    {
        gtk_container_remove(GTK_CONTAINER(main_window), current_view);
        current_view = NULL;
    }

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(vbox, 40);
    gtk_widget_set_margin_start(vbox, 50);
    gtk_widget_set_margin_end(vbox, 50);
    gtk_widget_set_margin_bottom(vbox, 40);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='32768'>ONLINE QUIZ SYSTEM</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 10);

    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep1, FALSE, FALSE, 0);

    GtkWidget *username_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(username_label), "<span foreground='#34495e' weight='bold' size='12000'>Username:</span>");
    GtkWidget *username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(username_entry), "Enter username");
    gtk_box_pack_start(GTK_BOX(vbox), username_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), username_entry, FALSE, FALSE, 0);

    GtkWidget *password_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(password_label), "<span foreground='#34495e' weight='bold' size='12000'>Password:</span>");
    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Enter password");
    gtk_box_pack_start(GTK_BOX(vbox), password_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), password_entry, FALSE, FALSE, 0);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 10);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    GtkWidget *login_btn = gtk_button_new_with_label("LOGIN");
    GtkWidget *register_btn = gtk_button_new_with_label("REGISTER");

    gtk_widget_set_name(login_btn, "login_btn");
    gtk_widget_set_name(register_btn, "register_btn");
    style_button(login_btn, "#3498db");
    style_button(register_btn, "#27ae60");
    

    LoginEntries *entries = g_malloc(sizeof(LoginEntries));
    entries->user_entry = username_entry;
    entries->pass_entry = password_entry;

    g_signal_connect(login_btn, "clicked", G_CALLBACK(on_login_clicked), entries);
    g_signal_connect(register_btn, "clicked", G_CALLBACK(on_register_clicked), entries);

    gtk_box_pack_start(GTK_BOX(button_box), login_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), register_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    show_view(vbox);
}
