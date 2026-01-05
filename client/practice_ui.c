#include "practice_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include "question_ui.h"
#include <string.h>
#include <stdlib.h>

// Forward declarations
void create_main_menu(void);
void show_error_dialog(const char *message);
void show_info_dialog(const char *message);

PracticeSession current_practice = {0};
PracticeRoomInfo practice_rooms[50];
int practice_room_count = 0;

// Callback for practice list refresh
void on_refresh_practice_list(GtkWidget *widget, gpointer data);
void on_join_practice(GtkWidget *widget, gpointer data);
void on_submit_practice_answer(GtkWidget *widget, gpointer data);
void on_finish_practice(GtkWidget *widget, gpointer data);
void on_restart_practice(GtkWidget *widget, gpointer data);
void on_view_practice_results(GtkWidget *widget, gpointer data);

// Admin callbacks
void on_create_practice_clicked(GtkWidget *widget, gpointer data);
void on_close_practice_clicked(GtkWidget *widget, gpointer data);
void on_open_practice_clicked(GtkWidget *widget, gpointer data);
void on_view_participants_clicked(GtkWidget *widget, gpointer data);

// Parse practice room list from server
void parse_practice_rooms(char *data) {
    practice_room_count = 0;
    
    char *token = strtok(data, ";");
    while (token != NULL && practice_room_count < 50) {
        PracticeRoomInfo *room = &practice_rooms[practice_room_count];
        
        // Format: id,name,creator,time_limit,show_answers,is_open,num_questions,active_count
        sscanf(token, "%d,%99[^,],%49[^,],%d,%d,%d,%d,%d",
               &room->practice_id,
               room->room_name,
               room->creator_name,
               &room->time_limit,
               &room->show_answers,
               &room->is_open,
               &room->num_questions,
               &room->active_participants);
        
        // Skip rooms with empty names or invalid IDs
        if (strlen(room->room_name) > 0 && room->practice_id > 0) {
            practice_room_count++;
        }
        token = strtok(NULL, ";");
    }
}

// Show practice room list for users
void show_practice_list_screen(void) {
    // Send request to server
    send_message("LIST_PRACTICE\n");
    
    char recv_buf[BUFFER_SIZE * 2];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n <= 0) {
        show_error_dialog("Failed to load practice rooms");
        return;
    }
    
    // Parse response: PRACTICE_ROOMS_LIST|room_data
    if (strncmp(recv_buf, "PRACTICE_ROOMS_LIST|", 20) != 0) {
        show_error_dialog("Invalid server response");
        return;
    }
    
    char *room_data = recv_buf + 20;
    parse_practice_rooms(room_data);
    
    // Create UI
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    // Title
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span foreground='#2c3e50' weight='bold' size='20000'>PRACTICE ROOMS</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    
    // Scrolled window for room list
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);
    
    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(scroll), list_box);
    
    // Add practice rooms
    for (int i = 0; i < practice_room_count; i++) {
        PracticeRoomInfo *room = &practice_rooms[i];
        
        GtkWidget *room_frame = gtk_frame_new(NULL);
        GtkWidget *room_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_start(room_box, 10);
        gtk_widget_set_margin_end(room_box, 10);
        gtk_widget_set_margin_top(room_box, 10);
        gtk_widget_set_margin_bottom(room_box, 10);
        
        // Room name and status
        char status_text[500];
        const char *status_icon = room->is_open ? "🟢" : "🔴";
        const char *status_label = room->is_open ? "OPEN" : "CLOSED";
        snprintf(status_text, sizeof(status_text),
                "<b>%s %s</b> %s",
                status_icon, room->room_name, status_label);
        
        GtkWidget *name_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(name_label), status_text);
        gtk_label_set_xalign(GTK_LABEL(name_label), 0);
        gtk_box_pack_start(GTK_BOX(room_box), name_label, FALSE, FALSE, 0);
        
        // Room info
        char info_text[500];
        snprintf(info_text, sizeof(info_text),
                "Creator: %s | Questions: %d | Time: %s | Show Answers: %s | Participants: %d",
                room->creator_name,
                room->num_questions,
                room->time_limit > 0 ? g_strdup_printf("%d min", room->time_limit) : "Unlimited",
                room->show_answers ? "Yes" : "No",
                room->active_participants);
        
        GtkWidget *info_label = gtk_label_new(info_text);
        gtk_label_set_xalign(GTK_LABEL(info_label), 0);
        gtk_box_pack_start(GTK_BOX(room_box), info_label, FALSE, FALSE, 0);
        
        // Join button
        if (room->is_open && room->num_questions > 0) {
            GtkWidget *join_btn = gtk_button_new_with_label("▶️ JOIN PRACTICE");
            style_button(join_btn, "#27ae60");
            g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_practice), 
                           GINT_TO_POINTER(room->practice_id));
            gtk_box_pack_start(GTK_BOX(room_box), join_btn, FALSE, FALSE, 0);
        }
        
        gtk_container_add(GTK_CONTAINER(room_frame), room_box);
        gtk_box_pack_start(GTK_BOX(list_box), room_frame, FALSE, FALSE, 0);
    }
    
    if (practice_room_count == 0) {
        GtkWidget *no_rooms = gtk_label_new("No practice rooms available");
        gtk_box_pack_start(GTK_BOX(list_box), no_rooms, TRUE, TRUE, 0);
    }
    
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    // Buttons
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *refresh_btn = gtk_button_new_with_label("REFRESH");
    style_button(refresh_btn, "#3498db");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(show_practice_list_screen), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), refresh_btn, TRUE, TRUE, 0);
    
    GtkWidget *back_btn = gtk_button_new_with_label("BACK");
    style_button(back_btn, "#95a5a6");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), back_btn, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);
    
    show_view(vbox);
}

