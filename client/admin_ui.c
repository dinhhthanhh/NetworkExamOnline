#include "admin_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

void request_user_rooms(GtkWidget *room_combo)
{
    // Clear existing items first
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(room_combo));
    
    // Add placeholder item
    gtk_combo_box_text_prepend(GTK_COMBO_BOX_TEXT(room_combo), "-1", "-- Select a room --");
    
    // Flush old socket data to prevent stale responses
    flush_socket_buffer(client.socket_fd);
    
    char request[256];
    snprintf(request, sizeof(request), "GET_USER_ROOMS|%d\n", current_user_id);
    send_message(request);
    
    // Receive response and populate combo box
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        printf("[DEBUG] GET_USER_ROOMS response: %s\n", buffer);
        
        // Parse response format: ROOMS_LIST|room_id1:room_name1:is_practice1|room_id2:room_name2:is_practice2|...
        if (strstr(buffer, "ROOMS_LIST")) {
            char *data_start = buffer + 11; // Skip "ROOMS_LIST|"
            int room_count = 0;
            
            // Parse manually without strtok to avoid buffer issues
            char *current = data_start;
            while (*current != '\0' && *current != '\n') {
                // Find the next pipe or end
                char room_entry[300];
                int entry_len = 0;
                while (*current != '|' && *current != '\0' && *current != '\n' && entry_len < 299) {
                    room_entry[entry_len++] = *current++;
                }
                room_entry[entry_len] = '\0';
                
                if (entry_len > 0) {
                    // Parse room_id:room_name:is_practice
                    char *first_colon = strchr(room_entry, ':');
                    if (first_colon) {
                        *first_colon = '\0';
                        char *room_id_str = room_entry;
                        char *room_name = first_colon + 1;
                        
                        char *second_colon = strchr(room_name, ':');
                        int is_practice = 0;
                        if (second_colon) {
                            *second_colon = '\0';
                            is_practice = atoi(second_colon + 1);
                        }
                        
                        // Create display name with room type
                        char display_name[300];
                        snprintf(display_name, sizeof(display_name), "%s [%s]", 
                                 room_name, is_practice ? "Practice" : "Exam");
                        
                        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), 
                                                 room_id_str, display_name);
                        room_count++;
                        printf("[DEBUG] Added room: %s (ID: %s, Type: %s)\n", 
                               room_name, room_id_str, is_practice ? "Practice" : "Exam");
                    }
                }
                
                // Skip the pipe separator
                if (*current == '|') current++;
            }
            
            if (room_count > 0) {
                // Don't auto-select, show placeholder
                gtk_combo_box_set_active(GTK_COMBO_BOX(room_combo), 0);
                gtk_widget_set_sensitive(room_combo, TRUE);
                printf("[DEBUG] Loaded %d rooms into dropdown\n", room_count);
            } else {
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), "-2", "No rooms available");
                gtk_combo_box_set_active(GTK_COMBO_BOX(room_combo), 0);
                gtk_widget_set_sensitive(room_combo, FALSE);
                printf("[DEBUG] No rooms found\n");
            }
        } else if (strstr(buffer, "NO_ROOMS")) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), "-2", "No rooms available");
            gtk_combo_box_set_active(GTK_COMBO_BOX(room_combo), 0);
            gtk_widget_set_sensitive(room_combo, FALSE);
        }
    } else {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), "-2", "Failed to load rooms");
        gtk_combo_box_set_active(GTK_COMBO_BOX(room_combo), 0);
        gtk_widget_set_sensitive(room_combo, FALSE);
    }
}

void on_import_csv_to_room(GtkWidget *widget, gpointer user_data)
{
    GtkWidget *room_combo = GTK_WIDGET(user_data);
    
    // Lấy room_id từ dropdown
    const char *room_id_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(room_combo));
    if (!room_id_str || atoi(room_id_str) == -1) {
        show_error_dialog("⚠️ Please select a valid room first!");
        return;
    }
    
    int room_id = atoi(room_id_str);
    
    // Tạo file chooser dialog
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "📂 Select CSV File to Import",
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
                show_error_dialog("❌ Cannot open file!");
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
                show_error_dialog("❌ File too large or empty (max 5MB)!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            // Read file into buffer
            char *file_buffer = malloc(file_size);
            if (!file_buffer) {
                fclose(fp);
                show_error_dialog("❌ Memory allocation failed!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            size_t read_bytes = fread(file_buffer, 1, file_size, fp);
            fclose(fp);
            
            if (read_bytes != file_size) {
                free(file_buffer);
                show_error_dialog("❌ Failed to read file!");
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
                "⏳ Uploading and importing CSV...\n\nFile: %s (%ld bytes)\nPlease wait...",
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
                show_error_dialog("❌ Server not ready to receive file!");
                g_free(filepath);
                gtk_widget_destroy(dialog);
                return;
            }
            
            // Gửi file content
            ssize_t sent = send(client.socket_fd, file_buffer, file_size, 0);
            free(file_buffer);
            
            if (sent != file_size) {
                gtk_widget_destroy(loading_dialog);
                show_error_dialog("❌ Failed to upload file!");
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
                                "✅ Import successful!\n\n"
                                "📝 Imported %d question(s) to this room.\n\n"
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
                            "❌ Import failed!\n\n"
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
                    "❌ No response from server!\n\nPlease check connection.");
                gtk_dialog_run(GTK_DIALOG(error_dialog));
                gtk_widget_destroy(error_dialog);
            }
            
            g_free(filepath);
        }
    }
    
    gtk_widget_destroy(dialog);
}

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
            show_success_dialog("✅ Question added successfully!");
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
                                       "✅ Room opened successfully!");
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
                                       "❌ %s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

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
                                       "✅ Room closed successfully!");
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

