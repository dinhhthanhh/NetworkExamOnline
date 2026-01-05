#include "admin_ui.h"
#include "exam_room_creator.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include "question_ui.h"
#include "broadcast.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <fcntl.h>

// Import questions from a CSV file into the currently selected room
void on_import_csv_to_room(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *room_combo = GTK_WIDGET(user_data);
    
    // Lấy room_id từ dropdown
    const char *room_id_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(room_combo));
    if (!room_id_str || atoi(room_id_str) == -1) {
        show_error_dialog("Please select a valid room first!");
        return;
    }
    
    int room_id = atoi(room_id_str);
    
    // Tạo file chooser dialog
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select CSV File to Import",
        GTK_WINDOW(main_window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Import", GTK_RESPONSE_ACCEPT,
        NULL);

    // Thêm filter chỉ hiển thị file CSV
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "CSV files (*.csv)");
    gtk_file_filter_add_pattern(filter, "*.csv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    // Filter hiển thị tất cả files
    GtkFileFilter *filter_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_all, "All files (*.*)");
    gtk_file_filter_add_pattern(filter_all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_all);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_ACCEPT)
    {
        char *filepath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        if (filepath)
        {
            // Đọc file content
            FILE *fp = fopen(filepath, "rb");
            if (!fp) {
                show_error_dialog("Cannot open file!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            // Get file size
            fseek(fp, 0, SEEK_END);
            long file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            
            // Validate file size (max 5MB)
            if (file_size <= 0 || file_size > 5 * 1024 * 1024) {
                fclose(fp);
                show_error_dialog("File too large or empty (max 5MB)!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            // Read file into buffer
            char *file_buffer = malloc(file_size);
            if (!file_buffer) {
                fclose(fp);
                show_error_dialog("Memory allocation failed!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            size_t read_bytes = fread(file_buffer, 1, file_size, fp);
            fclose(fp);
            
            if (read_bytes != file_size) {
                free(file_buffer);
                show_error_dialog("Failed to read file!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            // Extract filename only (without path)
            char *filename = strrchr(filepath, G_DIR_SEPARATOR);
            if (!filename) filename = strrchr(filepath, '/');
            if (!filename) filename = strrchr(filepath, '\\');
            filename = filename ? filename + 1 : filepath;
            
            // Hiển thị loading message
            GtkWidget *loading_dialog = gtk_message_dialog_new(
                GTK_WINDOW(main_window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_NONE,
                "Uploading and importing CSV...\n\nFile: %s (%ld bytes)\nPlease wait...",
                filename, file_size);
            gtk_widget_show_all(loading_dialog);
            
            // Process events để hiển thị dialog
            while (gtk_events_pending())
                gtk_main_iteration();
            
            // Gửi header: IMPORT_CSV|room_id|filename|file_size
            char header[512];
            snprintf(header, sizeof(header), "IMPORT_CSV|%d|%s|%ld\n", 
                     room_id, filename, file_size);
            send_message(header);
            
            // Wait for ACK from server
            char ack_buffer[64];
            ssize_t ack_n = receive_message(ack_buffer, sizeof(ack_buffer));
            if (ack_n <= 0 || strncmp(ack_buffer, "READY", 5) != 0) {
                free(file_buffer);
                gtk_widget_destroy(loading_dialog);
                show_error_dialog("Server not ready to receive file!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            // Gửi file content
            ssize_t sent = send(client.socket_fd, file_buffer, file_size, 0);
            free(file_buffer);
            
            if (sent != file_size) {
                gtk_widget_destroy(loading_dialog);
                show_error_dialog("Failed to upload file!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            // Nhận response từ server
            char buffer[BUFFER_SIZE];
            ssize_t n = receive_message(buffer, sizeof(buffer));
            
            // Đóng loading dialog
            gtk_widget_destroy(loading_dialog);

            if (n > 0)
            {
                if (strstr(buffer, "IMPORT_OK"))
                {
                    // Parse số lượng câu hỏi đã import: IMPORT_OK|count
                    int count = 0;
                    if (sscanf(buffer, "IMPORT_OK|%d", &count) == 1)
                    {
                        char success_msg[256];
                        snprintf(success_msg, sizeof(success_msg),
                                "Import successful!\n\n"
                                "Imported %d question(s) to this room.\n\n"
                                "File: %s",
                                count, filename);
                        
                        GtkWidget *success_dialog = gtk_message_dialog_new(
                            GTK_WINDOW(main_window),
                            GTK_DIALOG_DESTROY_WITH_PARENT,
                            GTK_MESSAGE_INFO,
                            GTK_BUTTONS_OK,
                            "%s", success_msg);
                        gtk_dialog_run(GTK_DIALOG(success_dialog));
                        gtk_widget_destroy(success_dialog);
                    }
                }
                else if (strstr(buffer, "ERROR"))
                {
                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                            "Import failed!\n\n"
                            "Server error: %.200s\n\n"
                            "Please check:\n"
                            "• CSV format is correct\n"
                            "• File is accessible by server\n"
                            "• Database has required columns",
                            buffer);
                    
                    GtkWidget *error_dialog = gtk_message_dialog_new(
                        GTK_WINDOW(main_window),
                        GTK_DIALOG_DESTROY_WITH_PARENT,
                        GTK_MESSAGE_ERROR,
                        GTK_BUTTONS_OK,
                        "%s", error_msg);
                    gtk_dialog_run(GTK_DIALOG(error_dialog));
                    gtk_widget_destroy(error_dialog);
                }
            }
            else
            {
                GtkWidget *error_dialog = gtk_message_dialog_new(
                    GTK_WINDOW(main_window),
                    GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_OK,
                    "No response from server!\n\nPlease check connection.");
                gtk_dialog_run(GTK_DIALOG(error_dialog));
                gtk_widget_destroy(error_dialog);
            }
            
            g_free(filepath);
        }
    }
    
    gtk_widget_destroy(dialog);
}

// Submit a single new question to the server for the selected room
void on_submit_question(GtkWidget *widget, gpointer user_data)
{
    QuestionFormData *data = (QuestionFormData *)user_data;
    
    // Lấy room_id từ combo box
    const char *room_id_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(data->room_combo));
    if (!room_id_str || atoi(room_id_str) == -1) {
        show_error_dialog("Please select a valid room!");
        return;
    }
    
    // Lấy dữ liệu từ form
    const char *question = gtk_entry_get_text(GTK_ENTRY(data->question_entry));
    const char *opt1 = gtk_entry_get_text(GTK_ENTRY(data->opt1_entry));
    const char *opt2 = gtk_entry_get_text(GTK_ENTRY(data->opt2_entry));
    const char *opt3 = gtk_entry_get_text(GTK_ENTRY(data->opt3_entry));
    const char *opt4 = gtk_entry_get_text(GTK_ENTRY(data->opt4_entry));
    
    // Lấy difficulty và category
    const char *difficulty = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(data->difficulty_combo));
    const char *category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(data->category_combo));
    
    // Xác định correct answer từ radio buttons
    int correct_answer = 0;
    for (int i = 0; i < 4; i++) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->correct_radio[i]))) {
            correct_answer = i;
            break;
        }
    }
    
    // Validation
    if (strlen(question) == 0 || strlen(opt1) == 0 || strlen(opt2) == 0 || 
        strlen(opt3) == 0 || strlen(opt4) == 0) {
        show_error_dialog("Please fill all fields!");
        return;
    }
    
    if (!difficulty || !category) {
        show_error_dialog("Please select difficulty and category!");
        return;
    }
    
    // Gửi request đến server với đầy đủ thông tin
    // Format: ADD_QUESTION|user_id|room_id|question|opt1|opt2|opt3|opt4|correct|difficulty|category
    char request[2048];
    snprintf(request, sizeof(request), 
             "ADD_QUESTION|%d|%d|%s|%s|%s|%s|%s|%d|%s|%s\n",
             current_user_id, atoi(room_id_str), question, opt1, opt2, opt3, opt4, 
             correct_answer, difficulty, category);
    send_message(request);
    
    // Nhận response
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        if (strstr(buffer, "QUESTION_ADDED")) {
            show_success_dialog("Question added successfully!");
            // Reset form
            gtk_entry_set_text(GTK_ENTRY(data->question_entry), "");
            gtk_entry_set_text(GTK_ENTRY(data->opt1_entry), "");
            gtk_entry_set_text(GTK_ENTRY(data->opt2_entry), "");
            gtk_entry_set_text(GTK_ENTRY(data->opt3_entry), "");
            gtk_entry_set_text(GTK_ENTRY(data->opt4_entry), "");
            gtk_combo_box_set_active(GTK_COMBO_BOX(data->difficulty_combo), 0);
            gtk_combo_box_set_active(GTK_COMBO_BOX(data->category_combo), 0);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->correct_radio[0]), TRUE);
        } else {
            show_error_dialog("Failed to add question!");
        }
    }
}

// Open a room so students can join and take the exam
void on_start_room_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    // Confirm dialog
    GtkWidget *confirm_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       GTK_MESSAGE_QUESTION,
                                                       GTK_BUTTONS_YES_NO,
                                                       "Open this room?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(confirm_dialog),
                                             "Users will be able to join and take the test.");
    
    int response = gtk_dialog_run(GTK_DIALOG(confirm_dialog));
    gtk_widget_destroy(confirm_dialog);
    
    if (response != GTK_RESPONSE_YES) {
        return;
    }
    
    char msg[64];
    snprintf(msg, sizeof(msg), "START_ROOM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    GtkWidget *dialog;
    if (n > 0 && strncmp(buffer, "START_ROOM_OK", 13) == 0) {
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_OK,
                                       "Room opened successfully!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        // Refresh admin panel
        create_admin_panel();
    } else {
        // Parse error message
        char *error_msg = strchr(buffer, '|');
        if (error_msg) {
            error_msg++; // Skip '|'
        } else {
            error_msg = "Failed to start room";
        }
        
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_OK,
                                       "%s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

// Close a room so it no longer appears in the join list
void on_close_room_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    // Confirm dialog
    GtkWidget *confirm_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       GTK_MESSAGE_QUESTION,
                                                       GTK_BUTTONS_YES_NO,
                                                       "Are you sure you want to close this room?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(confirm_dialog),
                                             "This room will be removed from the list.");
    
    int response = gtk_dialog_run(GTK_DIALOG(confirm_dialog));
    gtk_widget_destroy(confirm_dialog);
    
    if (response != GTK_RESPONSE_YES) {
        return;
    }
    
    char msg[64];
    snprintf(msg, sizeof(msg), "CLOSE_ROOM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    GtkWidget *dialog;
    if (n > 0 && strncmp(buffer, "CLOSE_ROOM_OK", 13) == 0) {
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_OK,
                                       "Room closed successfully!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        // Refresh admin panel
        create_admin_panel();
    } else {
        // Parse error message
        char *error_msg = strchr(buffer, '|');
        if (error_msg) {
            error_msg++; // Skip '|'
        } else {
            error_msg = "Failed to close room";
        }
        
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_OK,
                                       "%s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

// Permanently delete a room and all of its data after confirmation
void on_delete_room_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Delete this room?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "WARNING: This action cannot be undone.\n\n"
                                             "• All students currently taking this exam will be immediately stopped\n"
                                             "• All data (questions, answers, results) will be permanently deleted\n"
                                             "• This room will disappear from all lists\n\n"
                                             "Are you absolutely sure?");
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response != GTK_RESPONSE_YES) {
        return;
    }
    
    // Stop broadcast listener first so it stops touching socket flags
    if (broadcast_is_listening()) {
        broadcast_stop_listener();
        usleep(50000); // 50ms - wait for any pending timer callbacks to finish
    }
    
    // Now it is safe to force socket back to blocking mode
    if (client.socket_fd > 0) {
        int flags = fcntl(client.socket_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
        }
    }
    
    // Flush socket buffer before request
    flush_socket_buffer(client.socket_fd);
    
    char msg[64];
    snprintf(msg, sizeof(msg), "DELETE_ROOM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    GtkWidget *result_dialog;
    if (n > 0 && strncmp(buffer, "DELETE_ROOM_OK", 14) == 0) {
        result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_OK,
                                       "Room deleted successfully!");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                 "Room #%d has been permanently removed.\n"
                                                 "All participants have been notified.",
                                                 room_id);
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
        
        // Give a short delay so any broadcasts can be processed
        usleep(200000);
        
        // Refresh admin panel
        create_admin_panel();
    } else {
        char *error_msg = strchr(buffer, '|');
        if (error_msg) {
            error_msg++;
        } else {
            error_msg = "Failed to delete room";
        }
        
        result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_OK,
                                       "Error");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                 "%s", error_msg);
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
    }
}