// Join practice room callback
void on_join_practice(GtkWidget *widget, gpointer data) {
    int practice_id = GPOINTER_TO_INT(data);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "JOIN_PRACTICE|%d\n", practice_id);
    send_message(msg);
    
    char recv_buf[BUFFER_SIZE * 2];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n <= 0) {
        show_error_dialog("Failed to join practice room");
        return;
    }
    
    if (strncmp(recv_buf, "JOIN_PRACTICE_FAIL|", 19) == 0) {
        char *error_msg = recv_buf + 19;
        show_error_dialog(error_msg);
        return;
    }
    
    if (strncmp(recv_buf, "JOIN_PRACTICE_OK|", 17) != 0) {
        show_error_dialog("Invalid server response");
        return;
    }
    
    // Parse response: JOIN_PRACTICE_OK|practice_id|name|time_limit|show_answers|num_q|session_id|questions
    char *response_data = recv_buf + 17;
    
    memset(&current_practice, 0, sizeof(current_practice));
    
    char *token = strtok(response_data, "|");
    current_practice.practice_id = atoi(token);
    
    token = strtok(NULL, "|");
    strncpy(current_practice.room_name, token, sizeof(current_practice.room_name) - 1);
    
    token = strtok(NULL, "|");
    current_practice.time_limit = atoi(token);
    
    token = strtok(NULL, "|");
    current_practice.show_answers = atoi(token);
    
    token = strtok(NULL, "|");
    current_practice.num_questions = atoi(token);
    
    token = strtok(NULL, "|");
    current_practice.session_id = atoi(token);
    
    // Parse questions: id~text~opt1~opt2~opt3~opt4~difficulty~user_answer
    int q_idx = 0;
    token = strtok(NULL, "|");
    while (token != NULL && q_idx < current_practice.num_questions) {
        PracticeQuestion *q = &current_practice.questions[q_idx];
        
        char *q_data = strdup(token);
        char *q_token = strtok(q_data, "~");
        
        q->id = atoi(q_token);
        
        q_token = strtok(NULL, "~");
        strncpy(q->text, q_token, sizeof(q->text) - 1);
        
        for (int i = 0; i < 4; i++) {
            q_token = strtok(NULL, "~");
            if (q_token) {
                strncpy(q->options[i], q_token, sizeof(q->options[i]) - 1);
            }
        }
        
        q_token = strtok(NULL, "~");
        if (q_token) {
            strncpy(q->difficulty, q_token, sizeof(q->difficulty) - 1);
        }
        
        q_token = strtok(NULL, "~");
        q->user_answer = q_token ? atoi(q_token) : -1;
        q->is_correct = -1;
        
        free(q_data);
        q_idx++;
        token = strtok(NULL, "|");
    }
    
    current_practice.current_question = 0;
    current_practice.start_time = time(NULL);
    current_practice.is_finished = 0;
    
    show_practice_room_screen();
}