void on_delete_room_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    // Confirm dialog with strong warning
    GtkWidget *confirm_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       GTK_MESSAGE_WARNING,
                                                       GTK_BUTTONS_YES_NO,
                                                       "Permanently DELETE this room?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(confirm_dialog),
                                             "This will DELETE the room and ALL its questions permanently!\n"
                                             "This action CANNOT be undone.");
    
    int response = gtk_dialog_run(GTK_DIALOG(confirm_dialog));
    gtk_widget_destroy(confirm_dialog);
    
    if (response != GTK_RESPONSE_YES) {
        return;
    }
    
    char msg[64];
    snprintf(msg, sizeof(msg), "DELETE_ROOM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    GtkWidget *dialog;
    if (n > 0 && strncmp(buffer, "DELETE_ROOM_OK", 14) == 0) {
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_OK,
                                       "Room deleted successfully!");
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
            error_msg = "Failed to delete room";
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

// Update room settings (question counts and difficulty ratio)
void on_update_room_settings_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    // Request current room settings from server
    char request[64];
    snprintf(request, sizeof(request), "GET_ROOM_SETTINGS|%d\n", room_id);
    send_message(request);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    int current_easy = 0, current_medium = 0, current_hard = 0;
    if (n > 0 && strncmp(buffer, "ROOM_SETTINGS_OK", 16) == 0) {
        // Parse: ROOM_SETTINGS_OK|easy|medium|hard
        char *token = strtok(buffer, "|");
        token = strtok(NULL, "|");
        if (token) current_easy = atoi(token);
        token = strtok(NULL, "|");
        if (token) current_medium = atoi(token);
        token = strtok(NULL, "|\n");
        if (token) current_hard = atoi(token);
    }
    
    // Create dialog
    GtkWidget *dialog = gtk_dialog_new_with_buttons("⚙️ Update Room Settings",
                                                     GTK_WINDOW(main_window),
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "Cancel", GTK_RESPONSE_CANCEL,
                                                     "Update", GTK_RESPONSE_ACCEPT,
                                                     NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    gtk_container_add(GTK_CONTAINER(content_area), grid);

    // Info label
    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label), 
        "<b>Question Distribution Settings</b>");
    gtk_widget_set_halign(info_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), info_label, 0, 0, 2, 1);

    // Easy questions
    GtkWidget *easy_label = gtk_label_new("Easy Questions:");
    GtkWidget *easy_spin = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(easy_spin), current_easy);
    gtk_grid_attach(GTK_GRID(grid), easy_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), easy_spin, 1, 1, 1, 1);

    // Medium questions
    GtkWidget *medium_label = gtk_label_new("Medium Questions:");
    GtkWidget *medium_spin = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(medium_spin), current_medium);
    gtk_grid_attach(GTK_GRID(grid), medium_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), medium_spin, 1, 2, 1, 1);

    // Hard questions
    GtkWidget *hard_label = gtk_label_new("Hard Questions:");
    GtkWidget *hard_spin = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(hard_spin), current_hard);
    gtk_grid_attach(GTK_GRID(grid), hard_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), hard_spin, 1, 3, 1, 1);

    // Help text
    GtkWidget *help_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(help_label), 
        "<span foreground='#7f8c8d' size='small'>\n"
        "Total exam questions = Easy + Medium + Hard\n"
        "Questions will be randomly selected from your pool\n"
        "based on these counts when students begin the exam.</span>");
    gtk_label_set_line_wrap(GTK_LABEL(help_label), TRUE);
    gtk_widget_set_halign(help_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), help_label, 0, 4, 2, 1);

    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT)
    {
        int easy_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(easy_spin));
        int medium_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(medium_spin));
        int hard_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(hard_spin));

        // Send update request
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "UPDATE_ROOM_SETTINGS|%d|%d|%d|%d\n", 
                 room_id, easy_count, medium_count, hard_count);
        send_message(cmd);

        char update_buffer[BUFFER_SIZE];
        ssize_t update_n = receive_message(update_buffer, sizeof(update_buffer));

        GtkWidget *result_dialog;
        if (update_n > 0 && strncmp(update_buffer, "UPDATE_SETTINGS_OK", 18) == 0) {
            result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   "✅ Room settings updated!");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                     "Total questions: %d (Easy:%d, Medium:%d, Hard:%d)",
                                                     easy_count + medium_count + hard_count,
                                                     easy_count, medium_count, hard_count);
            gtk_dialog_run(GTK_DIALOG(result_dialog));
            gtk_widget_destroy(result_dialog);
            
            // Refresh admin panel
            create_admin_panel();
        } else {
            result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "❌ Failed to update settings!");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                     "Server response: %s", update_buffer);
            gtk_dialog_run(GTK_DIALOG(result_dialog));
            gtk_widget_destroy(result_dialog);
        }
    }
    gtk_widget_destroy(dialog);
}