// Main admin screen listing rooms owned by the current admin
void create_admin_panel()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>ADMIN PANEL - My Rooms</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    // Action buttons at top
    GtkWidget *top_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *create_btn = gtk_button_new_with_label("CREATE ROOM");
    style_button(create_btn, "#27ae60");
    g_signal_connect(create_btn, "clicked", G_CALLBACK(on_admin_create_room_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(top_button_box), create_btn, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), top_button_box, FALSE, FALSE, 5);
    
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 5);

    // Stop broadcast listener so we can safely use blocking I/O for this request
    if (broadcast_is_listening()) {
        broadcast_stop_listener();
        usleep(50000); // Small delay to ensure timer callback is finished
    }
    
    // Force socket back to blocking mode for LIST_MY_ROOMS
    if (client.socket_fd > 0) {
        int flags = fcntl(client.socket_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
        }
    }
    
    // Flush old socket data to prevent stale responses
    flush_socket_buffer(client.socket_fd);
    
    // Request rooms từ server
    send_message("LIST_MY_ROOMS\n");
    char buffer[BUFFER_SIZE * 4];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    // Tạo scrolled window chứa list rooms
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Parse và hiển thị rooms
    if (n > 0 && strncmp(buffer, "LIST_MY_ROOMS_OK|", 17) == 0) {
        // Parse header: LIST_MY_ROOMS_OK|count
        int room_count_expected = 0;
        sscanf(buffer, "LIST_MY_ROOMS_OK|%d", &room_count_expected);
        
        char *saveptr1, *saveptr2;  // For strtok_r
        char buffer_copy[BUFFER_SIZE * 4];
        strncpy(buffer_copy, buffer, sizeof(buffer_copy) - 1);
        buffer_copy[sizeof(buffer_copy) - 1] = '\0';
        
        char *line = strtok_r(buffer_copy, "\n", &saveptr1);
        line = strtok_r(NULL, "\n", &saveptr1); // Bỏ qua header line

        int room_count = 0;
        while (line != NULL) {
            if (strncmp(line, "ROOM|", 5) == 0) {
                // Parse: ROOM|id|name|duration|status|question_info
                char *room_data = strdup(line);
                strtok_r(room_data, "|", &saveptr2); // Skip "ROOM"
                
                int room_id = atoi(strtok_r(NULL, "|", &saveptr2));
                char *room_name = strtok_r(NULL, "|", &saveptr2);
                int duration = atoi(strtok_r(NULL, "|", &saveptr2));
                char *status = strtok_r(NULL, "|", &saveptr2);
                char *question_info = strtok_r(NULL, "|", &saveptr2);

                // Tạo row cho room
                GtkWidget *row = gtk_list_box_row_new();
                GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_widget_set_margin_top(hbox, 10);
                gtk_widget_set_margin_bottom(hbox, 10);
                gtk_widget_set_margin_start(hbox, 15);
                gtk_widget_set_margin_end(hbox, 15);
                gtk_container_add(GTK_CONTAINER(row), hbox);

                // Room info
                char info_text[512];
                snprintf(info_text, sizeof(info_text),
                        "<b>%s</b> (ID: %d)\n"
                        "Duration: %d minutes | %s | Status: %s",
                        room_name, room_id, duration, question_info, status);
                
                GtkWidget *info_label = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(info_label), info_text);
                gtk_label_set_xalign(GTK_LABEL(info_label), 0);
                gtk_box_pack_start(GTK_BOX(hbox), info_label, TRUE, TRUE, 0);

                // Action buttons - hiển thị dựa trên status
                GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
                
                if (strcmp(status, "Started") == 0) {
                    // Room đã Started - chỉ hiển thị status, không có nút
                    GtkWidget *status_btn = gtk_button_new_with_label("STARTED");
                    style_button(status_btn, "#95a5a6");
                    gtk_widget_set_size_request(status_btn, 120, 40);
                    gtk_widget_set_sensitive(status_btn, FALSE);
                    gtk_box_pack_start(GTK_BOX(btn_box), status_btn, FALSE, FALSE, 0);
                } else if (strcmp(status, "Closed") == 0 || strcmp(status, "Waiting") == 0) {
                    // Room đang Closed/Waiting - hiển thị button OPEN
                    GtkWidget *open_btn = gtk_button_new_with_label("OPEN");
                    style_button(open_btn, "#27ae60");
                    gtk_widget_set_size_request(open_btn, 120, 40);
                    g_signal_connect(open_btn, "clicked", 
                                   G_CALLBACK(on_start_room_clicked), 
                                   GINT_TO_POINTER(room_id));
                    gtk_box_pack_start(GTK_BOX(btn_box), open_btn, FALSE, FALSE, 0);
                } else if (strcmp(status, "Open") == 0) {
                    // Room đang Open (shouldn't happen with new logic) - hiển thị CLOSE
                    GtkWidget *close_btn = gtk_button_new_with_label("CLOSE");
                    style_button(close_btn, "#e74c3c");
                    gtk_widget_set_size_request(close_btn, 120, 40);
                    g_signal_connect(close_btn, "clicked", 
                                   G_CALLBACK(on_close_room_clicked), 
                                   GINT_TO_POINTER(room_id));
                    gtk_box_pack_start(GTK_BOX(btn_box), close_btn, FALSE, FALSE, 0);
                }
                
                // Always show manage questions button
                GtkWidget *questions_btn = gtk_button_new_with_label("Questions");
                style_button(questions_btn, "#3498db");
                gtk_widget_set_size_request(questions_btn, 120, 40);
                g_signal_connect(questions_btn, "clicked", 
                               G_CALLBACK(show_exam_question_manager), 
                               GINT_TO_POINTER(room_id));
                gtk_box_pack_start(GTK_BOX(btn_box), questions_btn, FALSE, FALSE, 0);
                
                // View Results button - detailed exam results
                GtkWidget *results_btn = gtk_button_new_with_label("Results");
                style_button(results_btn, "#16a085");
                gtk_widget_set_size_request(results_btn, 120, 40);
                g_signal_connect(results_btn, "clicked", 
                               G_CALLBACK(on_view_exam_results_clicked), 
                               GINT_TO_POINTER(room_id));
                gtk_box_pack_start(GTK_BOX(btn_box), results_btn, FALSE, FALSE, 0);
                
                // Always show delete button
                GtkWidget *delete_btn = gtk_button_new_with_label("DELETE");
                style_button(delete_btn, "#c0392b");
                gtk_widget_set_size_request(delete_btn, 120, 40);
                g_signal_connect(delete_btn, "clicked", 
                               G_CALLBACK(on_delete_room_clicked), 
                               GINT_TO_POINTER(room_id));
                gtk_box_pack_start(GTK_BOX(btn_box), delete_btn, FALSE, FALSE, 0);
                
                gtk_box_pack_end(GTK_BOX(hbox), btn_box, FALSE, FALSE, 0);

                gtk_container_add(GTK_CONTAINER(list_box), row);
                room_count++;
                free(room_data);
            }
            line = strtok_r(NULL, "\n", &saveptr1);
        }

        if (room_count == 0) {
            GtkWidget *empty_label = gtk_label_new("You haven't created any rooms yet");
            gtk_widget_set_margin_top(empty_label, 50);
            gtk_widget_set_margin_bottom(empty_label, 50);
            gtk_container_add(GTK_CONTAINER(list_box), empty_label);
        }
    } else {
        GtkWidget *error_label = gtk_label_new("Failed to load rooms");
        gtk_widget_set_margin_top(error_label, 50);
        gtk_widget_set_margin_bottom(error_label, 50);
        gtk_container_add(GTK_CONTAINER(list_box), error_label);
    }

    // Back button
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}