// Show practice room with questions
void show_practice_room_screen(void) {
    if (current_practice.num_questions == 0) {
        show_error_dialog("No questions in practice room");
        return;
    }
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    // Title with room info
    char title_text[256];
    snprintf(title_text, sizeof(title_text),
            "<span foreground='#2c3e50' weight='bold' size='18000'>%s</span>",
            current_practice.room_name);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), title_text);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);
    
    // Info bar
    char info_text[256];
    time_t elapsed = time(NULL) - current_practice.start_time;
    snprintf(info_text, sizeof(info_text),
            "Time: %s | Show Answers: %s | Progress: %d/%d",
            current_practice.time_limit > 0 ? 
                g_strdup_printf("%ld/%d min", elapsed/60, current_practice.time_limit) : 
                g_strdup_printf("%ld min", elapsed/60),
            current_practice.show_answers ? "Yes" : "No",
            current_practice.current_question + 1,
            current_practice.num_questions);
    
    GtkWidget *info_label = gtk_label_new(info_text);
    gtk_box_pack_start(GTK_BOX(vbox), info_label, FALSE, FALSE, 0);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    
    // Question navigation
    GtkWidget *nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    for (int i = 0; i < current_practice.num_questions; i++) {
        char btn_text[16];
        snprintf(btn_text, sizeof(btn_text), "%d", i + 1);
        
        GtkWidget *nav_btn = gtk_button_new_with_label(btn_text);
        
        // Color based on answer status
        PracticeQuestion *q = &current_practice.questions[i];
        if (q->user_answer == -1) {
            style_button(nav_btn, "#bdc3c7"); // Gray - not answered
        } else if (current_practice.show_answers) {
            if (q->is_correct == 1) {
                style_button(nav_btn, "#27ae60"); // Green - correct
            } else if (q->is_correct == 0) {
                style_button(nav_btn, "#e74c3c"); // Red - wrong
            } else {
                style_button(nav_btn, "#95a5a6"); // Gray - answered but unknown
            }
        } else {
            style_button(nav_btn, "#95a5a6"); // Gray - answered
        }
        
        g_signal_connect_swapped(nav_btn, "clicked", 
            G_CALLBACK(gtk_spin_button_set_value),
            GINT_TO_POINTER(i));
        
        gtk_box_pack_start(GTK_BOX(nav_box), nav_btn, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vbox), nav_box, FALSE, FALSE, 0);
    
    // Current question display
    PracticeQuestion *current_q = &current_practice.questions[current_practice.current_question];
    
    GtkWidget *q_frame = gtk_frame_new("Question");
    GtkWidget *q_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(q_box, 10);
    gtk_widget_set_margin_end(q_box, 10);
    gtk_widget_set_margin_top(q_box, 10);
    gtk_widget_set_margin_bottom(q_box, 10);
    
    // Question text with difficulty
    char q_text[600];
    snprintf(q_text, sizeof(q_text),
            "<b>Q%d. %s</b>\n<i>Difficulty: %s</i>",
            current_practice.current_question + 1,
            current_q->text,
            current_q->difficulty);
    
    GtkWidget *q_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(q_label), q_text);
    gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(q_label), 0);
    gtk_box_pack_start(GTK_BOX(q_box), q_label, FALSE, FALSE, 0);
    
    // Answer options with radio buttons
    GSList *group = NULL;
    GtkWidget *radio_buttons[4];
    
    for (int i = 0; i < 4; i++) {
        char opt_text[256];
        snprintf(opt_text, sizeof(opt_text), "%c. %s", 'A' + i, current_q->options[i]);
        
        radio_buttons[i] = gtk_radio_button_new_with_label(group, opt_text);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio_buttons[i]));
        
        if (current_q->user_answer == i) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_buttons[i]), TRUE);
        }
        
        g_object_set_data(G_OBJECT(radio_buttons[i]), "answer_index", GINT_TO_POINTER(i));
        g_signal_connect(radio_buttons[i], "toggled", G_CALLBACK(on_submit_practice_answer), NULL);
        
        gtk_box_pack_start(GTK_BOX(q_box), radio_buttons[i], FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(q_frame), q_box);
    gtk_box_pack_start(GTK_BOX(vbox), q_frame, TRUE, TRUE, 0);
    
    // Navigation buttons
    GtkWidget *nav_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    if (current_practice.current_question > 0) {
        GtkWidget *prev_btn = gtk_button_new_with_label("Previous");
        style_button(prev_btn, "#3498db");
        g_signal_connect_swapped(prev_btn, "clicked",
            G_CALLBACK(gtk_spin_button_set_value),
            GINT_TO_POINTER(current_practice.current_question - 1));
        gtk_box_pack_start(GTK_BOX(nav_btn_box), prev_btn, TRUE, TRUE, 0);
    }
    
    if (current_practice.current_question < current_practice.num_questions - 1) {
        GtkWidget *next_btn = gtk_button_new_with_label("➡️ Next");
        style_button(next_btn, "#3498db");
        g_signal_connect_swapped(next_btn, "clicked",
            G_CALLBACK(gtk_spin_button_set_value),
            GINT_TO_POINTER(current_practice.current_question + 1));
        gtk_box_pack_start(GTK_BOX(nav_btn_box), next_btn, TRUE, TRUE, 0);
    }
    
    gtk_box_pack_start(GTK_BOX(vbox), nav_btn_box, FALSE, FALSE, 0);
    
    // Action buttons
    GtkWidget *action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *finish_btn = gtk_button_new_with_label("FINISH PRACTICE");
    style_button(finish_btn, "#27ae60");
    g_signal_connect(finish_btn, "clicked", G_CALLBACK(on_finish_practice), NULL);
    gtk_box_pack_start(GTK_BOX(action_box), finish_btn, TRUE, TRUE, 0);
    
    GtkWidget *back_btn = gtk_button_new_with_label("EXIT");
    style_button(back_btn, "#e74c3c");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(show_practice_list_screen), NULL);
    gtk_box_pack_start(GTK_BOX(action_box), back_btn, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), action_box, FALSE, FALSE, 0);
    
    show_view(vbox);
}