void create_admin_panel()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>⚙️ ADMIN PANEL - My Rooms</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    // Request rooms từ server
    send_message("LIST_MY_ROOMS\n");
    char buffer[BUFFER_SIZE];
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
    if (n > 0 && strncmp(buffer, "LIST_MY_ROOMS_OK", 16) == 0) {
        char *saveptr1, *saveptr2;  // For strtok_r
        char *line = strtok_r(buffer, "\n", &saveptr1);
        line = strtok_r(NULL, "\n", &saveptr1); // Bỏ qua header line

        int room_count = 0;
        while (line != NULL) {
            if (strncmp(line, "ROOM|", 5) == 0) {
                // Parse: ROOM|id|name|duration|status|question_info|is_practice
                char *room_data = strdup(line);
                strtok_r(room_data, "|", &saveptr2); // Skip "ROOM"
                
                int room_id = atoi(strtok_r(NULL, "|", &saveptr2));
                char *room_name = strtok_r(NULL, "|", &saveptr2);
                int duration = atoi(strtok_r(NULL, "|", &saveptr2));
                char *status = strtok_r(NULL, "|", &saveptr2);
                char *question_info = strtok_r(NULL, "|", &saveptr2);
                int is_practice = atoi(strtok_r(NULL, "|\n", &saveptr2));

                // Tạo row cho room
                GtkWidget *row = gtk_list_box_row_new();
                GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_widget_set_margin_top(hbox, 10);
                gtk_widget_set_margin_bottom(hbox, 10);
                gtk_widget_set_margin_start(hbox, 15);
                gtk_widget_set_margin_end(hbox, 15);
                gtk_container_add(GTK_CONTAINER(row), hbox);

                // Room info with type indicator
                char info_text[512];
                snprintf(info_text, sizeof(info_text),
                        "<b>%s</b> %s (ID: %d)\n"
                        "⏱️ Duration: %d minutes | 📝 %s | Status: %s",
                        room_name, is_practice ? "[Practice]" : "[Exam]", room_id, duration, question_info, status);
                
                GtkWidget *info_label = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(info_label), info_text);
                gtk_label_set_xalign(GTK_LABEL(info_label), 0);
                gtk_box_pack_start(GTK_BOX(hbox), info_label, TRUE, TRUE, 0);

                // Action buttons - display based on status and type
                GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
                
                if (is_practice == 1) {
                    // Practice room - only show CLOSE (if started) or status
                    if (strcmp(status, "Started") == 0) {
                        GtkWidget *close_btn = gtk_button_new_with_label("CLOSE");
                        style_button(close_btn, "#e74c3c");
                        gtk_widget_set_size_request(close_btn, 100, 40);
                        g_signal_connect(close_btn, "clicked", 
                                       G_CALLBACK(on_close_room_clicked), 
                                       GINT_TO_POINTER(room_id));
                        gtk_box_pack_start(GTK_BOX(btn_box), close_btn, FALSE, FALSE, 0);
                    } else {
                        // Show status only
                        GtkWidget *status_btn = gtk_button_new_with_label(strcmp(status, "Waiting") == 0 ? "OPEN" : status);
                        style_button(status_btn, "#95a5a6");
                        gtk_widget_set_size_request(status_btn, 100, 40);
                        gtk_widget_set_sensitive(status_btn, FALSE);
                        gtk_box_pack_start(GTK_BOX(btn_box), status_btn, FALSE, FALSE, 0);
                    }
                } else {
                    // Exam room - show START/CLOSE based on status
                    if (strcmp(status, "Started") == 0) {
                        // Room is Started - show status only, no action button
                        GtkWidget *status_btn = gtk_button_new_with_label("STARTED");
                        style_button(status_btn, "#95a5a6");
                        gtk_widget_set_size_request(status_btn, 100, 40);
                        gtk_widget_set_sensitive(status_btn, FALSE);
                        gtk_box_pack_start(GTK_BOX(btn_box), status_btn, FALSE, FALSE, 0);
                    } else if (strcmp(status, "Closed") == 0 || strcmp(status, "Waiting") == 0) {
                        // Room is Closed/Waiting - show OPEN button
                        GtkWidget *open_btn = gtk_button_new_with_label("OPEN");
                        style_button(open_btn, "#27ae60");
                        gtk_widget_set_size_request(open_btn, 100, 40);
                        g_signal_connect(open_btn, "clicked", 
                                       G_CALLBACK(on_start_room_clicked), 
                                       GINT_TO_POINTER(room_id));
                        gtk_box_pack_start(GTK_BOX(btn_box), open_btn, FALSE, FALSE, 0);
                    } else if (strcmp(status, "Open") == 0) {
                        // Room is Open (shouldn't happen with new logic) - show CLOSE
                        GtkWidget *close_btn = gtk_button_new_with_label("CLOSE");
                        style_button(close_btn, "#e74c3c");
                        gtk_widget_set_size_request(close_btn, 100, 40);
                        g_signal_connect(close_btn, "clicked", 
                                       G_CALLBACK(on_close_room_clicked), 
                                       GINT_TO_POINTER(room_id));
                        gtk_box_pack_start(GTK_BOX(btn_box), close_btn, FALSE, FALSE, 0);
                    }
                }
                
                // DELETE button - always shown, red color
                GtkWidget *delete_btn = gtk_button_new_with_label("DELETE");
                style_button(delete_btn, "#c0392b");
                gtk_widget_set_size_request(delete_btn, 100, 40);
                g_signal_connect(delete_btn, "clicked", 
                               G_CALLBACK(on_delete_room_clicked), 
                               GINT_TO_POINTER(room_id));
                gtk_box_pack_start(GTK_BOX(btn_box), delete_btn, FALSE, FALSE, 0);
                
                // STATS button - always shown for monitoring
                GtkWidget *stats_btn = gtk_button_new_with_label("STATS");
                style_button(stats_btn, "#3498db");
                gtk_widget_set_size_request(stats_btn, 100, 40);
                g_signal_connect(stats_btn, "clicked", 
                               G_CALLBACK(on_view_room_stats_clicked), 
                               GINT_TO_POINTER(room_id));
                gtk_box_pack_start(GTK_BOX(btn_box), stats_btn, FALSE, FALSE, 0);
                
                // SETTINGS button - only for exam rooms (not practice)
                if (is_practice == 0) {
                    GtkWidget *settings_btn = gtk_button_new_with_label("SETTINGS");
                    style_button(settings_btn, "#9b59b6");
                    gtk_widget_set_size_request(settings_btn, 100, 40);
                    g_signal_connect(settings_btn, "clicked", 
                                   G_CALLBACK(on_update_room_settings_clicked), 
                                   GINT_TO_POINTER(room_id));
                    gtk_box_pack_start(GTK_BOX(btn_box), settings_btn, FALSE, FALSE, 0);
                }
                
                gtk_box_pack_end(GTK_BOX(hbox), btn_box, FALSE, FALSE, 0);

                gtk_container_add(GTK_CONTAINER(list_box), row);
                room_count++;
                free(room_data);
            }
            line = strtok_r(NULL, "\n", &saveptr1);
        }

        if (room_count == 0) {
            GtkWidget *empty_label = gtk_label_new("📭 You haven't created any rooms yet");
            gtk_widget_set_margin_top(empty_label, 50);
            gtk_widget_set_margin_bottom(empty_label, 50);
            gtk_container_add(GTK_CONTAINER(list_box), empty_label);
        }
    } else {
        GtkWidget *error_label = gtk_label_new("❌ Failed to load rooms");
        gtk_widget_set_margin_top(error_label, 50);
        gtk_widget_set_margin_bottom(error_label, 50);
        gtk_container_add(GTK_CONTAINER(list_box), error_label);
    }

    // Back button
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
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
                                                   "❌ Permission Denied");
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
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>📚 QUESTION BANK</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *info = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info), "<span foreground='#27ae60' weight='bold'>✏️ Add new questions to your rooms:</span>");
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 0);

    GtkWidget *form_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(form_box, 10);
    gtk_widget_set_margin_end(form_box, 10);

    // **MỚI: Dropdown chọn room**
    GtkWidget *room_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(room_label), "<span foreground='#e74c3c' weight='bold'>Select Your Room:</span>");
    GtkWidget *room_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(form_box), room_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), room_combo, FALSE, FALSE, 0);

    // Request danh sách rooms từ server
    request_user_rooms(room_combo);

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
    gtk_label_set_markup(GTK_LABEL(difficulty_label), "<span foreground='#3498db' weight='bold'>📊 Difficulty:</span>");
    gtk_box_pack_start(GTK_BOX(form_box), difficulty_label, FALSE, FALSE, 5);
    
    GtkWidget *difficulty_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(difficulty_combo), "Easy");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(difficulty_combo), "Medium");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(difficulty_combo), "Hard");
    gtk_combo_box_set_active(GTK_COMBO_BOX(difficulty_combo), 0);
    gtk_box_pack_start(GTK_BOX(form_box), difficulty_combo, FALSE, FALSE, 0);

    // Category Dropdown
    GtkWidget *category_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(category_label), "<span foreground='#9b59b6' weight='bold'>📚 Category:</span>");
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
    GtkWidget *submit_btn = gtk_button_new_with_label("✅ ADD QUESTION");
    GtkWidget *import_btn = gtk_button_new_with_label("📥 IMPORT CSV");
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
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

