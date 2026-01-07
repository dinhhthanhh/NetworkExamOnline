#include "room_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include "exam_ui.h"
#include "broadcast.h"
#include "broadcast.h"
#include <string.h>
#include <stdlib.h>
#include <gdk/gdk.h>
#include <fcntl.h>

// Idle callback for safe refresh
static gboolean idle_refresh_rooms(gpointer user_data) {
    if (rooms_list != NULL && GTK_IS_LIST_BOX(rooms_list)) {
        printf("[ROOM_UI] Idle refresh: loading rooms list\n");
        load_rooms_list();
    }
    return FALSE; // Remove idle callback after execution
}

// Callback when ROOM_CREATED broadcast received
static void on_room_created_broadcast(int room_id, const char *room_name, int duration) {
    printf("[ROOM_UI] Received ROOM_CREATED: %d - %s (%d min)\n", room_id, room_name, duration);
    
    // Schedule refresh in GTK main loop to avoid race conditions
    // This ensures the current event handler completes before refresh
    g_idle_add(idle_refresh_rooms, NULL);
}

void on_room_button_clicked(GtkWidget *button, gpointer data)
{
    const char *room_id_str = g_object_get_data(G_OBJECT(button), "room_id");
    const char *room_name = gtk_button_get_label(GTK_BUTTON(button));
    
    if (room_id_str)
    {
        selected_room_id = atoi(room_id_str);
        
        // Cập nhật label hiển thị phòng đã chọn
        char markup[256];
            snprintf(markup, sizeof(markup), 
                     "<span foreground='#27ae60' weight='bold'>Selected: %s (ID: %d)</span>", 
                 room_name, selected_room_id);
        gtk_label_set_markup(GTK_LABEL(selected_room_label), markup);
    }
}

void load_rooms_list()
{
    // Validate socket before proceeding
    if (client.socket_fd <= 0) {
        printf("[ROOM_UI] Invalid socket, cannot load rooms\n");
        return;
    }
    
    // Ensure socket is in blocking mode
    int flags = fcntl(client.socket_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
        printf("[ROOM_UI] Socket set to blocking mode\n");  // ← LOG
    }
    
    // Reset selected room
    selected_room_id = -1;
    if (selected_room_label && GTK_IS_LABEL(selected_room_label)) {
        gtk_label_set_markup(GTK_LABEL(selected_room_label), 
                            "<span foreground='#e74c3c'>No room selected</span>");
    }
    
    // Xóa hết các row cũ
    if (rooms_list && GTK_IS_LIST_BOX(rooms_list)) {
        GList *children, *iter;
        children = gtk_container_get_children(GTK_CONTAINER(rooms_list));
        for (iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        g_list_free(children);
    }

    // Flush old responses before new request
    flush_socket_buffer(client.socket_fd);
    
    // Gửi yêu cầu lấy danh sách phòng
    send_message("LIST_ROOMS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && buffer[0] != '\0')
    {
        char *line = strtok(buffer, "\n");
        int room_count = 0;
        
        while (line != NULL)
        {
            if (strncmp(line, "ROOM|", 5) == 0)
            {
                // Parse: ROOM|id|name|time|status|question_status|owner
                char room_id[16], room_name[64], owner[32], status[32];
                int time_limit;
                
                sscanf(line, "ROOM|%15[^|]|%63[^|]|%d|%31[^|]|%*[^|]|%31s",
                       room_id, room_name, &time_limit, status, owner);

                // Tạo button với thông tin phòng
                char button_label[256];
                    snprintf(button_label, sizeof(button_label), 
                            "%s | %dmin | %s", 
                        room_name, time_limit, owner);
                
                GtkWidget *btn = gtk_button_new_with_label(button_label);
                
                // Lưu room_id vào button
                g_object_set_data_full(G_OBJECT(btn), "room_id", 
                                      g_strdup(room_id), g_free);
                
                // Style cho button
                GtkCssProvider *provider = gtk_css_provider_new();
                char css[512];
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
                gtk_css_provider_load_from_data(provider, css, -1, NULL);
                gtk_style_context_add_provider(gtk_widget_get_style_context(btn),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                
                // Kết nối signal
                g_signal_connect(btn, "clicked", 
                               G_CALLBACK(on_room_button_clicked), NULL);

                // Thêm vào list
                GtkWidget *row = gtk_list_box_row_new();
                gtk_container_add(GTK_CONTAINER(row), btn);
                gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
                
                room_count++;
            }
            line = strtok(NULL, "\n");
        }
        
        if (room_count == 0)
        {
            GtkWidget *label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(label), 
                               "<span foreground='#95a5a6' size='large'>No rooms available. Create one!</span>");
            GtkWidget *row = gtk_list_box_row_new();
            gtk_container_add(GTK_CONTAINER(row), label);
            gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
        }
    }
    else
    {
        GtkWidget *label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), 
                   "<span foreground='#e74c3c'>Cannot load rooms. Check connection.</span>");
        GtkWidget *row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), label);
        gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
    }

    gtk_widget_show_all(GTK_WIDGET(rooms_list));
}

