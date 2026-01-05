#include "practice_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include "question_ui.h"
#include "broadcast.h"
#include <string.h>
#include <stdlib.h>

// Forward declarations
void create_main_menu(void);
void show_error_dialog(const char *message);
void show_info_dialog(const char *message);
void on_finish_practice(GtkWidget *widget, gpointer data);

PracticeSession current_practice = {0};
PracticeRoomInfo practice_rooms[50];
int practice_room_count = 0;

// Timer label for countdown
static GtkWidget *practice_timer_label = NULL;

// Timer callback - countdown
static gboolean update_practice_timer(gpointer data) {
    if (!current_practice.start_time || current_practice.time_limit <= 0) {
        return FALSE;
    }
    
    time_t now = time(NULL);
    long elapsed = now - current_practice.start_time;
    long remaining = (current_practice.time_limit * 60) - elapsed;
    
    if (remaining <= 0) {
        // Time's up - auto finish
        if (practice_timer_label && GTK_IS_WIDGET(practice_timer_label)) {
            gtk_label_set_markup(GTK_LABEL(practice_timer_label),
                "<span foreground='red' weight='bold' size='14000'>⏰ TIME'S UP!</span>");
        }
        current_practice.timer_id = 0;
        on_finish_practice(NULL, NULL);
        return FALSE;
    }
    
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    
    char timer_text[128];
    const char *color = (remaining < 300) ? "red" : "#2c3e50"; // Red if < 5 minutes
    snprintf(timer_text, sizeof(timer_text),
             "<span foreground='%s' weight='bold' size='14000'>⏱️ Time: %02d:%02d</span>",
             color, minutes, seconds);
    
    if (practice_timer_label && GTK_IS_WIDGET(practice_timer_label)) {
        gtk_label_set_markup(GTK_LABEL(practice_timer_label), timer_text);
    }
    
    return TRUE; // Continue timer
}

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

// Broadcast callback
static void on_practice_closed_broadcast(int practice_id, const char *room_name);

// Internal helpers
static void on_practice_nav_clicked(GtkWidget *widget, gpointer data);
static void on_practice_change_question(int new_index);

// Determine navigation button color based on question status
static const char *practice_question_status_color(const PracticeQuestion *q) {
    if (!q) {
        return "#bdc3c7"; // Default gray
    }

    // No answer chosen yet
    if (q->user_answer == -1) {
        return "#bdc3c7";
    }

    // While doing practice (not finished yet)
    if (!current_practice.is_finished) {
        // Admin allows showing answers AND server has returned correctness
        if (current_practice.show_answers && q->is_correct != -1) {
            return (q->is_correct == 1) ? "#27ae60" : "#e74c3c";
        }

        // Either admin did not enable show_answers or we don't have result yet
        // → blue when an answer has been chosen
        return "#3498db";
    }

    // After finish: always show based on result
    if (q->is_correct == 1) {
        return "#27ae60"; // Green - correct
    }
    if (q->is_correct == 0) {
        return "#e74c3c"; // Red - wrong
    }

    // Finished but no correctness info → gray
    return "#bdc3c7";
}

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

// Cleanup practice session state
void cleanup_practice_session(void) {
    // Stop timer if running
    if (current_practice.timer_id > 0) {
        g_source_remove(current_practice.timer_id);
        current_practice.timer_id = 0;
    }
    
    // Stop broadcast listener
    if (broadcast_is_listening()) {
        broadcast_stop_listener();
    }
    
    // Clear timer label reference
    practice_timer_label = NULL;
    
    // Clear practice data
    memset(&current_practice, 0, sizeof(current_practice));
}