void on_csv_file_selected(GtkWidget *widget, gpointer data)
{
    GtkWidget *file_chooser = GTK_WIDGET(data);
    char *filepath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_chooser));
    
    if (!filepath) {
        show_error_dialog("No file selected!");
        return;
    }
    
    // Note: This screen doesn't have room selection, so we use room_id=0 (general pool)
    // For room-specific import, use Question Bank screen instead
    int room_id = 0;
    
    // Read file content
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        show_error_dialog("❌ Cannot open file!");
        g_free(filepath);
        return;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Validate file size
    if (file_size <= 0 || file_size > 5 * 1024 * 1024) {
        fclose(fp);
        show_error_dialog("❌ File too large or empty (max 5MB)!");
        g_free(filepath);
        return;
    }
    
    // Read file into buffer
    char *file_buffer = malloc(file_size);
    if (!file_buffer) {
        fclose(fp);
        show_error_dialog("❌ Memory allocation failed!");
        g_free(filepath);
        return;
    }
    
    size_t read_bytes = fread(file_buffer, 1, file_size, fp);
    fclose(fp);
    
    if (read_bytes != file_size) {
        free(file_buffer);
        show_error_dialog("❌ Failed to read file!");
        g_free(filepath);
        return;
    }
    
    // Extract filename only
    char *filename = strrchr(filepath, G_DIR_SEPARATOR);
    if (!filename) filename = strrchr(filepath, '/');
    if (!filename) filename = strrchr(filepath, '\\');
    filename = filename ? filename + 1 : filepath;
    
    // Show loading dialog
    GtkWidget *loading_dialog = gtk_message_dialog_new(
        GTK_WINDOW(main_window),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        "⏳ Uploading and importing CSV...\\n\\nFile: %s (%ld bytes)\\nPlease wait...",
        filename, file_size);
    gtk_widget_show_all(loading_dialog);
    
    while (gtk_events_pending())
        gtk_main_iteration();
    
    // Send header: IMPORT_CSV|room_id|filename|file_size
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
        show_error_dialog("❌ Server not ready to receive file!");
        g_free(filepath);
        return;
    }
    
    // Send file content
    ssize_t sent = send(client.socket_fd, file_buffer, file_size, 0);
    free(file_buffer);
    
    if (sent != file_size) {
        gtk_widget_destroy(loading_dialog);
        show_error_dialog("❌ Failed to upload file!");
        g_free(filepath);
        return;
    }
    
    // Receive response
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    gtk_widget_destroy(loading_dialog);
    
    GtkWidget *result_dialog;
    if (n > 0 && strstr(buffer, "IMPORT_OK")) {
        int count = 0;
        if (sscanf(buffer, "IMPORT_OK|%d", &count) == 1) {
            char msg[256];
            snprintf(msg, sizeof(msg), "✅ Successfully imported %d questions!", count);
            result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_INFO,
                                                 GTK_BUTTONS_OK,
                                                 "%s", msg);
        } else {
            result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_INFO,
                                                 GTK_BUTTONS_OK,
                                                 "✅ Import successful!");
        }
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "❌ Import failed!\n\n%.200s", 
                 n > 0 ? buffer : "No response from server");
        result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                             GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_OK,
                                             "%s", error_msg);
    }
    
    gtk_dialog_run(GTK_DIALOG(result_dialog));
    gtk_widget_destroy(result_dialog);
    g_free(filepath);
}