// Submit practice answer callback
void on_submit_practice_answer(GtkWidget *widget, gpointer data) {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        return; // Only process when button is activated, not deactivated
    }
    
    int answer_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "answer_index"));
    int question_num = current_practice.current_question;
    
    char msg[128];
    snprintf(msg, sizeof(msg), "SUBMIT_PRACTICE_ANSWER|%d|%d|%d\n",
            current_practice.practice_id, question_num, answer_idx);
    send_message(msg);
    
    char recv_buf[256];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n > 0 && strncmp(recv_buf, "SUBMIT_PRACTICE_ANSWER_OK|", 27) == 0) {
        // Parse response: SUBMIT_PRACTICE_ANSWER_OK|question_num|answer|is_correct
        int q_num, ans, is_correct;
        sscanf(recv_buf + 27, "%d|%d|%d", &q_num, &ans, &is_correct);
        
        current_practice.questions[q_num].user_answer = ans;
        
        if (is_correct != -1) {
            current_practice.questions[q_num].is_correct = is_correct;
        }
        
        // Refresh display
        show_practice_room_screen();
    }
}

// Finish practice callback
void on_finish_practice(GtkWidget *widget, gpointer data) {
    char msg[128];
    snprintf(msg, sizeof(msg), "FINISH_PRACTICE|%d\n", current_practice.practice_id);
    send_message(msg);
    
    char recv_buf[256];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n > 0 && strncmp(recv_buf, "FINISH_PRACTICE_OK|", 19) == 0) {
        int practice_id, score, total;
        sscanf(recv_buf + 19, "%d|%d|%d", &practice_id, &score, &total);
        
        current_practice.score = score;
        current_practice.is_finished = 1;
        
        show_practice_results_screen();
    } else {
        show_error_dialog("Failed to finish practice");
    }
}

