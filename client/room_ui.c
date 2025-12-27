#include "room_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include "exam_ui.h"
#include <string.h>
#include <stdlib.h>
#include <gdk/gdk.h>

static int persistent_selected_room_id = -1;
static char persistent_selected_room_name[64] = "";

void on_room_button_clicked(GtkWidget *button, gpointer data)
{
    const char *room_id_str = g_object_get_data(G_OBJECT(button), "room_id");
    const char *room_name = gtk_button_get_label(GTK_BUTTON(button));
    int is_completed = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "is_completed"));
    
    // Không cho phép chọn phòng đã hoàn thành
    if (is_completed) {
        show_info_dialog("⚠️ Already Completed", 
                        "You have already completed this exam.\n"
                        "Please choose another room.");
        return;
    }
    
    if (room_id_str)
    {
        selected_room_id = atoi(room_id_str);
        persistent_selected_room_id = selected_room_id;
        
        strncpy(persistent_selected_room_name, room_name, sizeof(persistent_selected_room_name) - 1);
        persistent_selected_room_name[sizeof(persistent_selected_room_name) - 1] = '\0';
        
        char markup[256];
        snprintf(markup, sizeof(markup), 
                 "<span foreground='#27ae60' weight='bold'>✅ Selected: %s (ID: %d)</span>", 
                 room_name, selected_room_id);
        gtk_label_set_markup(GTK_LABEL(selected_room_label), markup);
        
        // Room selected (debug prints removed)
    }
}