void create_csv_import_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top(vbox, 30);
    gtk_widget_set_margin_start(vbox, 30);
    gtk_widget_set_margin_end(vbox, 30);

    // Title
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span foreground='#2c3e50' weight='bold' size='22000'>📥 IMPORT QUESTIONS FROM CSV</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    // Instructions
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(info_box, 15);
    gtk_widget_set_margin_bottom(info_box, 15);
    
    GtkWidget *info1 = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info1), 
        "<span foreground='#27ae60' weight='bold' size='11000'>📋 CSV File Format:</span>");
    gtk_label_set_xalign(GTK_LABEL(info1), 0);
    gtk_box_pack_start(GTK_BOX(info_box), info1, FALSE, FALSE, 0);
    
    GtkWidget *info2 = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info2),
        "<span font='monospace' size='9000' foreground='#34495e'>"
        "question_text,option_a,option_b,option_c,option_d,correct_answer,difficulty,category"
        "</span>");
    gtk_label_set_xalign(GTK_LABEL(info2), 0);
    gtk_widget_set_margin_start(info2, 20);
    gtk_box_pack_start(GTK_BOX(info_box), info2, FALSE, FALSE, 0);
    
    GtkWidget *info3 = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info3),
        "<span size='10000' foreground='#7f8c8d'>"
        "• <b>correct_answer</b>: 0=A, 1=B, 2=C, 3=D\n"
        "• <b>difficulty</b>: Easy, Medium, Hard\n"
        "• <b>category</b>: Math, Science, History, etc."
        "</span>");
    gtk_label_set_xalign(GTK_LABEL(info3), 0);
    gtk_widget_set_margin_start(info3, 20);
    gtk_widget_set_margin_top(info3, 5);
    gtk_box_pack_start(GTK_BOX(info_box), info3, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), info_box, FALSE, FALSE, 0);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 0);

    // Example
    GtkWidget *example_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(example_label),
        "<span foreground='#3498db' weight='bold'>💡 Example:</span>");
    gtk_label_set_xalign(GTK_LABEL(example_label), 0);
    gtk_box_pack_start(GTK_BOX(vbox), example_label, FALSE, FALSE, 0);
    
    GtkWidget *example_text = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(example_text),
        "<span font='monospace' size='9000' foreground='#2c3e50'>"
        "What is 2+2?,2,3,4,5,2,Easy,Math\n"
        "Capital of France?,London,Paris,Berlin,Madrid,1,Easy,Geography"
        "</span>");
    gtk_label_set_xalign(GTK_LABEL(example_text), 0);
    gtk_widget_set_margin_start(example_text, 20);
    gtk_widget_set_margin_top(example_text, 5);
    gtk_widget_set_margin_bottom(example_text, 15);
    gtk_box_pack_start(GTK_BOX(vbox), example_text, FALSE, FALSE, 0);

    // File chooser
    GtkWidget *file_chooser = gtk_file_chooser_button_new(
        "Select CSV File", 
        GTK_FILE_CHOOSER_ACTION_OPEN
    );
    
    // Add CSV filter
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "CSV Files");
    gtk_file_filter_add_pattern(filter, "*.csv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_chooser), filter);
    
    GtkFileFilter *filter_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_all, "All Files");
    gtk_file_filter_add_pattern(filter_all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_chooser), filter_all);
    
    gtk_widget_set_size_request(file_chooser, -1, 50);
    gtk_box_pack_start(GTK_BOX(vbox), file_chooser, FALSE, FALSE, 5);

    // Buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(button_box, 20);
    
    GtkWidget *import_btn = gtk_button_new_with_label("📥 IMPORT NOW");
    style_button(import_btn, "#27ae60");
    gtk_widget_set_size_request(import_btn, -1, 45);
    
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_widget_set_size_request(back_btn, -1, 45);
    
    gtk_box_pack_start(GTK_BOX(button_box), import_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    // Signals
    g_signal_connect(import_btn, "clicked", 
                    G_CALLBACK(on_csv_file_selected), file_chooser);
    g_signal_connect(back_btn, "clicked", 
                    G_CALLBACK(create_main_menu), NULL);
    
    show_view(vbox);
}