// Show practice results
void show_practice_results_screen(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    // Title
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span foreground='#2c3e50' weight='bold' size='20000'>🎉 PRACTICE RESULTS</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);
    
    // Score display
    char score_text[256];
    double percentage = (double)current_practice.score / current_practice.num_questions * 100;
    snprintf(score_text, sizeof(score_text),
            "<span size='18000'>Score: <b>%d/%d</b> (%.1f%%)</span>",
            current_practice.score, current_practice.num_questions, percentage);
    
    GtkWidget *score_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(score_label), score_text);
    gtk_box_pack_start(GTK_BOX(vbox), score_label, FALSE, FALSE, 0);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    
    // Action buttons
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *restart_btn = gtk_button_new_with_label("PRACTICE AGAIN");
    style_button(restart_btn, "#3498db");
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_restart_practice), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), restart_btn, TRUE, TRUE, 0);
    
    GtkWidget *back_btn = gtk_button_new_with_label("BACK TO LIST");
    style_button(back_btn, "#95a5a6");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(show_practice_list_screen), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), back_btn, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);
    
    show_view(vbox);
}

// Restart practice callback
void on_restart_practice(GtkWidget *widget, gpointer data) {
    int practice_id = current_practice.practice_id;
    on_join_practice(widget, GINT_TO_POINTER(practice_id));
}

// Admin: Show practice management menu - REMOVED
// This function is no longer needed as menu is now in main menu

// Wrapper for create practice button
void on_create_practice_clicked(GtkWidget *widget, gpointer data) {
    show_create_practice_dialog();
}

// Admin: Create practice room dialog
void show_create_practice_dialog(void) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Create Practice Room",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Create", GTK_RESPONSE_OK,
        "Cancel", GTK_RESPONSE_CANCEL,
        NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_margin_start(grid, 20);
    gtk_widget_set_margin_end(grid, 20);
    gtk_widget_set_margin_top(grid, 10);
    gtk_widget_set_margin_bottom(grid, 10);
    
    // Room name
    GtkWidget *name_label = gtk_label_new("Room Name:");
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Enter room name");
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);
    
    // Time limit
    GtkWidget *time_label = gtk_label_new("Time Limit (minutes, 0=unlimited):");
    GtkWidget *time_spin = gtk_spin_button_new_with_range(0, 300, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_spin), 30);
    gtk_grid_attach(GTK_GRID(grid), time_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), time_spin, 1, 1, 1, 1);
    
    // Show answers
    GtkWidget *show_label = gtk_label_new("Show Correct Answers:");
    GtkWidget *show_check = gtk_check_button_new_with_label("Yes");
    gtk_grid_attach(GTK_GRID(grid), show_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), show_check, 1, 2, 1, 1);
    
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    gtk_widget_show_all(dialog);
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_OK) {
        const char *room_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        int time_limit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(time_spin));
        int show_answers = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_check)) ? 1 : 0;
        
        if (strlen(room_name) == 0) {
            show_error_dialog("Room name cannot be empty");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "CREATE_PRACTICE|%s|%d|%d\n", 
                    room_name, time_limit, show_answers);
            send_message(msg);
            
            char recv_buf[512];
            ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
            
            if (n > 0 && strncmp(recv_buf, "CREATE_PRACTICE_OK|", 19) == 0) {
                show_info_dialog("Practice room created successfully!");
                gtk_widget_destroy(dialog);
                // Reload practice rooms to show new room
                show_manage_practice_rooms();
                return;
            } else if (n > 0 && strncmp(recv_buf, "CREATE_PRACTICE_FAIL|", 21) == 0) {
                show_error_dialog(recv_buf + 21);
            } else {
                show_error_dialog("Failed to create practice room");
            }
        }
    }
    
    gtk_widget_destroy(dialog);
}