void create_question_bank_screen()
{    // Check if user is admin
    if (strcmp(client.role, "admin") != 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "Permission Denied");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 "Only admin users can add questions.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        create_main_menu();
        return;
    }
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>QUESTION BANK</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *info = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info), "<span foreground='#27ae60' weight='bold'>Add new questions to your rooms:</span>");
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 0);

    GtkWidget *form_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(form_box, 10);
    gtk_widget_set_margin_end(form_box, 10);

    // **MỚI: Dropdown chọn room**
    GtkWidget *room_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(room_label), "<span foreground='#e74c3c' weight='bold'>Select Your Room (Exam or Practice):</span>");
    GtkWidget *room_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(form_box), room_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), room_combo, FALSE, FALSE, 0);

    // Request danh sách rooms từ server (both exam and practice)
    request_all_rooms_with_type(room_combo);

    GtkWidget *question_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(question_label), "<span foreground='#34495e'>Question:</span>");
    GtkWidget *question_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(question_entry), "Question text");
    gtk_box_pack_start(GTK_BOX(form_box), question_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), question_entry, FALSE, FALSE, 0);

    GtkWidget *options_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(options_label), "<span foreground='#34495e' weight='bold'>Answer Options:</span>");
    gtk_box_pack_start(GTK_BOX(form_box), options_label, FALSE, FALSE, 5);

    GtkWidget *opt1_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opt1_label), "<span foreground='#27ae60'>Option 1 (Correct ✓):</span>");
    GtkWidget *opt1_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(opt1_entry), "Correct answer");
    gtk_box_pack_start(GTK_BOX(form_box), opt1_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), opt1_entry, FALSE, FALSE, 0);

    GtkWidget *opt2_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opt2_label), "<span foreground='#34495e'>Option 2:</span>");
    GtkWidget *opt2_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(opt2_entry), "Wrong answer");
    gtk_box_pack_start(GTK_BOX(form_box), opt2_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), opt2_entry, FALSE, FALSE, 0);

    GtkWidget *opt3_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opt3_label), "<span foreground='#34495e'>Option 3:</span>");
    GtkWidget *opt3_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(opt3_entry), "Wrong answer");
    gtk_box_pack_start(GTK_BOX(form_box), opt3_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), opt3_entry, FALSE, FALSE, 0);

    GtkWidget *opt4_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opt4_label), "<span foreground='#34495e'>Option 4:</span>");
    GtkWidget *opt4_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(opt4_entry), "Wrong answer");
    gtk_box_pack_start(GTK_BOX(form_box), opt4_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), opt4_entry, FALSE, FALSE, 0);

    // Correct Answer Section
    GtkWidget *correct_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(correct_label), "<span foreground='#27ae60' weight='bold'>✓ Correct Answer:</span>");
    gtk_box_pack_start(GTK_BOX(form_box), correct_label, FALSE, FALSE, 5);
    
    GtkWidget *radio_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *correct_radio[4];
    const char *radio_labels[] = {"Option 1", "Option 2", "Option 3", "Option 4"};
    
    for (int i = 0; i < 4; i++) {
        if (i == 0) {
            correct_radio[i] = gtk_radio_button_new_with_label(NULL, radio_labels[i]);
        } else {
            correct_radio[i] = gtk_radio_button_new_with_label_from_widget(
                GTK_RADIO_BUTTON(correct_radio[0]), radio_labels[i]);
        }
        gtk_box_pack_start(GTK_BOX(radio_box), correct_radio[i], TRUE, TRUE, 0);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(correct_radio[0]), TRUE);
    gtk_box_pack_start(GTK_BOX(form_box), radio_box, FALSE, FALSE, 0);

    // Difficulty Dropdown
    GtkWidget *difficulty_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(difficulty_label), "<span foreground='#3498db' weight='bold'>Difficulty:</span>");
    gtk_box_pack_start(GTK_BOX(form_box), difficulty_label, FALSE, FALSE, 5);
    
    GtkWidget *difficulty_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(difficulty_combo), "Easy");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(difficulty_combo), "Medium");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(difficulty_combo), "Hard");
    gtk_combo_box_set_active(GTK_COMBO_BOX(difficulty_combo), 0);
    gtk_box_pack_start(GTK_BOX(form_box), difficulty_combo, FALSE, FALSE, 0);

    // Category Dropdown
    GtkWidget *category_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(category_label), "<span foreground='#9b59b6' weight='bold'>Category:</span>");
    gtk_box_pack_start(GTK_BOX(form_box), category_label, FALSE, FALSE, 5);
    
    GtkWidget *category_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(category_combo), "Math");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(category_combo), "Science");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(category_combo), "Geography");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(category_combo), "History");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(category_combo), "Literature");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(category_combo), "Art");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(category_combo), "Other");
    gtk_combo_box_set_active(GTK_COMBO_BOX(category_combo), 0);
    gtk_box_pack_start(GTK_BOX(form_box), category_combo, FALSE, FALSE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *submit_btn = gtk_button_new_with_label("ADD QUESTION");
    GtkWidget *import_btn = gtk_button_new_with_label("IMPORT CSV");
    GtkWidget *back_btn = gtk_button_new_with_label("BACK");
    style_button(submit_btn, "#27ae60");
    style_button(import_btn, "#9b59b6");
    style_button(back_btn, "#95a5a6");

    gtk_box_pack_start(GTK_BOX(button_box), submit_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), import_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), button_box, FALSE, FALSE, 5);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), form_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Struct để truyền data vào callback
    QuestionFormData *data = g_malloc(sizeof(QuestionFormData));
    data->room_combo = room_combo;
    data->question_entry = question_entry;
    data->opt1_entry = opt1_entry;
    data->opt2_entry = opt2_entry;
    data->opt3_entry = opt3_entry;
    data->opt4_entry = opt4_entry;
    data->difficulty_combo = difficulty_combo;
    data->category_combo = category_combo;
    for (int i = 0; i < 4; i++) {
        data->correct_radio[i] = correct_radio[i];
    }

    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_question), data);
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_import_csv_to_room), room_combo);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    
    show_view(vbox);
}