// Tạo Practice Room
void on_admin_create_practice_room_clicked(GtkWidget *widget, gpointer data)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("🎯 Create Practice Room",
                                                     GTK_WINDOW(main_window),
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "Cancel", GTK_RESPONSE_CANCEL,
                                                     "Create", GTK_RESPONSE_ACCEPT,
                                                     NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(content_area), grid);

    // Room name
    GtkWidget *name_label = gtk_label_new("Practice Room Name:");
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "e.g., Math Practice");
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);

    // Time limit (0 = unlimited)
    GtkWidget *time_label = gtk_label_new("Time Limit (0=unlimited):");
    GtkWidget *time_spin = gtk_spin_button_new_with_range(0, 180, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_spin), 0);
    gtk_grid_attach(GTK_GRID(grid), time_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), time_spin, 1, 1, 1, 1);

    // Show answer checkbox
    GtkWidget *show_answer_check = gtk_check_button_new_with_label("Show correct answer immediately");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show_answer_check), TRUE);
    gtk_grid_attach(GTK_GRID(grid), show_answer_check, 0, 2, 2, 1);

    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT)
    {
        const char *room_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        int time_limit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(time_spin));
        int show_answer = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_answer_check)) ? 1 : 0;

        if (strlen(room_name) > 0)
        {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "CREATE_PRACTICE|%s|%d|%d\n", room_name, time_limit, show_answer);
            send_message(cmd);

            char buffer[BUFFER_SIZE];
            ssize_t n = receive_message(buffer, sizeof(buffer));

            if (n > 0 && strstr(buffer, "CREATE_PRACTICE_OK"))
            {
                GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                                   "✅ Practice room created successfully!");
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(success_dialog),
                                                         "Students can now join and practice.\n"
                                                         "Show Answer: %s",
                                                         show_answer ? "Yes" : "No");
                gtk_dialog_run(GTK_DIALOG(success_dialog));
                gtk_widget_destroy(success_dialog);
                create_main_menu();
            }
            else 
            {
                GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                                 GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                                 "❌ Failed to create practice room!");
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                                                         "Server response: %s", buffer);
                gtk_dialog_run(GTK_DIALOG(error_dialog));
                gtk_widget_destroy(error_dialog);
            }
        }
        else
        {
            show_error_dialog("Room name cannot be empty!");
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

// Admin-specific create room handler
void on_admin_create_room_clicked(GtkWidget *widget, gpointer data)
{
    // Tạo dialog để nhập thông tin phòng
    GtkWidget *dialog = gtk_dialog_new_with_buttons("➕ Create New Room",
                                                     GTK_WINDOW(main_window),
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "Cancel", GTK_RESPONSE_CANCEL,
                                                     "Create", GTK_RESPONSE_ACCEPT,
                                                     NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(content_area), grid);

    // Room name
    GtkWidget *name_label = gtk_label_new("Room Name:");
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "e.g., Math Quiz 101");
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);

    // Time limit
    GtkWidget *time_label = gtk_label_new("Time Limit (minutes):");
    GtkWidget *time_spin = gtk_spin_button_new_with_range(1, 180, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_spin), 30);
    gtk_grid_attach(GTK_GRID(grid), time_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), time_spin, 1, 1, 1, 1);

    // === NEW: Question count settings ===
    GtkWidget *q_section_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(q_section_label), "<b>Question Settings</b>");
    gtk_widget_set_halign(q_section_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), q_section_label, 0, 2, 2, 1);

    // Easy questions
    GtkWidget *easy_label = gtk_label_new("Easy Questions:");
    GtkWidget *easy_spin = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(easy_spin), 10);
    gtk_grid_attach(GTK_GRID(grid), easy_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), easy_spin, 1, 3, 1, 1);

    // Medium questions
    GtkWidget *medium_label = gtk_label_new("Medium Questions:");
    GtkWidget *medium_spin = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(medium_spin), 5);
    gtk_grid_attach(GTK_GRID(grid), medium_label, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), medium_spin, 1, 4, 1, 1);

    // Hard questions
    GtkWidget *hard_label = gtk_label_new("Hard Questions:");
    GtkWidget *hard_spin = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(hard_spin), 0);
    gtk_grid_attach(GTK_GRID(grid), hard_label, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), hard_spin, 1, 5, 1, 1);

    // Info label
    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label), 
        "<span foreground='#7f8c8d' size='small'>Total questions = Easy + Medium + Hard\n"
        "Questions will be randomly selected from your pool</span>");
    gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
    gtk_widget_set_halign(info_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), info_label, 0, 6, 2, 1);

    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT)
    {
        const char *room_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        int time_limit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(time_spin));
        int easy_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(easy_spin));
        int medium_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(medium_spin));
        int hard_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(hard_spin));

        if (strlen(room_name) > 0)
        {
            // Updated protocol: CREATE_ROOM|name|duration|easy|medium|hard
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "CREATE_ROOM|%s|%d|%d|%d|%d\n", 
                     room_name, time_limit, easy_count, medium_count, hard_count);
            send_message(cmd);

            char buffer[BUFFER_SIZE];
            ssize_t n = receive_message(buffer, sizeof(buffer));

            if (n > 0 && strstr(buffer, "CREATE_ROOM_OK"))
            {
                GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                                   "✅ Room created successfully!");
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(success_dialog),
                                                         "Room: %s\nTotal questions: %d (Easy:%d, Medium:%d, Hard:%d)",
                                                         room_name, easy_count + medium_count + hard_count,
                                                         easy_count, medium_count, hard_count);
                gtk_dialog_run(GTK_DIALOG(success_dialog));
                gtk_widget_destroy(success_dialog);

                // Quay về main menu
                create_main_menu();
            }
            else 
            {
                GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                                 GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                                 "❌ Failed to create room!");
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                                                         "Server response: %s", buffer);
                gtk_dialog_run(GTK_DIALOG(error_dialog));
                gtk_widget_destroy(error_dialog);
            }
        }
        else
        {
            show_error_dialog("Room name cannot be empty!");
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

// View room statistics callback
void on_view_room_stats_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    create_room_stats_screen(room_id);
}