// Admin: Manage practice rooms
void show_manage_practice_rooms(void) {
    // Load practice rooms first
    send_message("LIST_PRACTICE\n");
    
    char recv_buf[BUFFER_SIZE * 2];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n <= 0) {
        show_error_dialog("Failed to load practice rooms");
        return;
    }
    
    if (strncmp(recv_buf, "PRACTICE_ROOMS_LIST|", 20) != 0) {
        show_error_dialog("Invalid server response");
        return;
    }
    
    char *room_data = recv_buf + 20;
    parse_practice_rooms(room_data);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span foreground='#2c3e50' weight='bold' size='18000'>📋 MANAGE PRACTICE ROOMS</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    
    // Action buttons at top
    GtkWidget *top_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *create_btn = gtk_button_new_with_label("➕ CREATE ROOM");
    style_button(create_btn, "#27ae60");
    g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_practice_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(top_button_box), create_btn, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), top_button_box, FALSE, FALSE, 5);
    
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 5);
    
    // Room list
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 400);
    
    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(scroll), list_box);
    
    for (int i = 0; i < practice_room_count; i++) {
        PracticeRoomInfo *room = &practice_rooms[i];
        
        GtkWidget *room_frame = gtk_frame_new(NULL);
        GtkWidget *room_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_start(room_box, 10);
        gtk_widget_set_margin_end(room_box, 10);
        gtk_widget_set_margin_top(room_box, 10);
        gtk_widget_set_margin_bottom(room_box, 10);
        
        char room_info[512];
        snprintf(room_info, sizeof(room_info),
                "<b>%s</b> | Status: %s | Questions: %d | Active: %d",
                room->room_name,
                room->is_open ? "🟢 OPEN" : "🔴 CLOSED",
                room->num_questions,
                room->active_participants);
        
        GtkWidget *info_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(info_label), room_info);
        gtk_label_set_xalign(GTK_LABEL(info_label), 0);
        gtk_box_pack_start(GTK_BOX(room_box), info_label, FALSE, FALSE, 0);
        
        GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        
        if (room->is_open) {
            GtkWidget *close_btn = gtk_button_new_with_label("Close");
            style_button(close_btn, "#e74c3c");
            g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_practice_clicked),
                           GINT_TO_POINTER(room->practice_id));
            gtk_box_pack_start(GTK_BOX(btn_box), close_btn, TRUE, TRUE, 0);
        } else {
            GtkWidget *open_btn = gtk_button_new_with_label("Open");
            style_button(open_btn, "#27ae60");
            g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_practice_clicked),
                           GINT_TO_POINTER(room->practice_id));
            gtk_box_pack_start(GTK_BOX(btn_box), open_btn, TRUE, TRUE, 0);
        }
        
        GtkWidget *questions_btn = gtk_button_new_with_label("Questions");
        style_button(questions_btn, "#9b59b6");
        g_signal_connect(questions_btn, "clicked", G_CALLBACK(show_practice_question_manager),
                       GINT_TO_POINTER(room->practice_id));
        gtk_box_pack_start(GTK_BOX(btn_box), questions_btn, TRUE, TRUE, 0);
        
        GtkWidget *view_btn = gtk_button_new_with_label("Members");
        style_button(view_btn, "#3498db");
        g_signal_connect(view_btn, "clicked", G_CALLBACK(on_view_participants_clicked),
                       GINT_TO_POINTER(room->practice_id));
        gtk_box_pack_start(GTK_BOX(btn_box), view_btn, TRUE, TRUE, 0);
        
        GtkWidget *delete_btn = gtk_button_new_with_label("DELETE");
        style_button(delete_btn, "#c0392b");
        g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_delete_practice_clicked),
                       GINT_TO_POINTER(room->practice_id));
        gtk_box_pack_start(GTK_BOX(btn_box), delete_btn, TRUE, TRUE, 0);
        
        gtk_box_pack_start(GTK_BOX(room_box), btn_box, FALSE, FALSE, 0);
        
        gtk_container_add(GTK_CONTAINER(room_frame), room_box);
        gtk_box_pack_start(GTK_BOX(list_box), room_frame, FALSE, FALSE, 0);
    }
    
    if (practice_room_count == 0) {
        GtkWidget *no_rooms = gtk_label_new("No practice rooms found");
        gtk_box_pack_start(GTK_BOX(list_box), no_rooms, TRUE, TRUE, 0);
    }
    
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    GtkWidget *back_btn = gtk_button_new_with_label("BACK TO MAIN MENU");
    style_button(back_btn, "#95a5a6");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), back_btn, FALSE, FALSE, 0);
    
    show_view(vbox);
}