void load_rooms_list()
{
    int previously_selected = persistent_selected_room_id;
    selected_room_id = -1;
    
    // Xóa các row cũ
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(rooms_list));
    for (iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    flush_socket_buffer(client.socket_fd);
    
    send_message("LIST_ROOMS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && buffer[0] != '\0')
    {
        char *line = strtok(buffer, "\n");
        int room_count = 0;
        int found_previous_selection = 0;
        
        while (line != NULL)
        {
            if (strncmp(line, "ROOM|", 5) == 0)
            {
                // Parse: ROOM|id|name|time|status|question_status|owner|attempts_info|user_status
                char room_id[16], room_name[64], owner[32], status[32];
                char attempts_info[64], user_status[32];
                int time_limit;
                
                int parsed = sscanf(line, "ROOM|%15[^|]|%63[^|]|%d|%31[^|]|%*[^|]|%31[^|]|%63[^|]|%31s",
                       room_id, room_name, &time_limit, status, owner, attempts_info, user_status);
                
                if (parsed < 6) {
                    strcpy(attempts_info, "Unlimited");
                }
                if (parsed < 7) {
                    strcpy(user_status, "available"); // Default
                }

                int current_room_id = atoi(room_id);
                int is_completed = (strcmp(user_status, "completed") == 0);

                // Tạo button label
                char button_label[256];
                if (is_completed) {
                    snprintf(button_label, sizeof(button_label), 
                            "✅ %s | ⏱️ %dmin | 👤 %s | 🎯 %s [COMPLETED]", 
                            room_name, time_limit, owner, attempts_info);
                } else {
                    snprintf(button_label, sizeof(button_label), 
                            "🏠 %s | ⏱️ %dmin | 👤 %s | 🎯 %s", 
                            room_name, time_limit, owner, attempts_info);
                }
                
                GtkWidget *btn = gtk_button_new_with_label(button_label);
                
                // Lưu metadata
                g_object_set_data_full(G_OBJECT(btn), "room_id", 
                                      g_strdup(room_id), g_free);
                g_object_set_data_full(G_OBJECT(btn), "room_name",
                                      g_strdup(room_name), g_free);
                g_object_set_data(G_OBJECT(btn), "is_completed",
                                 GINT_TO_POINTER(is_completed));
                
                // Style dựa trên trạng thái
                GtkCssProvider *provider = gtk_css_provider_new();
                char css[512];
                
                if (is_completed) {
                    // Màu xám cho phòng đã hoàn thành (không thể chọn)
                    snprintf(css, sizeof(css),
                            "button { "
                            "  background: linear-gradient(135deg, #95a5a6 0%%, #7f8c8d 100%%);"
                            "  color: white;"
                            "  padding: 15px;"
                            "  font-weight: bold;"
                            "  border-radius: 8px;"
                            "  border: 2px solid #34495e;"
                            "  margin: 5px;"
                            "  opacity: 0.7;"
                            "}"
                            "button:hover {"
                            "  background: linear-gradient(135deg, #7f8c8d 0%%, #95a5a6 100%%);"
                            "  cursor: not-allowed;"
                            "}");
                } else if (current_room_id == previously_selected) {
                    // Highlight cho phòng đã chọn
                    snprintf(css, sizeof(css),
                            "button { "
                            "  background: linear-gradient(135deg, #27ae60 0%%, #2ecc71 100%%);"
                            "  color: white;"
                            "  padding: 15px;"
                            "  font-weight: bold;"
                            "  border-radius: 8px;"
                            "  border: 3px solid #f1c40f;"
                            "  margin: 5px;"
                            "}"
                            "button:hover {"
                            "  background: linear-gradient(135deg, #229954 0%%, #27ae60 100%%);"
                            "}");
                    
                    selected_room_id = current_room_id;
                    found_previous_selection = 1;
                } else {
                    // Normal style
                    snprintf(css, sizeof(css),
                            "button { "
                            "  background: linear-gradient(135deg, #667eea 0%%, #764ba2 100%%);"
                            "  color: white;"
                            "  padding: 15px;"
                            "  font-weight: bold;"
                            "  border-radius: 8px;"
                            "  border: none;"
                            "  margin: 5px;"
                            "}"
                            "button:hover {"
                            "  background: linear-gradient(135deg, #764ba2 0%%, #667eea 100%%);"
                            "}");
                }
                
                gtk_css_provider_load_from_data(provider, css, -1, NULL);
                gtk_style_context_add_provider(gtk_widget_get_style_context(btn),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                
                g_signal_connect(btn, "clicked", 
                               G_CALLBACK(on_room_button_clicked), NULL);
                if (is_completed) {
                    gtk_widget_set_sensitive(btn, FALSE);
                }
                GtkWidget *row = gtk_list_box_row_new();
                gtk_container_add(GTK_CONTAINER(row), btn);
                gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
                
                room_count++;
            }
            line = strtok(NULL, "\n");
        }
        
        // Update label
        if (found_previous_selection) {
            char markup[256];
            snprintf(markup, sizeof(markup), 
                     "<span foreground='#27ae60' weight='bold'>✅ Selected: %s (ID: %d)</span>", 
                     persistent_selected_room_name, selected_room_id);
            gtk_label_set_markup(GTK_LABEL(selected_room_label), markup);
        } else {
            persistent_selected_room_id = -1;
            persistent_selected_room_name[0] = '\0';
            gtk_label_set_markup(GTK_LABEL(selected_room_label), 
                                "<span foreground='#e74c3c'>❌ No room selected</span>");
        }
        
        if (room_count == 0)
        {
            GtkWidget *label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(label), 
                               "<span foreground='#95a5a6' size='large'>📭 No rooms available. Create one!</span>");
            GtkWidget *row = gtk_list_box_row_new();
            gtk_container_add(GTK_CONTAINER(row), label);
            gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
        }
    }
    else
    {
        persistent_selected_room_id = -1;
        persistent_selected_room_name[0] = '\0';
        
        GtkWidget *label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), 
                           "<span foreground='#e74c3c'>⚠️ Cannot load rooms. Check connection.</span>");
        GtkWidget *row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), label);
        gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
        
        gtk_label_set_markup(GTK_LABEL(selected_room_label), 
                            "<span foreground='#e74c3c'>❌ No room selected</span>");
    }

    gtk_widget_show_all(GTK_WIDGET(rooms_list));
}

static char* safe_strtok_helper(char **str, const char *delim) {
    if (!str || !*str) return NULL;
    
    char *start = *str;
    char *end = strpbrk(start, delim);
    
    if (end) {
        *end = '\0';
        *str = end + 1;
    } else {
        *str = NULL;
    }
    
    return start;
}

