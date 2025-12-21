#include "admin_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

void request_user_rooms(GtkWidget *room_combo)
{
    char request[256];
    snprintf(request, sizeof(request), "GET_USER_ROOMS|%d\n", current_user_id);
    send_message(request);
    
    // Nhận response và populate combo box
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        // Parse: ROOMS_LIST|room_id1:room_name1|room_id2:room_name2|...
        if (strstr(buffer, "ROOMS_LIST")) {
            char *token = strtok(buffer + 11, "|"); // skip "ROOMS_LIST|"
            while (token != NULL) {
                char *colon = strchr(token, ':');
                if (colon) {
                    *colon = '\0';
                    char *room_id = token;
                    char *room_name = colon + 1;
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), room_id, room_name);
                }
                token = strtok(NULL, "|");
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(room_combo), 0);
        } else if (strstr(buffer, "NO_ROOMS")) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), "-1", "No rooms created yet");
            gtk_widget_set_sensitive(room_combo, FALSE);
        }
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
                                       "❌ %s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
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
                        "⏱️ Duration: %d minutes | 📝 %s | Status: %s",
                        room_name, room_id, duration, question_info, status);
                
                GtkWidget *info_label = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(info_label), info_text);
                gtk_label_set_xalign(GTK_LABEL(info_label), 0);
                gtk_box_pack_start(GTK_BOX(hbox), info_label, TRUE, TRUE, 0);

                // Action buttons - luôn hiển thị dựa trên status
                GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
                
                if (strcmp(status, "Open") == 0) {
                    // Room đang Open - hiển thị button CLOSE
                    GtkWidget *close_btn = gtk_button_new_with_label("🔒 CLOSE");
                    style_button(close_btn, "#e74c3c");
                    gtk_widget_set_size_request(close_btn, 120, 40);
                    g_signal_connect(close_btn, "clicked", 
                                   G_CALLBACK(on_close_room_clicked), 
                                   GINT_TO_POINTER(room_id));
                    gtk_box_pack_start(GTK_BOX(btn_box), close_btn, FALSE, FALSE, 0);
                } else {
                    // Room đang Closed - hiển thị button OPEN
                    GtkWidget *open_btn = gtk_button_new_with_label("🔓 OPEN");
                    style_button(open_btn, "#27ae60");
                    gtk_widget_set_size_request(open_btn, 120, 40);
                    g_signal_connect(open_btn, "clicked", 
                                   G_CALLBACK(on_start_room_clicked), 
                                   GINT_TO_POINTER(room_id));
                    gtk_box_pack_start(GTK_BOX(btn_box), open_btn, FALSE, FALSE, 0);
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

    // Max attempts (0 = unlimited)
    GtkWidget *attempts_label = gtk_label_new("Max Attempts:");
    GtkWidget *attempts_spin = gtk_spin_button_new_with_range(0, 10, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(attempts_spin), 0);
    gtk_grid_attach(GTK_GRID(grid), attempts_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), attempts_spin, 1, 2, 1, 1);
    
    // Tooltip
    GtkWidget *attempts_hint = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(attempts_hint), 
        "<span size='small' foreground='#7f8c8d'>0 = Unlimited attempts</span>");
    gtk_grid_attach(GTK_GRID(grid), attempts_hint, 1, 3, 1, 1);

    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT)
    {
        const char *room_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        int time_limit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(time_spin));
        int max_attempts = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(attempts_spin));

        if (strlen(room_name) > 0)
        {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "CREATE_ROOM|%s|%d\n", room_name, time_limit);
            send_message(cmd);

            char buffer[BUFFER_SIZE];
            ssize_t n = receive_message(buffer, sizeof(buffer));

            if (n > 0 && strstr(buffer, "CREATE_ROOM_OK"))
            {
                // Parse room_id từ response: CREATE_ROOM_OK|room_id|name|...
                char *response_copy = strdup(buffer);
                strtok(response_copy, "|"); // Skip "CREATE_ROOM_OK"
                char *room_id_str = strtok(NULL, "|");
                int created_room_id = room_id_str ? atoi(room_id_str) : -1;
                free(response_copy);
                
                // Set max_attempts nếu khác 0
                if (max_attempts > 0 && created_room_id > 0) {
                    char attempts_cmd[128];
                    snprintf(attempts_cmd, sizeof(attempts_cmd), 
                             "SET_MAX_ATTEMPTS|%d|%d\n", created_room_id, max_attempts);
                    send_message(attempts_cmd);
                    
                    // Đợi response
                    char attempts_buffer[BUFFER_SIZE];
                    receive_message(attempts_buffer, sizeof(attempts_buffer));
                }
                
                GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                                   "✅ Room created successfully!");
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(success_dialog),
                                                         "You can now add questions and manage this room.");
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