// Admin callbacks
void on_close_practice_clicked(GtkWidget *widget, gpointer data) {
    int practice_id = GPOINTER_TO_INT(data);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "CLOSE_PRACTICE|%d\n", practice_id);
    send_message(msg);
    
    char recv_buf[256];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n > 0 && strncmp(recv_buf, "CLOSE_PRACTICE_OK|", 18) == 0) {
        show_info_dialog("Practice room closed successfully");
        show_manage_practice_rooms();
    } else {
        show_error_dialog("Failed to close practice room");
    }
}

void on_open_practice_clicked(GtkWidget *widget, gpointer data) {
    int practice_id = GPOINTER_TO_INT(data);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "OPEN_PRACTICE|%d\n", practice_id);
    send_message(msg);
    
    char recv_buf[256];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n > 0 && strncmp(recv_buf, "OPEN_PRACTICE_OK|", 17) == 0) {
        show_info_dialog("Practice room opened successfully");
        show_manage_practice_rooms();
    } else {
        show_error_dialog("Failed to open practice room");
    }
}

void on_view_participants_clicked(GtkWidget *widget, gpointer data) {
    int practice_id = GPOINTER_TO_INT(data);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "PRACTICE_PARTICIPANTS|%d\n", practice_id);
    send_message(msg);
    
    char recv_buf[BUFFER_SIZE];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n <= 0 || strncmp(recv_buf, "PRACTICE_PARTICIPANTS|", 22) != 0) {
        show_error_dialog("Failed to load participants");
        return;
    }
    
    // Parse: PRACTICE_PARTICIPANTS|practice_id|user_id,username,score,answered,total;...
    char *response_data = recv_buf + 22;
    char *practice_id_str = strtok(response_data, "|");
    (void)practice_id_str; // Suppress unused warning
    char *participants_data = strtok(NULL, "|");
    
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "Practice Room Participants");
    
    char msg_text[BUFFER_SIZE] = "";
    
    if (participants_data && strcmp(participants_data, "NONE\n") != 0) {
        strcat(msg_text, "Username | Score | Progress\n");
        strcat(msg_text, "─────────────────────────\n");
        
        char *token = strtok(participants_data, ";");
        while (token != NULL) {
            int user_id, score, answered, total;
            char username[50];
            
            sscanf(token, "%d,%49[^,],%d,%d,%d", &user_id, username, &score, &answered, &total);
            
            char line[256];
            snprintf(line, sizeof(line), "%s | %d/%d | %d/%d\n",
                    username, score, total, answered, total);
            strcat(msg_text, line);
            
            token = strtok(NULL, ";");
        }
    } else {
        strcat(msg_text, "No active participants");
    }
    
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", msg_text);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Delete practice room callback
void on_delete_practice_clicked(GtkWidget *widget, gpointer data) {
    int practice_id = GPOINTER_TO_INT(data);
    
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Delete this practice room?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This action cannot be undone. All data will be permanently deleted.");
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response != GTK_RESPONSE_YES) {
        return;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "DELETE_PRACTICE|%d\n", practice_id);
    send_message(msg);
    
    char recv_buf[256];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    GtkWidget *result_dialog;
    if (n > 0 && strncmp(recv_buf, "DELETE_PRACTICE_OK|", 19) == 0) {
        result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_OK,
                                       "Practice room deleted successfully!");
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
        
        // Refresh practice rooms list
        show_manage_practice_rooms();
    } else {
        char *error_msg = strchr(recv_buf, '|');
        if (error_msg) {
            error_msg++;
        } else {
            error_msg = "Failed to delete practice room";
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