void on_join_room_clicked(GtkWidget *widget, gpointer data)
{
    if (selected_room_id < 0)
    {
        show_error_dialog("⚠️ No Room Selected", 
                         "Please select a room first!");
        return;
    }

    flush_socket_buffer(client.socket_fd);

    // Step 1: JOIN_ROOM
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "JOIN_ROOM|%d\n", selected_room_id);
    send_message(cmd);

    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message_timeout(buffer, sizeof(buffer), 5);

    if (n <= 0 || strncmp(buffer, "JOIN_ROOM_OK", 12) != 0)
    {
        char error_msg[BUFFER_SIZE + 50];
        snprintf(error_msg, sizeof(error_msg), 
                "Server response: %s", 
                (n > 0 && buffer[0]) ? buffer : "No response");
        
        show_error_dialog("❌ Failed to Join Room", error_msg);
        return;
    }

    // Joined room (debug prints removed)

    // Step 2: Check resume state
    flush_socket_buffer(client.socket_fd);
    
    char check_cmd[128];
    snprintf(check_cmd, sizeof(check_cmd), "RESUME_EXAM|%d\n", selected_room_id);
    send_message(check_cmd);
    
    char resume_buffer[BUFFER_SIZE * 2];
    memset(resume_buffer, 0, sizeof(resume_buffer));
    ssize_t resume_n = receive_message_timeout(resume_buffer, sizeof(resume_buffer), 5);
    
    // RESUME_EXAM response received (debug prints removed)
    
    // Step 3: Handle different states
    if (resume_n > 0) {
        // Already submitted
        if (strncmp(resume_buffer, "RESUME_ALREADY_SUBMITTED", 24) == 0) {
            char *info_copy = strdup(resume_buffer);
            char *ptr = info_copy;
            
            safe_strtok_helper(&ptr, "|");
            
            int score = 0, total = 0, time_taken = 0;
            char *token;
            
            if ((token = safe_strtok_helper(&ptr, "|"))) score = atoi(token);
            if ((token = safe_strtok_helper(&ptr, "|"))) total = atoi(token);
            if ((token = safe_strtok_helper(&ptr, "|"))) time_taken = atoi(token);
            
            free(info_copy);
            
            GtkWidget *dialog = gtk_message_dialog_new(
                GTK_WINDOW(main_window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "✅ Exam Already Completed");
            
            char result_text[256];
            snprintf(result_text, sizeof(result_text),
                    "You have already submitted this exam.\n\n"
                    "Your result: %d/%d (%.1f%%)\n"
                    "Time taken: %d minutes",
                    score, total, 
                    total > 0 ? (score * 100.0) / total : 0, 
                    time_taken);
            
            gtk_message_dialog_format_secondary_text(
                GTK_MESSAGE_DIALOG(dialog), "%s", result_text);
            
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            
            // Refresh room list để cập nhật UI
            load_rooms_list();
            return;
        }
        
        // Can resume
        if (strncmp(resume_buffer, "RESUME_EXAM_OK", 14) == 0) {
            GtkWidget *resume_dialog = gtk_dialog_new_with_buttons(
                "🔄 Resume Previous Exam?",
                GTK_WINDOW(main_window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                "Resume", GTK_RESPONSE_YES,
                "Start New", GTK_RESPONSE_NO,
                "Cancel", GTK_RESPONSE_CANCEL,
                NULL);
            
            GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(resume_dialog));
            GtkWidget *label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(label),
                "<span size='large' weight='bold'>You have an unfinished exam!</span>\n\n"
                "• <b>Resume</b>: Continue with your saved answers and remaining time\n"
                "• <b>Start New</b>: Discard progress and start fresh\n"
                "• <b>Cancel</b>: Go back");
            gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
            gtk_container_add(GTK_CONTAINER(content), label);
            gtk_widget_show_all(content);
            
            int response = gtk_dialog_run(GTK_DIALOG(resume_dialog));
            gtk_widget_destroy(resume_dialog);
            
            if (response == GTK_RESPONSE_CANCEL) {
                return;
            }
            
            if (response == GTK_RESPONSE_YES) {
                // Resuming exam with saved progress
                create_exam_page_from_resume(selected_room_id, resume_buffer);
                return;
            }
            
            // User chose to start new exam
        }
    }
    
    // Step 4: Start new exam
    GtkWidget *start_dialog = gtk_dialog_new_with_buttons(
        "🚀 Start New Exam?",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Start Exam", GTK_RESPONSE_ACCEPT,
        "Cancel", GTK_RESPONSE_CANCEL,
        NULL);
    
    GtkWidget *start_content = gtk_dialog_get_content_area(GTK_DIALOG(start_dialog));
    GtkWidget *start_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(start_label),
        "<span size='large' weight='bold'>Ready to begin?</span>\n\n"
        "• Timer will start immediately\n"
        "• Answer all questions carefully\n"
        "• You can save progress and resume later\n"
        "• Submit when finished or time runs out");
    gtk_label_set_line_wrap(GTK_LABEL(start_label), TRUE);
    gtk_container_add(GTK_CONTAINER(start_content), start_label);
    gtk_widget_show_all(start_content);
    
    int start_response = gtk_dialog_run(GTK_DIALOG(start_dialog));
    gtk_widget_destroy(start_dialog);
    
    if (start_response != GTK_RESPONSE_ACCEPT) {
        // User cancelled exam start
        return;
    }
    
    // Starting new exam for room
    create_exam_page(selected_room_id);
}