void on_join_room_clicked(GtkWidget *widget, gpointer data)
{
    if (selected_room_id < 0)
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                               "Please select a room first!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    // Gửi lệnh JOIN_ROOM với room_id đã chọn
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "JOIN_ROOM|%d\n", selected_room_id);
    send_message(cmd);

    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strstr(buffer, "JOIN_ROOM_OK"))
    {
        // Kiểm tra xem user có session cũ trong room này không
        char check_cmd[128];
        snprintf(check_cmd, sizeof(check_cmd), "RESUME_EXAM|%d\n", selected_room_id);
        send_message(check_cmd);
        
        char resume_buffer[BUFFER_SIZE];
        ssize_t resume_n = receive_message(resume_buffer, sizeof(resume_buffer));
        
        // Nếu có session cũ và chưa hết thời gian - TỰ ĐỘNG RESUME
        if (resume_n > 0 && strstr(resume_buffer, "RESUME_EXAM_OK")) {
            printf("[ROOM_UI] Auto-resuming previous exam session\n");
            create_exam_page_from_resume(selected_room_id, resume_buffer);
            return;
        }
        
        // Nếu hết thời gian - báo điểm và không cho resume
        if (resume_n > 0 && strstr(resume_buffer, "RESUME_TIME_EXPIRED")) {
            int score = -1, total = 0, time_minutes = 0;

            // Thử parse dạng: RESUME_TIME_EXPIRED|score|total|time_minutes
            char tmp[BUFFER_SIZE];
            strncpy(tmp, resume_buffer, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            char *ptr = tmp;
            char *token = strtok(ptr, "|"); // RESUME_TIME_EXPIRED
            token = strtok(NULL, "|");
            if (token) score = atoi(token);
            token = strtok(NULL, "|");
            if (token) total = atoi(token);
            token = strtok(NULL, "|\n");
            if (token) time_minutes = atoi(token);

            GtkWidget *error_dialog = gtk_message_dialog_new(
                GTK_WINDOW(main_window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_OK,
                    "Time Expired");

            if (score >= 0 && total > 0) {
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(error_dialog),
                    "Your exam session has expired.\n"
                    "Final score: %d/%d (%.1f%%)\nTime taken: %d minutes",
                    score, total, (score * 100.0) / total, time_minutes);
            } else {
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(error_dialog),
                    "Your exam session has expired. You cannot resume.");
            }

            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
            return;
        }
        
        // Nếu đã submit - báo lỗi
        if (resume_n > 0 && strstr(resume_buffer, "RESUME_ALREADY_SUBMITTED")) {
            GtkWidget *error_dialog = gtk_message_dialog_new(
                GTK_WINDOW(main_window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "Already Completed");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                "You have already completed this exam.");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
            return;
        }
        
        // Không có session cũ - BEGIN_EXAM trực tiếp (không cần dialog)
        printf("[ROOM_UI] Starting new exam session\n");
        
        // CRITICAL: Stop broadcast listener before entering exam
        if (broadcast_is_listening()) {
            broadcast_stop_listener();
            printf("[ROOM_UI] Stopped broadcast listener before exam\n");
        }
        
        char begin_cmd[128];
        snprintf(begin_cmd, sizeof(begin_cmd), "BEGIN_EXAM|%d\n", selected_room_id);
        send_message(begin_cmd);
        
        char exam_buffer[BUFFER_SIZE];
        ssize_t exam_n = receive_message(exam_buffer, sizeof(exam_buffer));
        
        // BEGIN_EXAM sẽ trả về BEGIN_EXAM_OK hoặc EXAM_WAITING
        // exam_ui.c sẽ xử lý logic waiting nếu cần
        if (exam_n > 0) {
            create_exam_page(selected_room_id);
        } else {
            GtkWidget *error_dialog = gtk_message_dialog_new(
                GTK_WINDOW(main_window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_OK,
                    "Failed to start exam:\n\n%s",
                exam_buffer[0] ? exam_buffer : "No response");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        }
    }
    else
    {
        char error_msg[BUFFER_SIZE + 50];
            snprintf(error_msg, sizeof(error_msg), 
                "Failed to join room!\n\nServer response: %s", 
                buffer[0] ? buffer : "No response");
        
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "%s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void on_create_room_clicked(GtkWidget *widget, gpointer data)
{
    // Tạo dialog để nhập thông tin phòng
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

    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT)
    {
        const char *room_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        int time_limit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(time_spin));

        if (strlen(room_name) > 0)
        {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "CREATE_ROOM|%s|%d\n", room_name, time_limit);
            send_message(cmd);

            char buffer[BUFFER_SIZE];
            ssize_t n = receive_message(buffer, sizeof(buffer));

            if (n > 0 && strstr(buffer, "CREATE_ROOM_OK"))
            {
                GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                                   "Room created!");
                gtk_dialog_run(GTK_DIALOG(success_dialog));
                gtk_widget_destroy(success_dialog);

                // Cập nhật danh sách phòng
                load_rooms_list();
            }
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void create_test_mode_screen()
{
    // CRITICAL: Stop any existing broadcast listener to clean up old callbacks
    if (broadcast_is_listening()) {
        broadcast_stop_listener();
        printf("[ROOM_UI] Stopped old broadcast listener before creating new screen\n");
    }
    
    // Reset global widget pointers
    rooms_list = NULL;
    selected_room_label = NULL;
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    // Title
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span foreground='#2c3e50' weight='bold' size='20480'>TEST MODE - Select a Room</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 5);

    // Button box
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *create_btn = NULL;
    // Only show CREATE ROOM button for admin
    if (strcmp(client.role, "admin") == 0) {
        create_btn = gtk_button_new_with_label("➥ CREATE ROOM");
        style_button(create_btn, "#27ae60");
    }
    
    GtkWidget *join_btn = gtk_button_new_with_label("JOIN ROOM");
    GtkWidget *refresh_btn = gtk_button_new_with_label("REFRESH");
    GtkWidget *back_btn = gtk_button_new_with_label("BACK");

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

    // Label phòng đang chọn
    selected_room_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(selected_room_label), 
                        "<span foreground='#e74c3c' size='large'>No room selected</span>");
    gtk_box_pack_start(GTK_BOX(vbox), selected_room_label, FALSE, FALSE, 5);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 5);

    // ListBox cho danh sách phòng
    rooms_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(rooms_list), GTK_SELECTION_NONE);
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(rooms_list));
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 350);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Load danh sách phòng
    load_rooms_list();

    // Signal connect
    if (create_btn) {
        g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_room_clicked), NULL);
    }
    g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_room_clicked), NULL);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(load_rooms_list), NULL);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);

    // CRITICAL: Show UI FIRST before starting broadcast listener
    show_view(vbox);
    
    // Start listening for ROOM_CREATED broadcasts AFTER UI is shown
    broadcast_on_room_created(on_room_created_broadcast);
    broadcast_start_listener();
    printf("[ROOM_UI] Started listening for ROOM_CREATED broadcasts\n");
}

