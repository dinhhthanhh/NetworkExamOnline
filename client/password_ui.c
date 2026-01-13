#include "password_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include <string.h>

// Callback for change password button
void on_change_password_submit(GtkWidget *widget, gpointer user_data) {
    GtkWidget **entries = (GtkWidget **)user_data;
    GtkWidget *old_pass_entry = entries[0];
    GtkWidget *new_pass_entry = entries[1];
    GtkWidget *confirm_pass_entry = entries[2];
    GtkWidget *dialog = entries[3];
    
    const char *old_pass = gtk_entry_get_text(GTK_ENTRY(old_pass_entry));
    const char *new_pass = gtk_entry_get_text(GTK_ENTRY(new_pass_entry));
    const char *confirm_pass = gtk_entry_get_text(GTK_ENTRY(confirm_pass_entry));
    
    // Validation
    if (strlen(old_pass) == 0 || strlen(new_pass) == 0 || strlen(confirm_pass) == 0) {
        show_error_dialog("Please fill all fields!");
        return;
    }
    
    if (strlen(new_pass) < 3) {
        show_error_dialog("New password must be at least 3 characters!");
        return;
    }
    
    if (strcmp(new_pass, confirm_pass) != 0) {
        show_error_dialog("New passwords do not match!");
        return;
    }
    
    // Send to server
    char request[512];
    snprintf(request, sizeof(request), "CHANGE_PASSWORD|%s|%s\n", old_pass, new_pass);
    send_message(request);
    
    // Receive response
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    if (n > 0) {
        buffer[n] = '\0';
        
        if (strstr(buffer, "CHANGE_PASSWORD_OK")) {
            GtkWidget *success_dialog = gtk_message_dialog_new(
                GTK_WINDOW(main_window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "Password Changed Successfully"
            );
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(success_dialog),
                "Your password has been updated.");
            gtk_dialog_run(GTK_DIALOG(success_dialog));
            gtk_widget_destroy(success_dialog);
            
            // Close change password dialog
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        } else {
            // Parse error message
            char *error_msg = strchr(buffer, '|');
            if (error_msg) {
                error_msg++; // Skip '|'
                char *newline = strchr(error_msg, '\n');
                if (newline) *newline = '\0';
            } else {
                error_msg = "Failed to change password";
            }
            
            GtkWidget *error_dialog = gtk_message_dialog_new(
                GTK_WINDOW(main_window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                "Error"
            );
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                "%s", error_msg);
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }
    } else {
        show_error_dialog("Network error!");
    }
}

// Show change password dialog
void show_change_password_dialog(void) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Change Password",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Change Password", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);
    
    // Title
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='14000' weight='bold'>Change Your Password</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 5);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 5);
    
    // Old password
    GtkWidget *old_label = gtk_label_new("Old Password:");
    gtk_label_set_xalign(GTK_LABEL(old_label), 0);
    GtkWidget *old_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(old_pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(old_pass_entry), "Enter current password");
    gtk_box_pack_start(GTK_BOX(vbox), old_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), old_pass_entry, FALSE, FALSE, 0);
    
    // New password
    GtkWidget *new_label = gtk_label_new("New Password:");
    gtk_label_set_xalign(GTK_LABEL(new_label), 0);
    GtkWidget *new_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(new_pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(new_pass_entry), "Enter new password (min 3 chars)");
    gtk_box_pack_start(GTK_BOX(vbox), new_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), new_pass_entry, FALSE, FALSE, 0);
    
    // Confirm password
    GtkWidget *confirm_label = gtk_label_new("Confirm New Password:");
    gtk_label_set_xalign(GTK_LABEL(confirm_label), 0);
    GtkWidget *confirm_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(confirm_pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(confirm_pass_entry), "Re-enter new password");
    gtk_box_pack_start(GTK_BOX(vbox), confirm_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), confirm_pass_entry, FALSE, FALSE, 0);
    
    gtk_widget_show_all(dialog);
    
    // Prepare user data for callback
    GtkWidget **entries = g_malloc(sizeof(GtkWidget*) * 4);
    entries[0] = old_pass_entry;
    entries[1] = new_pass_entry;
    entries[2] = confirm_pass_entry;
    entries[3] = dialog;
    
    // Connect Accept button to callback
    g_signal_connect(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT),
                    "clicked", G_CALLBACK(on_change_password_submit), entries);
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    g_free(entries);
    gtk_widget_destroy(dialog);
    
    (void)response; // Suppress unused warning
}