void on_create_room_clicked(GtkWidget *widget, gpointer data)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Create New Room",
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

    GtkWidget *name_label = gtk_label_new("Room Name:");
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "e.g., Math Quiz 101");
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);

    GtkWidget *time_label = gtk_label_new("Time Limit (minutes):");
    GtkWidget *time_spin = gtk_spin_button_new_with_range(1, 180, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_spin), 30);
    gtk_grid_attach(GTK_GRID(grid), time_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), time_spin, 1, 1, 1, 1);

    GtkWidget *attempts_label = gtk_label_new("Max Attempts:");
    GtkWidget *attempts_spin = gtk_spin_button_new_with_range(0, 10, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(attempts_spin), 0);
    gtk_grid_attach(GTK_GRID(grid), attempts_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), attempts_spin, 1, 2, 1, 1);
    
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
                char *response_copy = strdup(buffer);
                char *ptr = response_copy;
                safe_strtok_helper(&ptr, "|");
                char *room_id_str = safe_strtok_helper(&ptr, "|");
                int created_room_id = room_id_str ? atoi(room_id_str) : -1;
                free(response_copy);
                
                if (max_attempts > 0 && created_room_id > 0) {
                    char attempts_cmd[128];
                    snprintf(attempts_cmd, sizeof(attempts_cmd), 
                             "SET_MAX_ATTEMPTS|%d|%d\n", created_room_id, max_attempts);
                    send_message(attempts_cmd);
                    
                    char attempts_buffer[BUFFER_SIZE];
                    receive_message(attempts_buffer, sizeof(attempts_buffer));
                }
                
                show_info_dialog("✅ Success", "Room created successfully!");
                load_rooms_list();
            } else {
                show_error_dialog("Error", "Could not create room. Please try again.");
            }
        } else {
            show_error_dialog("Error", "Room name cannot be empty!");
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void create_test_mode_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span foreground='#2c3e50' weight='bold' size='20480'>🎯 TEST MODE - Select a Room</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 5);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *create_btn = NULL;
    if (strcmp(client.role, "admin") == 0) {
        create_btn = gtk_button_new_with_label("+ CREATE ROOM");
        style_button(create_btn, "#27ae60");
    }
    
    GtkWidget *join_btn = gtk_button_new_with_label("🚪 JOIN ROOM");
    GtkWidget *refresh_btn = gtk_button_new_with_label("🔄 REFRESH");
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");

    style_button(join_btn, "#3498db");
    style_button(refresh_btn, "#f39c12");
    style_button(back_btn, "#95a5a6");

    if (create_btn) {
        gtk_box_pack_start(GTK_BOX(button_box), create_btn, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(button_box), join_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), refresh_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    selected_room_label = gtk_label_new(NULL);
    
    if (persistent_selected_room_id > 0) {
        char markup[256];
        snprintf(markup, sizeof(markup), 
                 "<span foreground='#27ae60' weight='bold'>✅ Selected: %s (ID: %d)</span>", 
                 persistent_selected_room_name, persistent_selected_room_id);
        gtk_label_set_markup(GTK_LABEL(selected_room_label), markup);
    } else {
        gtk_label_set_markup(GTK_LABEL(selected_room_label), 
                            "<span foreground='#e74c3c' size='large'>❌ No room selected</span>");
    }
    
    gtk_box_pack_start(GTK_BOX(vbox), selected_room_label, FALSE, FALSE, 5);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 5);

    rooms_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(rooms_list), GTK_SELECTION_NONE);
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(rooms_list));
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 350);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    load_rooms_list();

    if (create_btn) {
        g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_room_clicked), NULL);
    }
    g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_room_clicked), NULL);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(load_rooms_list), NULL);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);

    show_view(vbox);
}

gboolean go_back_to_room(gpointer data) {
    create_test_mode_screen();
    return FALSE;
}