void create_exam_room_with_questions(GtkWidget *widget, gpointer user_data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Create Exam Room",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create Room", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 15);
    gtk_container_add(GTK_CONTAINER(content_area), main_box);
    
    // Title
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), 
        "<span size='16000' weight='bold'>Create New Exam Room</span>");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0);
    gtk_box_pack_start(GTK_BOX(main_box), title_label, FALSE, FALSE, 0);
    
    // Separator
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep1, FALSE, FALSE, 5);
    
    // Room info section
    GtkWidget *info_frame = gtk_frame_new("Basic Information");
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(info_box, 15);
    gtk_widget_set_margin_end(info_box, 15);
    gtk_widget_set_margin_top(info_box, 10);
    gtk_widget_set_margin_bottom(info_box, 10);
    
    // Room name
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *name_label = gtk_label_new("Room Name:");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0);
    gtk_widget_set_size_request(name_label, 120, -1);
    GtkWidget *room_name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(room_name_entry), "Enter exam room name");
    gtk_box_pack_start(GTK_BOX(name_box), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(name_box), room_name_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), name_box, FALSE, FALSE, 0);
    
    // Time limit
    GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *time_label = gtk_label_new("Time Limit (min):");
    gtk_label_set_xalign(GTK_LABEL(time_label), 0);
    gtk_widget_set_size_request(time_label, 120, -1);
    GtkWidget *time_spin = gtk_spin_button_new_with_range(1, 180, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_spin), 30);
    gtk_box_pack_start(GTK_BOX(time_box), time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_box), time_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), time_box, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(info_frame), info_box);
    gtk_box_pack_start(GTK_BOX(main_box), info_frame, FALSE, FALSE, 5);
    
    // Info note about difficulty settings
    GtkWidget *note_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(note_label), 
        "<span foreground='#7f8c8d' size='10000'><i>Note: Add questions and configure difficulty settings\n"
        "in the Question Manager after creating the room.</i></span>");
    gtk_label_set_line_wrap(GTK_LABEL(note_label), TRUE);
    gtk_box_pack_start(GTK_BOX(main_box), note_label, FALSE, FALSE, 10);
    
    gtk_widget_show_all(dialog);
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_ACCEPT) {
        const char *room_name = gtk_entry_get_text(GTK_ENTRY(room_name_entry));
        int time_limit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(time_spin));
        
        if (strlen(room_name) == 0) {
            show_error_dialog("Room name cannot be empty!");
        } else {
            // Send create room command with default difficulty counts (0,0,0)
            // Admin will configure this in Question Manager
            char msg[512];
            snprintf(msg, sizeof(msg), "CREATE_ROOM|%s|%d|0|0|0\n", room_name, time_limit);
            send_message(msg);
            
            char recv_buf[BUFFER_SIZE];
            ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
            
            if (n > 0 && strncmp(recv_buf, "CREATE_ROOM_OK|", 15) == 0) {
                char success_msg[512];
                snprintf(success_msg, sizeof(success_msg),
                    "Exam room '%s' created!\n\n"
                    "Time Limit: %d minutes\n\n"
                    "Next: Add questions using the Question Manager.",
                    room_name, time_limit);
                show_info_dialog(success_msg);
                
                // Automatically show admin panel to display the new room
                create_admin_panel();
            } else if (n > 0 && strncmp(recv_buf, "CREATE_ROOM_FAIL|", 17) == 0) {
                char *error = recv_buf + 17;
                show_error_dialog(error);
            } else {
                show_error_dialog("Failed to create exam room. Please try again.");
            }
        }
    }
    
    gtk_widget_destroy(dialog);
}