// Admin-specific create room handler
void on_admin_create_room_clicked(GtkWidget *widget, gpointer data)
{
    // Call the new exam room creator with question selection
    create_exam_room_with_questions(widget, data);
}

// Request all rooms (exam + practice) with type labels
void request_all_rooms_with_type(GtkWidget *room_combo) {
    // Clear existing items
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(room_combo));
    
    // Don't auto-select - leave empty for user to choose
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), "-1", "-- Select a Room --");
    
    // Flush old socket data
    flush_socket_buffer(client.socket_fd);
    
    // Get exam rooms
    char request[256];
    snprintf(request, sizeof(request), "GET_USER_ROOMS|%d\n", current_user_id);
    send_message(request);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        
        if (strstr(buffer, "ROOMS_LIST")) {
            char *token = strtok(buffer + 11, "|");
            while (token != NULL) {
                char *colon = strchr(token, ':');
                if (colon) {
                    *colon = '\0';
                    char *room_id = token;
                    char *room_name = colon + 1;
                    
                    // Add label to show it's an exam room
                    char display_name[256];
                    snprintf(display_name, sizeof(display_name), "[EXAM] %s", room_name);
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), room_id, display_name);
                }
                token = strtok(NULL, "|");
            }
        }
    }
    
    // Get practice rooms
    flush_socket_buffer(client.socket_fd);
    snprintf(request, sizeof(request), "GET_PRACTICE_ROOMS|%d\n", current_user_id);
    send_message(request);
    
    n = receive_message(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        
        if (strstr(buffer, "PRACTICE_ROOMS_LIST")) {
            char *token = strtok(buffer + 20, "|");
            while (token != NULL) {
                char *colon = strchr(token, ':');
                if (colon) {
                    *colon = '\0';
                    // Use practice_id prefixed with 'P' to distinguish from exam room ids
                    char practice_id[16];
                    snprintf(practice_id, sizeof(practice_id), "P%s", token);
                    char *room_name = colon + 1;
                    
                    // Add label to show it's a practice room
                    char display_name[256];
                    snprintf(display_name, sizeof(display_name), "[PRACTICE] %s", room_name);
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), practice_id, display_name);
                }
                token = strtok(NULL, "|");
            }
        }
    }
    
    // Set to the placeholder (don't auto-select)
    gtk_combo_box_set_active(GTK_COMBO_BOX(room_combo), 0);
}