// Show practice room list for users
void show_practice_list_screen(void) {
    // Cleanup current practice session if any
    cleanup_practice_session();
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
        char time_text[64];
        if (room->time_limit > 0) {
            snprintf(time_text, sizeof(time_text), "%d min", room->time_limit);
        } else {
            snprintf(time_text, sizeof(time_text), "Unlimited");
        }

        snprintf(info_text, sizeof(info_text),
                "Creator: %s | Questions: %d | Time: %s | Show Answers: %s | Participants: %d",
                room->creator_name,
                room->num_questions,
                time_text,
                room->show_answers ? "Yes" : "No",
                room->active_participants);
        
        GtkWidget *info_label = gtk_label_new(info_text);
        gtk_label_set_xalign(GTK_LABEL(info_label), 0);
        gtk_box_pack_start(GTK_BOX(room_box), info_label, FALSE, FALSE, 0);
        
        // Action buttons row
        GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

        // Join button (only when room is open and has questions)
        if (room->is_open && room->num_questions > 0) {
            GtkWidget *join_btn = gtk_button_new_with_label("▶️ JOIN PRACTICE");
            style_button(join_btn, "#27ae60");
            g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_practice), 
                           GINT_TO_POINTER(room->practice_id));
            gtk_box_pack_start(GTK_BOX(btn_row), join_btn, TRUE, TRUE, 0);
        }

        // View last results details (for current user)
        GtkWidget *details_btn = gtk_button_new_with_label("VIEW DETAILS");
        style_button(details_btn, "#9b59b6");
        g_signal_connect(details_btn, "clicked", G_CALLBACK(on_view_practice_results),
                       GINT_TO_POINTER(room->practice_id));
        gtk_box_pack_start(GTK_BOX(btn_row), details_btn, TRUE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(room_box), btn_row, FALSE, FALSE, 0);
        
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
    if (!token) {
        show_error_dialog("Invalid practice data (missing id)");
        return;
    }
    current_practice.practice_id = atoi(token);

    token = strtok(NULL, "|");
    if (!token) {
        show_error_dialog("Invalid practice data (missing name)");
        return;
    }
    strncpy(current_practice.room_name, token, sizeof(current_practice.room_name) - 1);

    token = strtok(NULL, "|");
    if (!token) {
        show_error_dialog("Invalid practice data (missing time limit)");
        return;
    }
    current_practice.time_limit = atoi(token);

    token = strtok(NULL, "|");
    if (!token) {
        show_error_dialog("Invalid practice data (missing show_answers)");
        return;
    }
    current_practice.show_answers = atoi(token);

    token = strtok(NULL, "|");
    if (!token) {
        show_error_dialog("Invalid practice data (missing question count)");
        return;
    }
    current_practice.num_questions = atoi(token);

    token = strtok(NULL, "|");
    if (!token) {
        show_error_dialog("Invalid practice data (missing session id)");
        return;
    }
    current_practice.session_id = atoi(token);

    // Parse questions: id~text~opt1~opt2~opt3~opt4~difficulty~user_answer
    int q_idx = 0;
    token = strtok(NULL, "|");
    while (token != NULL && q_idx < current_practice.num_questions) {
        PracticeQuestion *q = &current_practice.questions[q_idx];

        char *q_data = strdup(token);
        if (!q_data) {
            break;
        }

        char *saveptr = NULL;
        char *id_str = strtok_r(q_data, "~", &saveptr);
        char *text = strtok_r(NULL, "~", &saveptr);
        char *opt1 = strtok_r(NULL, "~", &saveptr);
        char *opt2 = strtok_r(NULL, "~", &saveptr);
        char *opt3 = strtok_r(NULL, "~", &saveptr);
        char *opt4 = strtok_r(NULL, "~", &saveptr);
        char *difficulty = strtok_r(NULL, "~", &saveptr);
        char *user_ans_str = strtok_r(NULL, "~", &saveptr);

        // Require at least id, text, and 4 options to consider this a valid question
        if (!id_str || !text || !opt1 || !opt2 || !opt3 || !opt4) {
            free(q_data);
            break;
        }

        q->id = atoi(id_str);
        strncpy(q->text, text, sizeof(q->text) - 1);
        strncpy(q->options[0], opt1, sizeof(q->options[0]) - 1);
        strncpy(q->options[1], opt2, sizeof(q->options[1]) - 1);
        strncpy(q->options[2], opt3, sizeof(q->options[2]) - 1);
        strncpy(q->options[3], opt4, sizeof(q->options[3]) - 1);

        if (difficulty) {
            strncpy(q->difficulty, difficulty, sizeof(q->difficulty) - 1);
        }

        q->user_answer = user_ans_str ? atoi(user_ans_str) : -1;
        q->is_correct = -1;

        free(q_data);
        q_idx++;
        token = strtok(NULL, "|");
    }

    // Adjust actual question count to what we successfully parsed
    current_practice.num_questions = q_idx;

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
    
    // Main horizontal layout
    GtkWidget *main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_widget_set_margin_top(main_hbox, 20);
    gtk_widget_set_margin_start(main_hbox, 20);
    gtk_widget_set_margin_end(main_hbox, 20);
    gtk_widget_set_margin_bottom(main_hbox, 20);
    
    // LEFT PANEL: Navigation (5 numbers per row)
    GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(left_panel, 180, -1);
    
    // Header with title and timer
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    
    // Title with room info
    char title_text[256];
    snprintf(title_text, sizeof(title_text),
            "<span foreground='#2c3e50' weight='bold' size='14000'>%s</span>",
            current_practice.room_name);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), title_text);
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    
    // Timer label (only if time limit exists)
    if (current_practice.time_limit > 0) {
        practice_timer_label = gtk_label_new(NULL);
        gtk_box_pack_start(GTK_BOX(header_box), practice_timer_label, FALSE, FALSE, 0);
        
        // Start timer if not already running
        if (current_practice.timer_id == 0) {
            current_practice.timer_id = g_timeout_add(1000, update_practice_timer, NULL);
            update_practice_timer(NULL); // Update immediately
        }
    } else {
        // Show elapsed time for unlimited practice
        time_t elapsed = time(NULL) - current_practice.start_time;
        char time_text[128];
        snprintf(time_text, sizeof(time_text),
                "<span foreground='#2c3e50' weight='bold' size='12000'>⏱️ Time: %ld min</span>",
                elapsed / 60);
        practice_timer_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(practice_timer_label), time_text);
        gtk_box_pack_start(GTK_BOX(header_box), practice_timer_label, FALSE, FALSE, 0);
    }
    
    gtk_box_pack_start(GTK_BOX(left_panel), header_box, FALSE, FALSE, 0);
    
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(left_panel), sep1, FALSE, FALSE, 0);
    
    // Navigation grid (5 per row)
    GtkWidget *nav_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nav_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    GtkWidget *nav_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(nav_vbox, 5);
    gtk_widget_set_margin_end(nav_vbox, 5);
    
    int rows = (current_practice.num_questions + 4) / 5;
    for (int row = 0; row < rows; row++) {
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        
        for (int col = 0; col < 5; col++) {
            int idx = row * 5 + col;
            if (idx >= current_practice.num_questions) break;
            
            char btn_text[16];
            snprintf(btn_text, sizeof(btn_text), "%d", idx + 1);
            
            GtkWidget *nav_btn = gtk_button_new_with_label(btn_text);
            gtk_widget_set_size_request(nav_btn, 45, 40);

            // Color based on answer status
            PracticeQuestion *q = &current_practice.questions[idx];
            style_button(nav_btn, practice_question_status_color(q));
            
            g_signal_connect(nav_btn, "clicked", 
                G_CALLBACK(on_practice_nav_clicked),
                GINT_TO_POINTER(idx));
            
            gtk_box_pack_start(GTK_BOX(row_box), nav_btn, TRUE, TRUE, 0);
        }
        
        gtk_box_pack_start(GTK_BOX(nav_vbox), row_box, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(nav_scroll), nav_vbox);
    gtk_box_pack_start(GTK_BOX(left_panel), nav_scroll, TRUE, TRUE, 0);
    
    // Action buttons at bottom of left panel
    GtkWidget *action_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    
    GtkWidget *finish_btn = gtk_button_new_with_label("FINISH PRACTICE");
    gtk_widget_set_size_request(finish_btn, -1, 40);
    style_button(finish_btn, "#27ae60");
    g_signal_connect(finish_btn, "clicked", G_CALLBACK(on_finish_practice), NULL);
    gtk_box_pack_start(GTK_BOX(action_box), finish_btn, FALSE, FALSE, 0);
    
    GtkWidget *back_btn = gtk_button_new_with_label("EXIT");
    gtk_widget_set_size_request(back_btn, -1, 35);
    style_button(back_btn, "#e74c3c");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(show_practice_list_screen), NULL);
    gtk_box_pack_start(GTK_BOX(action_box), back_btn, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(left_panel), action_box, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_hbox), left_panel, FALSE, FALSE, 0);
    
    // Separator
    GtkWidget *sep_v = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_hbox), sep_v, FALSE, FALSE, 0);
    
    // RIGHT PANEL: Current question
    GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    // Progress info
    char progress_text[128];
    snprintf(progress_text, sizeof(progress_text),
            "<span size='11000'>Question <b>%d</b> of <b>%d</b></span>",
            current_practice.current_question + 1, current_practice.num_questions);
    GtkWidget *progress_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(progress_label), progress_text);
    gtk_box_pack_start(GTK_BOX(right_panel), progress_label, FALSE, FALSE, 0);
    
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(right_panel), sep2, FALSE, FALSE, 0);
    
    // Current question display
    PracticeQuestion *current_q = &current_practice.questions[current_practice.current_question];
    
    GtkWidget *q_frame = gtk_frame_new("Question");
    GtkWidget *q_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(q_box, 15);
    gtk_widget_set_margin_end(q_box, 15);
    gtk_widget_set_margin_top(q_box, 10);
    gtk_widget_set_margin_bottom(q_box, 10);
    
    // Question text with difficulty
    char q_text[700];
    const char *diff_color = strcmp(current_q->difficulty, "Easy") == 0 ? "#27ae60" :
                            strcmp(current_q->difficulty, "Hard") == 0 ? "#e74c3c" : "#f39c12";
    snprintf(q_text, sizeof(q_text),
            "<span foreground='#2c3e50' weight='bold' size='12000'>Q%d. %s</span>\n"
            "<span foreground='%s' size='10000'><i>Difficulty: %s</i></span>",
            current_practice.current_question + 1, current_q->text,
            diff_color, current_q->difficulty);
    
    GtkWidget *q_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(q_label), q_text);
    gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(q_label), 0);
    gtk_box_pack_start(GTK_BOX(q_box), q_label, FALSE, FALSE, 0);
    
    // Answer options with radio buttons
    GtkWidget *radio_buttons[4];
    
    // Create dummy radio button (hidden) to prevent auto-selection
    GtkWidget *dummy_radio = gtk_radio_button_new(NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dummy_radio), TRUE);
    gtk_widget_set_no_show_all(dummy_radio, TRUE);
    gtk_widget_hide(dummy_radio);
    gtk_box_pack_start(GTK_BOX(q_box), dummy_radio, FALSE, FALSE, 0);
    
    GSList *group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(dummy_radio));
    
    for (int i = 0; i < 4; i++) {
        char opt_text[256];
        snprintf(opt_text, sizeof(opt_text), "%c. %s", 'A' + i, current_q->options[i]);
        
        radio_buttons[i] = gtk_radio_button_new_with_label(group, opt_text);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio_buttons[i]));
        
        // Only set active if user has previously answered this question
        if (current_q->user_answer == i) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_buttons[i]), TRUE);
        }

        // Color the option for the current question based on selection/result
        GtkWidget *child = gtk_bin_get_child(GTK_BIN(radio_buttons[i]));
        if (GTK_IS_LABEL(child)) {
            const char *color = "#2c3e50";
            const char *weight = "normal";

            if (current_q->user_answer != -1 && !current_practice.is_finished && current_q->user_answer == i) {
                // While doing practice, highlight selected option
                if (current_practice.show_answers && current_q->is_correct != -1) {
                    // Admin shows answers and we have result
                    color = (current_q->is_correct == 1) ? "#27ae60" : "#e74c3c";
                } else {
                    // No result yet or show_answers disabled -> blue
                    color = "#3498db";
                }
                weight = "bold";
            }

            char markup[320];
            snprintf(markup, sizeof(markup),
                     "<span foreground='%s' weight='%s'>%s</span>",
                     color, weight, opt_text);
            gtk_label_set_markup(GTK_LABEL(child), markup);
        }

        g_object_set_data(G_OBJECT(radio_buttons[i]), "answer_index", GINT_TO_POINTER(i));
        g_signal_connect(radio_buttons[i], "toggled", G_CALLBACK(on_submit_practice_answer), NULL);

        gtk_box_pack_start(GTK_BOX(q_box), radio_buttons[i], FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(q_frame), q_box);
    gtk_box_pack_start(GTK_BOX(right_panel), q_frame, TRUE, TRUE, 0);
    
    // Navigation buttons (Previous/Next)
    GtkWidget *nav_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    if (current_practice.current_question > 0) {
        GtkWidget *prev_btn = gtk_button_new_with_label("⬅️ Previous");
        style_button(prev_btn, "#95a5a6");
        g_signal_connect(prev_btn, "clicked",
            G_CALLBACK(on_practice_nav_clicked),
            GINT_TO_POINTER(current_practice.current_question - 1));
        gtk_box_pack_start(GTK_BOX(nav_btn_box), prev_btn, TRUE, TRUE, 0);
    }
    
    if (current_practice.current_question < current_practice.num_questions - 1) {
        GtkWidget *next_btn = gtk_button_new_with_label("Next ➡️");
        style_button(next_btn, "#3498db");
        g_signal_connect(next_btn, "clicked",
            G_CALLBACK(on_practice_nav_clicked),
            GINT_TO_POINTER(current_practice.current_question + 1));
        gtk_box_pack_start(GTK_BOX(nav_btn_box), next_btn, TRUE, TRUE, 0);
    }
    
    gtk_box_pack_start(GTK_BOX(right_panel), nav_btn_box, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_hbox), right_panel, TRUE, TRUE, 0);

    // Ensure we listen for PRACTICE_CLOSED broadcasts while in practice mode
    broadcast_on_practice_closed(on_practice_closed_broadcast);
    broadcast_start_listener();

    show_view(main_hbox);
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
    // Stop timer if running
    if (current_practice.timer_id > 0) {
        g_source_remove(current_practice.timer_id);
        current_practice.timer_id = 0;
    }
    
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
    // Set flag to indicate we're in results mode
    current_practice.is_finished = 1;
    
    // Main horizontal layout
    GtkWidget *main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_widget_set_margin_top(main_hbox, 20);
    gtk_widget_set_margin_start(main_hbox, 20);
    gtk_widget_set_margin_end(main_hbox, 20);
    gtk_widget_set_margin_bottom(main_hbox, 20);
    
    // LEFT PANEL: Navigation and summary
    GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(left_panel, 180, -1);
    
    // Header
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span foreground='#2c3e50' weight='bold' size='14000'>🎉 RESULTS</span>");
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    
    // Score display
    char score_text[128];
    double percentage = (double)current_practice.score / current_practice.num_questions * 100;
    snprintf(score_text, sizeof(score_text),
            "<span size='12000'><b>%d/%d</b>\n%.1f%%</span>",
            current_practice.score, current_practice.num_questions, percentage);
    GtkWidget *score_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(score_label), score_text);
    gtk_box_pack_start(GTK_BOX(header_box), score_label, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(left_panel), header_box, FALSE, FALSE, 0);
    
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(left_panel), sep1, FALSE, FALSE, 0);
    
    // Navigation grid (5 per row)
    GtkWidget *nav_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nav_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    GtkWidget *nav_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(nav_vbox, 5);
    gtk_widget_set_margin_end(nav_vbox, 5);
    
    int rows = (current_practice.num_questions + 4) / 5;
    for (int row = 0; row < rows; row++) {
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        
        for (int col = 0; col < 5; col++) {
            int idx = row * 5 + col;
            if (idx >= current_practice.num_questions) break;
            
            char btn_text[16];
            snprintf(btn_text, sizeof(btn_text), "%d", idx + 1);
            
            GtkWidget *nav_btn = gtk_button_new_with_label(btn_text);
            gtk_widget_set_size_request(nav_btn, 45, 40);

            // Color based on answer status (always result-based after finish)
            PracticeQuestion *q = &current_practice.questions[idx];
            style_button(nav_btn, practice_question_status_color(q));
            
            g_signal_connect(nav_btn, "clicked",
                G_CALLBACK(on_practice_nav_clicked),
                GINT_TO_POINTER(idx));
            
            gtk_box_pack_start(GTK_BOX(row_box), nav_btn, TRUE, TRUE, 0);
        }
        
        gtk_box_pack_start(GTK_BOX(nav_vbox), row_box, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(nav_scroll), nav_vbox);
    gtk_box_pack_start(GTK_BOX(left_panel), nav_scroll, TRUE, TRUE, 0);
    
    // Action buttons
    GtkWidget *action_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    
    GtkWidget *restart_btn = gtk_button_new_with_label("PRACTICE AGAIN");
    gtk_widget_set_size_request(restart_btn, -1, 40);
    style_button(restart_btn, "#3498db");
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_restart_practice), NULL);
    gtk_box_pack_start(GTK_BOX(action_box), restart_btn, FALSE, FALSE, 0);
    
    GtkWidget *back_btn = gtk_button_new_with_label("BACK TO LIST");
    gtk_widget_set_size_request(back_btn, -1, 35);
    style_button(back_btn, "#95a5a6");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(show_practice_list_screen), NULL);
    gtk_box_pack_start(GTK_BOX(action_box), back_btn, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(left_panel), action_box, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_hbox), left_panel, FALSE, FALSE, 0);
    
    // Separator
    GtkWidget *sep_v = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_hbox), sep_v, FALSE, FALSE, 0);
    
    // RIGHT PANEL: Current question details
    GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    // Progress info
    char progress_text[128];
    snprintf(progress_text, sizeof(progress_text),
            "<span size='11000'>Question <b>%d</b> of <b>%d</b></span>",
            current_practice.current_question + 1, current_practice.num_questions);
    GtkWidget *progress_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(progress_label), progress_text);
    gtk_box_pack_start(GTK_BOX(right_panel), progress_label, FALSE, FALSE, 0);
    
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(right_panel), sep2, FALSE, FALSE, 0);
    
    // Current question display
    PracticeQuestion *current_q = &current_practice.questions[current_practice.current_question];
    
    GtkWidget *q_frame = gtk_frame_new("Question");
    GtkWidget *q_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(q_box, 15);
    gtk_widget_set_margin_end(q_box, 15);
    gtk_widget_set_margin_top(q_box, 10);
    gtk_widget_set_margin_bottom(q_box, 10);
    
    // Question text with difficulty and result
    char q_text[1024];
    const char *diff_color = strcmp(current_q->difficulty, "Easy") == 0 ? "#27ae60" :
                            strcmp(current_q->difficulty, "Hard") == 0 ? "#e74c3c" : "#f39c12";
    
    const char *result_icon = "";
    const char *result_color = "#2c3e50";
    if (current_q->user_answer == -1) {
        result_icon = "⚪";
        result_color = "#bdc3c7";
    } else if (current_q->is_correct == 1) {
        result_icon = "✅";
        result_color = "#27ae60";
    } else if (current_q->is_correct == 0) {
        result_icon = "❌";
        result_color = "#e74c3c";
    }
    
    snprintf(q_text, sizeof(q_text),
            "<span foreground='%s' size='14000'><b>%s</b></span> "
            "<span foreground='#2c3e50' weight='bold' size='12000'>Q%d.</span>\n"
            "<span foreground='%s' size='10000'><i>Difficulty: %s</i></span>",
            result_color, result_icon,
            current_practice.current_question + 1,
            diff_color, current_q->difficulty);
    
    GtkWidget *q_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(q_label), q_text);
    gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(q_label), 0);
    gtk_box_pack_start(GTK_BOX(q_box), q_label, FALSE, FALSE, 0);
    
    // Question text content
    char q_content[600];
    snprintf(q_content, sizeof(q_content),
            "<span foreground='#2c3e50' size='11000'>%s</span>",
            current_q->text);
    GtkWidget *q_content_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(q_content_label), q_content);
    gtk_label_set_line_wrap(GTK_LABEL(q_content_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(q_content_label), 0);
    gtk_widget_set_margin_top(q_content_label, 5);
    gtk_box_pack_start(GTK_BOX(q_box), q_content_label, FALSE, FALSE, 0);
    
    // Show answer options with colors
    for (int i = 0; i < 4; i++) {
        char opt_text[512];
        const char *opt_color = "#2c3e50";
        const char *opt_weight = "normal";
        const char *opt_prefix = "";
        
        // Check if this is user's answer
        gboolean is_user_answer = (current_q->user_answer == i);
        
        if (is_user_answer && current_q->is_correct == 1) {
            // User's answer and correct
            opt_color = "#27ae60";
            opt_weight = "bold";
            opt_prefix = "✅ ";
        } else if (is_user_answer && current_q->is_correct == 0) {
            // User's answer and wrong
            opt_color = "#e74c3c";
            opt_weight = "bold";
            opt_prefix = "❌ ";
        } else if (is_user_answer) {
            // User's answer (no correctness info)
            opt_color = "#3498db";
            opt_weight = "bold";
            opt_prefix = "👉 ";
        }
        
        snprintf(opt_text, sizeof(opt_text),
                "<span foreground='%s' weight='%s' size='11000'>%s%c. %s</span>",
                opt_color, opt_weight, opt_prefix, 'A' + i, current_q->options[i]);
        
        GtkWidget *opt_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(opt_label), opt_text);
        gtk_label_set_line_wrap(GTK_LABEL(opt_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(opt_label), 0);
        gtk_widget_set_margin_start(opt_label, 10);
        gtk_box_pack_start(GTK_BOX(q_box), opt_label, FALSE, FALSE, 3);
    }
    
    gtk_container_add(GTK_CONTAINER(q_frame), q_box);
    gtk_box_pack_start(GTK_BOX(right_panel), q_frame, TRUE, TRUE, 0);
    
    // Navigation buttons (Previous/Next)
    GtkWidget *nav_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    if (current_practice.current_question > 0) {
        GtkWidget *prev_btn = gtk_button_new_with_label("⬅️ Previous");
        style_button(prev_btn, "#95a5a6");
        g_signal_connect(prev_btn, "clicked",
            G_CALLBACK(on_practice_nav_clicked),
            GINT_TO_POINTER(current_practice.current_question - 1));
        gtk_box_pack_start(GTK_BOX(nav_btn_box), prev_btn, TRUE, TRUE, 0);
    }
    
    if (current_practice.current_question < current_practice.num_questions - 1) {
        GtkWidget *next_btn = gtk_button_new_with_label("Next ➡️");
        style_button(next_btn, "#3498db");
        g_signal_connect(next_btn, "clicked",
            G_CALLBACK(on_practice_nav_clicked),
            GINT_TO_POINTER(current_practice.current_question + 1));
        gtk_box_pack_start(GTK_BOX(nav_btn_box), next_btn, TRUE, TRUE, 0);
    }
    
    gtk_box_pack_start(GTK_BOX(right_panel), nav_btn_box, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_hbox), right_panel, TRUE, TRUE, 0);
    
    show_view(main_hbox);
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