// Display room statistics screen
void create_room_stats_screen(int room_id) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);

    // Title
    GtkWidget *title = gtk_label_new(NULL);
    char title_text[128];
    snprintf(title_text, sizeof(title_text), 
             "<span foreground=\"#2c3e50\" weight=\"bold\" size=\"20480\">Room Statistics - ID: %d</span>", 
             room_id);
    gtk_label_set_markup(GTK_LABEL(title), title_text);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    // Request statistics from server
    flush_socket_buffer(client.socket_fd);
    
    char request[64];
    snprintf(request, sizeof(request), "GET_ROOM_STATS|%d\n", room_id);
    send_message(request);

    char buffer[16384];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strncmp(buffer, "ROOM_STATS|", 11) == 0) {
        // Parse response
        
        // Summary statistics box
        GtkWidget *summary_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
        gtk_widget_set_margin_top(summary_box, 10);
        gtk_widget_set_margin_bottom(summary_box, 10);

        // Parse to find SUMMARY section
        char *summary_start = strstr(buffer, "SUMMARY|");
        int total_count = 0, taking_count = 0, submitted_count = 0;
        double avg_score = 0.0;
        
        if (summary_start) {
            sscanf(summary_start, "SUMMARY|Total:%d|Taking:%d|Submitted:%d|AvgScore:%lf",
                   &total_count, &taking_count, &submitted_count, &avg_score);
        }

        // Display summary cards
        GtkWidget *total_label = gtk_label_new(NULL);
        char total_text[128];
        snprintf(total_text, sizeof(total_text), 
                 "<span foreground=\"#3498db\" weight=\"bold\" size=\"14000\">Total Participants\n%d</span>",
                 total_count);
        gtk_label_set_markup(GTK_LABEL(total_label), total_text);
        gtk_box_pack_start(GTK_BOX(summary_box), total_label, TRUE, TRUE, 0);

        GtkWidget *taking_label = gtk_label_new(NULL);
        char taking_text[128];
        snprintf(taking_text, sizeof(taking_text), 
                 "<span foreground=\"#f39c12\" weight=\"bold\" size=\"14000\">Currently Taking\n%d</span>",
                 taking_count);
        gtk_label_set_markup(GTK_LABEL(taking_label), taking_text);
        gtk_box_pack_start(GTK_BOX(summary_box), taking_label, TRUE, TRUE, 0);

        GtkWidget *submitted_label = gtk_label_new(NULL);
        char submitted_text[128];
        snprintf(submitted_text, sizeof(submitted_text), 
                 "<span foreground=\"#27ae60\" weight=\"bold\" size=\"14000\">Submitted\n%d</span>",
                 submitted_count);
        gtk_label_set_markup(GTK_LABEL(submitted_label), submitted_text);
        gtk_box_pack_start(GTK_BOX(summary_box), submitted_label, TRUE, TRUE, 0);

        GtkWidget *avg_label = gtk_label_new(NULL);
        char avg_text[128];
        snprintf(avg_text, sizeof(avg_text), 
                 "<span foreground=\"#9b59b6\" weight=\"bold\" size=\"14000\">Average Score\n%.2f%%</span>",
                 avg_score);
        gtk_label_set_markup(GTK_LABEL(avg_label), avg_text);
        gtk_box_pack_start(GTK_BOX(summary_box), avg_label, TRUE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(vbox), summary_box, FALSE, FALSE, 0);

        GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 5);

        // Participants table
        GtkWidget *participants_title = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(participants_title),
                            "<span foreground=\"#2c3e50\" weight=\"bold\" size=\"16000\">Participants Details</span>");
        gtk_box_pack_start(GTK_BOX(vbox), participants_title, FALSE, FALSE, 5);

        // Scrolled window for participants
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 300);

        GtkWidget *participants_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        // Header row
        GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_start(header_box, 10);
        gtk_widget_set_margin_end(header_box, 10);
        gtk_widget_set_margin_top(header_box, 5);
        gtk_widget_set_margin_bottom(header_box, 5);

        GtkWidget *h_user = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(h_user), "<b>Username</b>");
        gtk_widget_set_size_request(h_user, 200, -1);
        gtk_label_set_xalign(GTK_LABEL(h_user), 0);
        gtk_box_pack_start(GTK_BOX(header_box), h_user, FALSE, FALSE, 0);

        GtkWidget *h_status = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(h_status), "<b>Status</b>");
        gtk_widget_set_size_request(h_status, 120, -1);
        gtk_box_pack_start(GTK_BOX(header_box), h_status, FALSE, FALSE, 0);

        GtkWidget *h_score = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(h_score), "<b>Score</b>");
        gtk_widget_set_size_request(h_score, 100, -1);
        gtk_box_pack_start(GTK_BOX(header_box), h_score, FALSE, FALSE, 0);

        GtkWidget *h_completed = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(h_completed), "<b>Completed At</b>");
        gtk_box_pack_start(GTK_BOX(header_box), h_completed, TRUE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(participants_box), header_box, FALSE, FALSE, 0);

        GtkWidget *header_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(participants_box), header_sep, FALSE, FALSE, 0);

        // Parse participants data
        char *participants_start = strstr(buffer, "PARTICIPANTS|");
        if (participants_start) {
            participants_start += 13; // Skip "PARTICIPANTS|"
            
            char *entry = strtok(participants_start, "|");
            while (entry && strncmp(entry, "SUMMARY", 7) != 0) {
                // Parse: user_id:username:status:score:total_questions:completed_at
                char username[100], status[20], completed[100];
                int user_id, score, total_questions;
                
                if (sscanf(entry, "%d:%99[^:]:%19[^:]:%d:%d:%99s",
                          &user_id, username, status, &score, &total_questions, completed) >= 3) {
                    
                    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                    gtk_widget_set_margin_start(row_box, 10);
                    gtk_widget_set_margin_end(row_box, 10);
                    gtk_widget_set_margin_top(row_box, 5);
                    gtk_widget_set_margin_bottom(row_box, 5);

                    // Username
                    GtkWidget *user_label = gtk_label_new(username);
                    gtk_widget_set_size_request(user_label, 200, -1);
                    gtk_label_set_xalign(GTK_LABEL(user_label), 0);
                    gtk_box_pack_start(GTK_BOX(row_box), user_label, FALSE, FALSE, 0);

                    // Status with color coding
                    GtkWidget *status_label = gtk_label_new(NULL);
                    char status_markup[200];
                    const char *color;
                    if (strcmp(status, "Submitted") == 0) {
                        color = "#27ae60";
                    } else if (strcmp(status, "Taking") == 0) {
                        color = "#f39c12";
                    } else {
                        color = "#95a5a6";
                    }
                    snprintf(status_markup, sizeof(status_markup),
                            "<span foreground=\"%s\" weight=\"bold\">%s</span>", color, status);
                    gtk_label_set_markup(GTK_LABEL(status_label), status_markup);
                    gtk_widget_set_size_request(status_label, 120, -1);
                    gtk_box_pack_start(GTK_BOX(row_box), status_label, FALSE, FALSE, 0);

                    // Score
                    GtkWidget *score_label = gtk_label_new(NULL);
                    char score_text[50];
                    if (strcmp(status, "Submitted") == 0 && total_questions > 0) {
                        double percent = (score * 100.0 / total_questions);
                        snprintf(score_text, sizeof(score_text), "%d/%d (%.1f%%)", 
                                score, total_questions, percent);
                    } else {
                        snprintf(score_text, sizeof(score_text), "-");
                    }
                    gtk_label_set_text(GTK_LABEL(score_label), score_text);
                    gtk_widget_set_size_request(score_label, 100, -1);
                    gtk_box_pack_start(GTK_BOX(row_box), score_label, FALSE, FALSE, 0);

                    // Completed time
                    GtkWidget *time_label = gtk_label_new(completed);
                    gtk_label_set_xalign(GTK_LABEL(time_label), 0);
                    gtk_box_pack_start(GTK_BOX(row_box), time_label, TRUE, TRUE, 0);

                    gtk_box_pack_start(GTK_BOX(participants_box), row_box, FALSE, FALSE, 0);

                    // Separator
                    GtkWidget *row_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                    gtk_box_pack_start(GTK_BOX(participants_box), row_sep, FALSE, FALSE, 0);
                }
                
                entry = strtok(NULL, "|");
            }
        }

        gtk_container_add(GTK_CONTAINER(scroll), participants_box);
        gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    } else {
        // Error handling
        GtkWidget *error_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(error_label),
                            "<span foreground=\"#e74c3c\" size=\"14000\">Failed to load room statistics</span>");
        gtk_box_pack_start(GTK_BOX(vbox), error_label, TRUE, TRUE, 0);
    }

    // Back button
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *refresh_btn = gtk_button_new_with_label("REFRESH");
    GtkWidget *back_btn = gtk_button_new_with_label("BACK");
    style_button(refresh_btn, "#3498db");
    style_button(back_btn, "#95a5a6");
    
    g_signal_connect_swapped(refresh_btn, "clicked", 
                             G_CALLBACK(create_room_stats_screen), 
                             GINT_TO_POINTER(room_id));
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_admin_panel), NULL);
    
    gtk_box_pack_start(GTK_BOX(button_box), refresh_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 10);

    show_view(vbox);
}