// View exam results with detailed GTK interface
void on_view_exam_results_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    // Flush old socket data
    flush_socket_buffer(client.socket_fd);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "GET_ROOM_MEMBERS|%d\n", room_id);
    send_message(msg);
    
    char recv_buf[BUFFER_SIZE * 2];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n <= 0 || strncmp(recv_buf, "ROOM_MEMBERS|", 13) != 0) {
        show_error_dialog("Failed to load exam results");
        return;
    }
    
    // Parse: ROOM_MEMBERS|room_id|count|user_id:username:score;...
    char *response_data = recv_buf + 13;
    char *room_id_str = strtok(response_data, "|");
    char *count_str = strtok(NULL, "|");
    char *members_data = strtok(NULL, "|");
    
    int count = atoi(count_str ? count_str : "0");
    (void)room_id_str; // Suppress unused warning
    
    // Create new window for results
    GtkWidget *results_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(results_window), "Exam Results");
    gtk_window_set_default_size(GTK_WINDOW(results_window), 700, 500);
    gtk_window_set_position(GTK_WINDOW(results_window), GTK_WIN_POS_CENTER);
    gtk_window_set_transient_for(GTK_WINDOW(results_window), GTK_WINDOW(main_window));
    gtk_window_set_modal(GTK_WINDOW(results_window), TRUE);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 15);
    gtk_widget_set_margin_start(vbox, 15);
    gtk_widget_set_margin_end(vbox, 15);
    gtk_widget_set_margin_bottom(vbox, 15);
    gtk_container_add(GTK_CONTAINER(results_window), vbox);
    
    // Title
    GtkWidget *title = gtk_label_new(NULL);
    char title_markup[256];
    snprintf(title_markup, sizeof(title_markup), 
             "<span foreground='#2c3e50' weight='bold' size='18000'>Exam Results - Room #%d</span>", 
             room_id);
    gtk_label_set_markup(GTK_LABEL(title), title_markup);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 5);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 5);
    
    // Statistics summary
    GtkWidget *stats_label = gtk_label_new(NULL);
    char stats_text[256];
    snprintf(stats_text, sizeof(stats_text), 
             "<span foreground='#27ae60' weight='bold'>Total Participants: %d</span>", count);
    gtk_label_set_markup(GTK_LABEL(stats_label), stats_text);
    gtk_box_pack_start(GTK_BOX(vbox), stats_label, FALSE, FALSE, 5);
    
    // Scrolled window for list
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    // Create list box
    GtkWidget *list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), list_box);
    
    // Add header row
    GtkWidget *header_row = gtk_list_box_row_new();
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(header_box, 10);
    gtk_widget_set_margin_bottom(header_box, 10);
    gtk_widget_set_margin_start(header_box, 15);
    gtk_widget_set_margin_end(header_box, 15);
    
    GtkWidget *header_rank = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_rank), "<b>Rank</b>");
    gtk_widget_set_size_request(header_rank, 60, -1);
    gtk_label_set_xalign(GTK_LABEL(header_rank), 0.0);
    
    GtkWidget *header_name = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_name), "<b>Student Name</b>");
    gtk_widget_set_size_request(header_name, 250, -1);
    gtk_label_set_xalign(GTK_LABEL(header_name), 0.0);
    
    GtkWidget *header_score = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_score), "<b>Score</b>");
    gtk_widget_set_size_request(header_score, 100, -1);
    gtk_label_set_xalign(GTK_LABEL(header_score), 0.0);
    
    GtkWidget *header_status = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_status), "<b>Status</b>");
    gtk_widget_set_size_request(header_status, 150, -1);
    gtk_label_set_xalign(GTK_LABEL(header_status), 0.0);
    
    gtk_box_pack_start(GTK_BOX(header_box), header_rank, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_box), header_name, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header_box), header_score, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_box), header_status, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(header_row), header_box);
    gtk_list_box_insert(GTK_LIST_BOX(list_box), header_row, -1);
    
    // Parse and display results
    if (count > 0 && members_data && strcmp(members_data, "NONE\n") != 0) {
        char *token = strtok(members_data, ";");
        int rank = 1;
        
        while (token != NULL) {
            int user_id, score;
            char username[50];
            
            if (sscanf(token, "%d:%49[^:]:%d", &user_id, username, &score) == 3) {
                GtkWidget *row = gtk_list_box_row_new();
                GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_widget_set_margin_top(row_box, 8);
                gtk_widget_set_margin_bottom(row_box, 8);
                gtk_widget_set_margin_start(row_box, 15);
                gtk_widget_set_margin_end(row_box, 15);
                
                // Rank
                GtkWidget *rank_label = gtk_label_new(NULL);
                char rank_text[32];
                if (score >= 0) {
                    snprintf(rank_text, sizeof(rank_text), "#%d", rank++);
                } else {
                    snprintf(rank_text, sizeof(rank_text), "-");
                }
                gtk_label_set_text(GTK_LABEL(rank_label), rank_text);
                gtk_widget_set_size_request(rank_label, 60, -1);
                gtk_label_set_xalign(GTK_LABEL(rank_label), 0.0);
                
                // Username
                GtkWidget *name_label = gtk_label_new(username);
                gtk_widget_set_size_request(name_label, 250, -1);
                gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
                
                // Score
                GtkWidget *score_label = gtk_label_new(NULL);
                char score_markup[128];
                if (score >= 0) {
                    const char *color = score >= 80 ? "#27ae60" : (score >= 50 ? "#f39c12" : "#e74c3c");
                    snprintf(score_markup, sizeof(score_markup), 
                             "<span foreground='%s' weight='bold' size='12000'>%d</span>", 
                             color, score);
                } else {
                    snprintf(score_markup, sizeof(score_markup), 
                             "<span foreground='#95a5a6'>-</span>");
                }
                gtk_label_set_markup(GTK_LABEL(score_label), score_markup);
                gtk_widget_set_size_request(score_label, 100, -1);
                gtk_label_set_xalign(GTK_LABEL(score_label), 0.0);
                
                // Status
                GtkWidget *status_label = gtk_label_new(NULL);
                char status_markup[128];
                if (score >= 0) {
                    snprintf(status_markup, sizeof(status_markup), 
                             "<span foreground='#27ae60'>Đã thi</span>");
                } else {
                    snprintf(status_markup, sizeof(status_markup), 
                             "<span foreground='#95a5a6'>Chưa thi</span>");
                }
                gtk_label_set_markup(GTK_LABEL(status_label), status_markup);
                gtk_widget_set_size_request(status_label, 150, -1);
                gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);
                
                gtk_box_pack_start(GTK_BOX(row_box), rank_label, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(row_box), name_label, TRUE, TRUE, 0);
                gtk_box_pack_start(GTK_BOX(row_box), score_label, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(row_box), status_label, FALSE, FALSE, 0);
                
                gtk_container_add(GTK_CONTAINER(row), row_box);
                gtk_list_box_insert(GTK_LIST_BOX(list_box), row, -1);
            }
            
            token = strtok(NULL, ";");
        }
    } else {
        GtkWidget *empty_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(empty_label), 
                            "<span foreground='#95a5a6' size='12000'>No participants yet</span>");
        gtk_widget_set_margin_top(empty_label, 50);
        gtk_widget_set_margin_bottom(empty_label, 50);
        gtk_box_pack_start(GTK_BOX(vbox), empty_label, TRUE, TRUE, 0);
    }
    
    // Close button
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *close_btn = gtk_button_new_with_label("Close");
    style_button(close_btn, "#95a5a6");
    gtk_box_pack_end(GTK_BOX(button_box), close_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);
    
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_widget_destroy), results_window);
    g_signal_connect(results_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &results_window);
    
    gtk_widget_show_all(results_window);
}