// ===== Practice broadcast handler =====

static void on_practice_closed_broadcast(int practice_id, const char *room_name) {
    // If we're not in this practice, ignore
    if (current_practice.practice_id != practice_id) {
        return;
    }

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "Practice Room Closed");

    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "The practice room '%s' has been closed by the admin.\nYou will be returned to the practice room list.",
        room_name ? room_name : "");

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    // Go back to practice list
    show_practice_list_screen();
}

// ===== Detailed practice results =====

void on_view_practice_results(GtkWidget *widget, gpointer data) {
    (void)widget;

    char msg[128];
    int practice_id = current_practice.practice_id;
    if (data != NULL) {
        int from_data = GPOINTER_TO_INT(data);
        if (from_data > 0) {
            practice_id = from_data;
        }
    }

    snprintf(msg, sizeof(msg), "VIEW_PRACTICE_RESULTS|%d\n", practice_id);
    send_message(msg);

    char recv_buf[BUFFER_SIZE * 2];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    
    if (n > 0 && strncmp(recv_buf, "PRACTICE_RESULTS_FAIL|", 21) == 0) {
        show_error_dialog(recv_buf + 21);
        return;
    }

    if (n <= 0 || strncmp(recv_buf, "PRACTICE_RESULTS|", 17) != 0) {
        show_error_dialog("Failed to load practice results");
        return;
    }

    // Parse: PRACTICE_RESULTS|practice_id|score|total|qdata|...
    char *buffer = recv_buf;
    char *saveptr1;
    strtok_r(buffer, "|", &saveptr1); // PRACTICE_RESULTS
    char *practice_id_str = strtok_r(NULL, "|", &saveptr1);
    char *score_str = strtok_r(NULL, "|", &saveptr1);
    char *total_str = strtok_r(NULL, "|", &saveptr1);

    if (!practice_id_str || !score_str || !total_str) {
        show_error_dialog("Invalid results data");
        return;
    }

    int practice_id_resp = atoi(practice_id_str);
    int score = atoi(score_str);
    int total = atoi(total_str);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);

    // Title
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span foreground='#2c3e50' weight='bold' size='20000'>Practice Detailed Results</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    // Summary
    double percentage = total > 0 ? (double)score / total * 100.0 : 0.0;
    char summary_text[256];
    snprintf(summary_text, sizeof(summary_text),
             "<span size='16000'>Score: <b>%d/%d</b> (%.1f%%)</span>",
             score, total, percentage);
    GtkWidget *summary_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(summary_label), summary_text);
    gtk_box_pack_start(GTK_BOX(vbox), summary_label, FALSE, FALSE, 0);

    // Optional difficulty stats (only if this is current practice)
    if (practice_id_resp == current_practice.practice_id && current_practice.num_questions > 0) {
        int easy_total = 0, medium_total = 0, hard_total = 0;
        int easy_correct = 0, medium_correct = 0, hard_correct = 0;
        for (int i = 0; i < current_practice.num_questions; i++) {
            PracticeQuestion *q = &current_practice.questions[i];
            if (strcmp(q->difficulty, "Easy") == 0) {
                easy_total++;
                if (q->is_correct == 1) easy_correct++;
            } else if (strcmp(q->difficulty, "Medium") == 0) {
                medium_total++;
                if (q->is_correct == 1) medium_correct++;
            } else if (strcmp(q->difficulty, "Hard") == 0) {
                hard_total++;
                if (q->is_correct == 1) hard_correct++;
            }
        }

        if (easy_total + medium_total + hard_total > 0) {
            char diff_stats[256];
            snprintf(diff_stats, sizeof(diff_stats),
                     "<span size='12000'>Easy: <b>%d/%d</b>  |  Medium: <b>%d/%d</b>  |  Hard: <b>%d/%d</b></span>",
                     easy_correct, easy_total,
                     medium_correct, medium_total,
                     hard_correct, hard_total);
            GtkWidget *diff_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(diff_label), diff_stats);
            gtk_box_pack_start(GTK_BOX(vbox), diff_label, FALSE, FALSE, 0);
        }
        
        // Add navigation for current practice questions
        GtkWidget *sep_nav = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(vbox), sep_nav, FALSE, FALSE, 0);
        
        GtkWidget *nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        for (int i = 0; i < current_practice.num_questions; i++) {
            char btn_text[16];
            snprintf(btn_text, sizeof(btn_text), "%d", i + 1);

            GtkWidget *nav_btn = gtk_button_new_with_label(btn_text);

            PracticeQuestion *q = &current_practice.questions[i];
            style_button(nav_btn, practice_question_status_color(q));

            gtk_box_pack_start(GTK_BOX(nav_box), nav_btn, TRUE, TRUE, 0);
        }
        gtk_box_pack_start(GTK_BOX(vbox), nav_box, FALSE, FALSE, 0);
    }

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 5);

    // Scrolled window for questions
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);

    GtkWidget *q_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(scroll), q_list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    int q_index = 0;
    char *q_token;
    while ((q_token = strtok_r(NULL, "|", &saveptr1)) != NULL) {
        char q_copy[BUFFER_SIZE];
        strncpy(q_copy, q_token, sizeof(q_copy) - 1);
        q_copy[sizeof(q_copy) - 1] = '\0';

        char *saveptr2;
        char *id_str = strtok_r(q_copy, "~", &saveptr2);
        char *text = strtok_r(NULL, "~", &saveptr2);
        char *optA = strtok_r(NULL, "~", &saveptr2);
        char *optB = strtok_r(NULL, "~", &saveptr2);
        char *optC = strtok_r(NULL, "~", &saveptr2);
        char *optD = strtok_r(NULL, "~", &saveptr2);
        char *correct_str = strtok_r(NULL, "~", &saveptr2);
        char *user_ans_str = strtok_r(NULL, "~", &saveptr2);
        char *is_correct_str = strtok_r(NULL, "~", &saveptr2);

        if (!id_str || !text || !optA || !optB || !optC || !optD ||
            !correct_str || !user_ans_str || !is_correct_str) {
            continue;
        }

        int correct = atoi(correct_str);
        int user_ans = atoi(user_ans_str);
        int is_correct = atoi(is_correct_str);
        (void)is_correct; // not strictly needed for per-option colors

        GtkWidget *q_frame = gtk_frame_new(NULL);
        GtkWidget *q_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_start(q_box, 10);
        gtk_widget_set_margin_end(q_box, 10);
        gtk_widget_set_margin_top(q_box, 10);
        gtk_widget_set_margin_bottom(q_box, 10);

        char q_title[512];
        snprintf(q_title, sizeof(q_title),
                 "<b>Q%d.</b> %s", q_index + 1, text);
        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_title);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0);
        gtk_box_pack_start(GTK_BOX(q_box), q_label, FALSE, FALSE, 0);

        const char *options[4] = { optA, optB, optC, optD };
        for (int i = 0; i < 4; i++) {
            const char *opt_text = options[i] ? options[i] : "";
            const char *color = "#2c3e50";
            const char *prefix = "";

            if (i == correct) {
                color = "#27ae60"; // green for correct answer
                prefix = "✅ ";
            } else if (i == user_ans && i != correct) {
                color = "#e74c3c"; // red for wrong selected answer
                prefix = "❌ ";
            } else if (i == user_ans) {
                // Correct and selected - already handled as correct
            }

            char opt_line[512];
            snprintf(opt_line, sizeof(opt_line),
                     "<span foreground='%s'>%s%c. %s</span>",
                     color,
                     prefix,
                     'A' + i,
                     opt_text);

            GtkWidget *opt_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(opt_label), opt_line);
            gtk_label_set_xalign(GTK_LABEL(opt_label), 0);
            gtk_box_pack_start(GTK_BOX(q_box), opt_label, FALSE, FALSE, 0);
        }

        gtk_container_add(GTK_CONTAINER(q_frame), q_box);
        gtk_box_pack_start(GTK_BOX(q_list_box), q_frame, FALSE, FALSE, 0);
        q_index++;
    }

    if (q_index == 0) {
        GtkWidget *empty_label = gtk_label_new("No questions found in results.");
        gtk_box_pack_start(GTK_BOX(q_list_box), empty_label, FALSE, FALSE, 0);
    }

    // Bottom buttons
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("BACK");
    style_button(back_btn, "#95a5a6");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(show_practice_results_screen), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), back_btn, TRUE, TRUE, 0);

    GtkWidget *list_btn = gtk_button_new_with_label("BACK TO LIST");
    style_button(list_btn, "#bdc3c7");
    g_signal_connect(list_btn, "clicked", G_CALLBACK(show_practice_list_screen), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), list_btn, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 10);

    show_view(vbox);
}

// ===== Internal helpers =====

static void on_practice_change_question(int new_index) {
    if (new_index < 0 || new_index >= current_practice.num_questions) {
        return;
    }
    current_practice.current_question = new_index;
    show_practice_room_screen();
}

static void on_practice_nav_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    int index = GPOINTER_TO_INT(data);
    on_practice_change_question(index);
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
