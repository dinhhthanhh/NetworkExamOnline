#include "exam_ui.h"
#include "net.h"
#include "room_ui.h"
#include "broadcast.h"
#include <gtk/gtk.h>
#include <time.h>

extern GtkWidget *main_window;
extern int sock_fd;
extern int current_user_id;

// Global state
static int exam_room_id = 0;
static int exam_duration = 0;
static time_t exam_start_time = 0;
static guint timer_id = 0;
static GtkWidget *timer_label = NULL;
static GtkWidget **question_radios = NULL; // Array of radio button groups
static int total_questions = 0;

// Waiting screen state
static GtkWidget *waiting_dialog = NULL;
static int waiting_room_id = 0;

typedef struct {
    int question_id;
    char text[512];
    char options[4][128];
    char difficulty[20];  // Easy, Medium, Hard
} Question;

static Question *questions = NULL;

// Forward declarations
static void start_exam_ui(int room_id);
static void start_exam_ui_from_response(int room_id, char *buffer);

// Callback when ROOM_STARTED broadcast received
static void on_room_started_broadcast(int room_id, long start_time) {
    printf("[EXAM_UI] Received ROOM_STARTED for room %d\n", room_id);
    
    // Check if we're waiting for this room
    if (waiting_room_id == room_id && waiting_dialog != NULL) {
        printf("[EXAM_UI] Closing waiting dialog and starting exam\n");
        
        // Close waiting dialog
        gtk_dialog_response(GTK_DIALOG(waiting_dialog), GTK_RESPONSE_ACCEPT);
        
        // Stop listening to broadcasts
        broadcast_stop_listener();
        
        // Start the actual exam
        start_exam_ui(room_id);
    }
}

// Timer callback - đếm ngược
static gboolean update_timer(gpointer data) {
    if (!exam_start_time) return FALSE;
    
    time_t now = time(NULL);
    long elapsed = now - exam_start_time;
    long remaining = (exam_duration * 60) - elapsed;
    
    if (remaining <= 0) {
        // Hết giờ - auto submit
        gtk_label_set_markup(GTK_LABEL(timer_label), 
                            "<span foreground='red' size='16000' weight='bold'>⏰ TIME'S UP!</span>");
        
        // Stop timer trước
        if (timer_id > 0) {
            g_source_remove(timer_id);
            timer_id = 0;
        }
        
        // Lưu room_id trước khi cleanup
        int submit_room_id = exam_room_id;
        
        printf("[DEBUG] Timeout - Auto-submitting exam for room: %d\n", submit_room_id);
        
        // Gửi SUBMIT_TEST
        char msg[64];
        snprintf(msg, sizeof(msg), "SUBMIT_TEST|%d\n", submit_room_id);
        send_message(msg);
        
        char buffer[BUFFER_SIZE];
        ssize_t n = receive_message(buffer, sizeof(buffer));
        
        if (n > 0 && strncmp(buffer, "SUBMIT_TEST_OK", 14) == 0) {
            // Parse: SUBMIT_TEST_OK|score|total|time_minutes
            strtok(buffer, "|");
            int score = atoi(strtok(NULL, "|"));
            int total = atoi(strtok(NULL, "|"));
            int time_taken = atoi(strtok(NULL, "|"));
            
            // Cleanup
            cleanup_exam_ui();
            
            // Show result
            GtkWidget *result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                             GTK_DIALOG_MODAL,
                                                             GTK_MESSAGE_INFO,
                                                             GTK_BUTTONS_OK,
                                                             "⏰ Time's Up! Exam Auto-Submitted!");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                     "Score: %d/%d (%.1f%%)\nTime: %d minutes",
                                                     score, total, (score * 100.0) / total, time_taken);
            gtk_dialog_run(GTK_DIALOG(result_dialog));
            gtk_widget_destroy(result_dialog);
            
            // Redirect về room list
            create_test_mode_screen();
        } else {
            // Error case
            cleanup_exam_ui();
            
            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                             GTK_DIALOG_MODAL,
                                                             GTK_MESSAGE_ERROR,
                                                             GTK_BUTTONS_OK,
                                                             "Auto-submit failed: %s",
                                                             buffer);
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
            
            // Redirect về room list
            create_test_mode_screen();
        }
        
        return FALSE; // Stop timer
    }
    
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    
    char timer_text[128];
    const char *color = (remaining < 300) ? "red" : "#2c3e50"; // Red nếu < 5 phút
    snprintf(timer_text, sizeof(timer_text),
             "<span foreground='%s' size='16000' weight='bold'>Time: %02d:%02d</span>",
             color, minutes, seconds);
    
    gtk_label_set_markup(GTK_LABEL(timer_label), timer_text);
    
    return TRUE; // Continue timer
}

// Callback khi user chọn đáp án - auto save
void on_answer_selected(GtkWidget *widget, gpointer data) {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        return; // Chỉ xử lý khi radio được chọn (không phải bỏ chọn)
    }
    
    int question_index = GPOINTER_TO_INT(data);
    int question_id = questions[question_index].question_id;
    
    // Tìm đáp án được chọn (0=A, 1=B, 2=C, 3=D)
    int selected = -1;
    for (int i = 0; i < 4; i++) {
        GtkWidget *radio = question_radios[question_index * 4 + i];
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio))) {
            selected = i;
            break;
        }
    }
    
    if (selected == -1) return;
    
    // Gửi SAVE_ANSWER đến server
    char msg[128];
    snprintf(msg, sizeof(msg), "SAVE_ANSWER|%d|%d|%d\n", 
             exam_room_id, question_id, selected);
    send_message(msg);
    
    // Read response to avoid buffer conflicts when submitting
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0 && strncmp(buffer, "SAVE_ANSWER_OK", 14) == 0) {
        printf("[DEBUG] Saved answer Q%d = %d (confirmed)\n", question_id, selected);
    } else {
        printf("[DEBUG] Save answer warning: %s\n", buffer);
    }
}

// Callback submit bài thi
void on_submit_exam_clicked(GtkWidget *widget, gpointer data) {
    // Stop timer
    if (timer_id > 0) {
        g_source_remove(timer_id);
        timer_id = 0;
    }
    
    // Lưu room_id trước khi cleanup (vì cleanup sẽ reset về 0)
    int submit_room_id = exam_room_id;
    
    // Đếm số câu đã làm và chưa làm
    int answered = 0;
    int unanswered = 0;
    for (int i = 0; i < total_questions; i++) {
        int has_answer = 0;
        for (int j = 0; j < 4; j++) {
            GtkWidget *radio = question_radios[i * 4 + j];
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio))) {
                has_answer = 1;
                break;
            }
        }
        if (has_answer) {
            answered++;
        } else {
            unanswered++;
        }
    }
    
    // Confirm dialog (trừ khi auto-submit do hết giờ)
    if (widget != NULL) {
        // Hiển thị cảnh báo số câu chưa làm
        char message[256];
        if (unanswered > 0) {
            snprintf(message, sizeof(message),
                     "You have answered %d/%d questions, %d remaining.\n\n"
                     "Are you sure you want to submit?",
                     answered, total_questions, unanswered);
        } else {
            snprintf(message, sizeof(message),
                     "You have completed all %d questions.\n\n"
                     "Are you sure you want to submit?",
                     total_questions);
        }
        
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL,
                                                   unanswered > 0 ? GTK_MESSAGE_WARNING : GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_YES_NO,
                                                   "%s", message);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 "You cannot change answers after submission.");
        
        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        if (response != GTK_RESPONSE_YES) {
            // Resume timer
            timer_id = g_timeout_add(1000, update_timer, NULL);
            return;
        }
    }
    
    printf("[DEBUG] Submitting exam for room: %d (answered: %d/%d)\n", 
           submit_room_id, answered, total_questions);
    
    // Gửi SUBMIT_TEST
    char msg[64];
    snprintf(msg, sizeof(msg), "SUBMIT_TEST|%d\n", submit_room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    if (n > 0 && strncmp(buffer, "SUBMIT_TEST_OK", 14) == 0) {
        // Parse: SUBMIT_TEST_OK|score|total|time_minutes
        char *token = strtok(buffer, "|"); // Skip "SUBMIT_TEST_OK"
        token = strtok(NULL, "|");
        int score = token ? atoi(token) : 0;
        token = strtok(NULL, "|");
        int total = token ? atoi(token) : 0;
        token = strtok(NULL, "|\n");  // Remove newline
        int time_taken = token ? atoi(token) : 0;
        
        // Cleanup trước khi show dialog
        cleanup_exam_ui();
        
        GtkWidget *result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_INFO,
                                                         GTK_BUTTONS_OK,
                                                         "Exam Submitted!");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                 "Score: %d/%d (%.1f%%)\nTime: %d minutes",
                                                 score, total, (score * 100.0) / total, time_taken);
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
        
        // Redirect về room list
        create_test_mode_screen();
        
    } else {
        // Cleanup ngay cả khi fail
        cleanup_exam_ui();
        
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "Submit failed: %s",
                                                         buffer);
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        
        // Redirect về room list
        create_test_mode_screen();
    }
}

// Tạo UI exam page
void create_exam_page(int room_id) {
    exam_room_id = room_id;
    
    // Gửi BEGIN_EXAM để load questions
    char msg[64];
    snprintf(msg, sizeof(msg), "BEGIN_EXAM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    printf("[DEBUG] BEGIN_EXAM response (%zd bytes): %s\n", n, buffer);
    
    // ===== XỬ LÝ EXAM_WAITING (LOGIC MỚI) =====
    if (n > 0 && strncmp(buffer, "EXAM_WAITING", 12) == 0) {
        printf("[EXAM_UI] Entering waiting mode for room %d\n", room_id);
        
        waiting_room_id = room_id;
        
        // Register callback for ROOM_STARTED broadcast
        broadcast_on_room_started(on_room_started_broadcast);
        
        // Start listening for broadcasts
        broadcast_start_listener();
        
        // Show waiting dialog
        waiting_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_CANCEL,
                                               "⏳ Waiting for Host to Start");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(waiting_dialog),
                                                 "You have joined the exam room.\nPlease wait for the host to start the exam.\n\nListening for broadcast...");
        
        int response = gtk_dialog_run(GTK_DIALOG(waiting_dialog));
        gtk_widget_destroy(waiting_dialog);
        waiting_dialog = NULL;
        
        if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT) {
            // User cancelled - stop listening
            broadcast_stop_listener();
            waiting_room_id = 0;
            printf("[EXAM_UI] User cancelled waiting\n");
        }
        
        return;
    }
    
    if (n <= 0 || strncmp(buffer, "BEGIN_EXAM_OK", 13) != 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "Cannot start exam");
        
        if (strncmp(buffer, "ERROR", 5) == 0) {
            char *msg = strchr(buffer, '|');
            if (msg) {
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                                                         "%s", msg + 1);
            }
        }
        
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }
    
    // Exam already started - proceed directly
    start_exam_ui_from_response(room_id, buffer);
}

// Start exam UI after receiving BEGIN_EXAM_OK
static void start_exam_ui(int room_id) {
    exam_room_id = room_id;
    
    // Send BEGIN_EXAM again to get questions
    char msg[64];
    snprintf(msg, sizeof(msg), "BEGIN_EXAM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    printf("[DEBUG] BEGIN_EXAM response after broadcast (%zd bytes): %s\n", n, buffer);
    
    if (n <= 0 || strncmp(buffer, "BEGIN_EXAM_OK", 13) != 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "Cannot start exam");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }
    
    start_exam_ui_from_response(room_id, buffer);
}

// Common function to start exam UI from BEGIN_EXAM_OK response
static void start_exam_ui_from_response(int room_id, char *buffer) {
    
    // Lưu copy của buffer NGAY từ đầu
    char *original_buffer = strdup(buffer);
    
    // Parse response: BEGIN_EXAM_OK|remaining_seconds|q1_id:text:A:B:C:D|q2_id:...
    // ===== ĐỔI TỪ DURATION (PHÚT) SANG REMAINING (GIÂY) =====
    
    // Đếm số câu hỏi từ original buffer TRƯỚC KHI parse
    total_questions = 0;
    char *count_ptr = original_buffer;
    char *pipe_pos = count_ptr;
    // Skip first two pipes: "BEGIN_EXAM_OK|remaining_seconds|"
    int pipe_count = 0;
    while ((pipe_pos = strchr(pipe_pos, '|')) != NULL) {
        pipe_count++;
        pipe_pos++;
        if (pipe_count > 1) {
            total_questions++;
        }
    }
    
    printf("[DEBUG] Counted %d questions from buffer\n", total_questions);
    
    // Parse remaining_seconds
    char *ptr = buffer;
    strtok(ptr, "|"); // Skip "BEGIN_EXAM_OK"
    int remaining_seconds = atoi(strtok(NULL, "|"));
    exam_duration = (remaining_seconds + 59) / 60; // Convert về phút (làm tròn lên)
    exam_start_time = time(NULL) - (exam_duration * 60 - remaining_seconds); // Điều chỉnh start_time
    
    printf("[DEBUG] Remaining: %d seconds (~ %d minutes)\n", remaining_seconds, exam_duration);
    
    if (total_questions == 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "No questions in this room");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }
    
    // Allocate memory
    questions = malloc(sizeof(Question) * total_questions);
    question_radios = malloc(sizeof(GtkWidget*) * total_questions * 4);
    
    // Parse lại từ original buffer để lấy questions
    ptr = original_buffer;
    strtok(ptr, "|"); // Skip "BEGIN_EXAM_OK"
    strtok(NULL, "|"); // Skip duration
    
    int q_idx = 0;
    char *q_token;
    while ((q_token = strtok(NULL, "|")) != NULL && q_idx < total_questions) {
        // Parse: q_id:text:optA:optB:optC:optD:difficulty
        char *q_ptr = q_token;
        
        // Parse question_id
        char *id_str = strsep(&q_ptr, ":");
        if (!id_str || !q_ptr) continue;
        questions[q_idx].question_id = atoi(id_str);
        
        // Parse question text
        char *text = strsep(&q_ptr, ":");
        if (!text || !q_ptr) continue;
        strncpy(questions[q_idx].text, text, sizeof(questions[q_idx].text) - 1);
        
        // Parse 4 options
        for (int i = 0; i < 4; i++) {
            char *opt = strsep(&q_ptr, ":");
            if (!opt) {
                strcpy(questions[q_idx].options[i], "???");
            } else {
                strncpy(questions[q_idx].options[i], opt, sizeof(questions[q_idx].options[i]) - 1);
            }
        }
        
        // Parse difficulty (optional - default to "Easy" if missing)
        char *diff = strsep(&q_ptr, ":");
        if (diff && strlen(diff) > 0) {
            strncpy(questions[q_idx].difficulty, diff, sizeof(questions[q_idx].difficulty) - 1);
        } else {
            strcpy(questions[q_idx].difficulty, "Easy");
        }
        
        printf("[DEBUG] Q%d [%s]: %s\n", questions[q_idx].question_id, 
               questions[q_idx].difficulty, questions[q_idx].text);
        q_idx++;
    }
    
    total_questions = q_idx; // Update với số câu thực tế parse được
    free(original_buffer);
    
    printf("[DEBUG] Successfully parsed %d questions\n", total_questions);
    
    if (total_questions == 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "No questions found");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }
    
    printf("[DEBUG] Creating UI with %d questions...\n", total_questions);
    
    // Tạo UI
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    
    // Header với timer
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
                        "<span size='20000' weight='bold' foreground='#2c3e50'>📝 EXAM</span>");
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    
    timer_label = gtk_label_new(NULL);
    gtk_box_pack_end(GTK_BOX(header_box), timer_label, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    
    // Scrolled window cho questions
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);
    
    GtkWidget *questions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(questions_box, 10);
    gtk_widget_set_margin_bottom(questions_box, 10);
    gtk_widget_set_margin_start(questions_box, 10);
    gtk_widget_set_margin_end(questions_box, 10);
    
    printf("[DEBUG] Creating %d question widgets...\n", total_questions);
    
    // Hiển thị từng câu hỏi
    for (int i = 0; i < total_questions; i++) {
        GtkWidget *q_frame = gtk_frame_new(NULL);
        GtkWidget *q_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_set_margin_top(q_vbox, 10);
        gtk_widget_set_margin_bottom(q_vbox, 10);
        gtk_widget_set_margin_start(q_vbox, 15);
        gtk_widget_set_margin_end(q_vbox, 15);
        
        // Question header với difficulty badge
        GtkWidget *q_header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        
        char q_text[600];
        snprintf(q_text, sizeof(q_text), 
                "<b>%d. %s</b>", i + 1, questions[i].text);
        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_text);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_box_pack_start(GTK_BOX(q_header_box), q_label, TRUE, TRUE, 0);
        
        // Difficulty badge
        const char *badge_color;
        if (strcmp(questions[i].difficulty, "Hard") == 0) {
            badge_color = "#e74c3c";  // Red
        } else if (strcmp(questions[i].difficulty, "Medium") == 0) {
            badge_color = "#f39c12";  // Orange
        } else {
            badge_color = "#27ae60";  // Green
        }
        
        char badge_markup[128];
        snprintf(badge_markup, sizeof(badge_markup),
                "<span foreground='%s' weight='bold'>[%s]</span>",
                badge_color, questions[i].difficulty);
        GtkWidget *badge_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(badge_label), badge_markup);
        gtk_box_pack_end(GTK_BOX(q_header_box), badge_label, FALSE, FALSE, 0);
        
        gtk_box_pack_start(GTK_BOX(q_vbox), q_header_box, FALSE, FALSE, 0);
        
        // Radio buttons cho options
        // Tạo invisible radio button để không có đáp án nào được chọn mặc định
        GSList *group = NULL;
        GtkWidget *invisible_radio = gtk_radio_button_new(group);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(invisible_radio));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(invisible_radio), TRUE);  // Chọn invisible làm default
        // Không thêm vào UI
        
        const char *labels[] = {"A", "B", "C", "D"};
        
        for (int j = 0; j < 4; j++) {
            char option_text[150];
            snprintf(option_text, sizeof(option_text), "%s. %s", 
                    labels[j], questions[i].options[j]);
            
            GtkWidget *radio = gtk_radio_button_new_with_label(group, option_text);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
            
            g_signal_connect(radio, "toggled", 
                           G_CALLBACK(on_answer_selected), 
                           GINT_TO_POINTER(i));
            
            gtk_box_pack_start(GTK_BOX(q_vbox), radio, FALSE, FALSE, 0);
            question_radios[i * 4 + j] = radio;
        }
        
        gtk_container_add(GTK_CONTAINER(q_frame), q_vbox);
        gtk_box_pack_start(GTK_BOX(questions_box), q_frame, FALSE, FALSE, 0);
        
        printf("[DEBUG] Added question %d: %s\n", i+1, questions[i].text);
    }
    
    gtk_container_add(GTK_CONTAINER(scroll), questions_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    printf("[DEBUG] All questions added to UI\n");
    
    // Submit button
    GtkWidget *submit_btn = gtk_button_new_with_label("📤 SUBMIT EXAM");
    gtk_widget_set_size_request(submit_btn, 200, 50);
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_exam_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), submit_btn, FALSE, FALSE, 0);
    
    // Show UI - Remove old widget properly
    GtkWidget *old_child = gtk_bin_get_child(GTK_BIN(main_window));
    if (old_child) {
        gtk_container_remove(GTK_CONTAINER(main_window), old_child);
    }
    gtk_container_add(GTK_CONTAINER(main_window), vbox);
    gtk_widget_show_all(main_window);
    
    // Start timer
    timer_id = g_timeout_add(1000, update_timer, NULL);
}

void cleanup_exam_ui() {
    if (timer_id > 0) {
        g_source_remove(timer_id);
        timer_id = 0;
    }
    
    // Remove exam widget from window properly
    GtkWidget *old_child = gtk_bin_get_child(GTK_BIN(main_window));
    if (old_child) {
        gtk_container_remove(GTK_CONTAINER(main_window), old_child);
    }
    
    if (questions) {
        free(questions);
        questions = NULL;
    }
    
    if (question_radios) {
        free(question_radios);
        question_radios = NULL;
    }
    
    exam_room_id = 0;
    exam_duration = 0;
    exam_start_time = 0;
    total_questions = 0;
}

// Resume exam từ session cũ
void create_exam_page_from_resume(int room_id, char *resume_data) {
    exam_room_id = room_id;
    
    printf("[DEBUG] Resume data: %s\n", resume_data);
    
    // Parse: RESUME_EXAM_OK|remaining_seconds|q1_id:text:A:B:C:D:saved_answer|...
    // Sử dụng strstr thay vì strtok để tránh modify string gốc
    
    // Tìm remaining_seconds
    const char *ptr = strstr(resume_data, "RESUME_EXAM_OK|");
    if (!ptr) {
        printf("[ERROR] Invalid resume data format\n");
        return;
    }
    
    ptr += 15; // Skip "RESUME_EXAM_OK|"
    long remaining_seconds = atol(ptr);
    
    // Tìm vị trí bắt đầu questions (sau remaining_seconds)
    ptr = strchr(ptr, '|');
    if (!ptr) {
        printf("[ERROR] No questions data\n");
        return;
    }
    ptr++; // Skip '|'
    
    printf("[DEBUG] Remaining: %ld seconds\n", remaining_seconds);
    
    // Set timer
    exam_start_time = time(NULL);
    exam_duration = (remaining_seconds / 60) + 1;
    
    // Đếm số câu hỏi bằng cách đếm dấu '|'
    const char *count_ptr = ptr;
    total_questions = 0;
    while ((count_ptr = strchr(count_ptr, '|')) != NULL) {
        total_questions++;
        count_ptr++;
    }
    
    printf("[DEBUG] Total questions: %d\n", total_questions);
    
    if (total_questions == 0) {
        printf("[ERROR] No questions found\n");
        return;
    }
    
    // Allocate memory
    questions = malloc(sizeof(Question) * total_questions);
    question_radios = malloc(sizeof(GtkWidget*) * total_questions * 4);
    int saved_answers[total_questions];
    
    // Parse từng câu hỏi
    int q_idx = 0;
    const char *q_start = ptr;
    
    while (q_idx < total_questions && *q_start) {
        // Tìm end của question này (đến '|' tiếp theo hoặc '\n')
        const char *q_end = strchr(q_start, '|');
        if (!q_end) q_end = strchr(q_start, '\n');
        if (!q_end) q_end = q_start + strlen(q_start);
        
        // Copy question data
        size_t q_len = q_end - q_start;
        char *q_data = malloc(q_len + 1);
        memcpy(q_data, q_start, q_len);
        q_data[q_len] = '\0';
        
        printf("[DEBUG] Q%d data: %s\n", q_idx + 1, q_data);
        
        // Parse: id:text:optA:optB:optC:optD:saved_answer
        char *parse_ptr = q_data;
        char *fields[8]; // id, text, A, B, C, D, saved
        int field_idx = 0;
        
        char *field_start = parse_ptr;
        while (*parse_ptr && field_idx < 8) {
            if (*parse_ptr == ':') {
                *parse_ptr = '\0';
                fields[field_idx++] = field_start;
                field_start = parse_ptr + 1;
            }
            parse_ptr++;
        }
        if (field_start < parse_ptr) {
            fields[field_idx++] = field_start;
        }
        
        if (field_idx >= 7) {
            // Parse thành công
            questions[q_idx].question_id = atoi(fields[0]);
            strncpy(questions[q_idx].text, fields[1], sizeof(questions[q_idx].text) - 1);
            questions[q_idx].text[sizeof(questions[q_idx].text) - 1] = '\0';
            
            for (int i = 0; i < 4; i++) {
                strncpy(questions[q_idx].options[i], fields[2 + i], 
                       sizeof(questions[q_idx].options[i]) - 1);
                questions[q_idx].options[i][sizeof(questions[q_idx].options[i]) - 1] = '\0';
            }
            
            saved_answers[q_idx] = atoi(fields[6]);
            
            printf("[DEBUG] Parsed Q%d: id=%d, saved=%d\n", 
                   q_idx + 1, questions[q_idx].question_id, saved_answers[q_idx]);
        } else {
            printf("[ERROR] Failed to parse Q%d - only %d fields\n", q_idx + 1, field_idx);
            saved_answers[q_idx] = -1;
        }
        
        free(q_data);
        q_idx++;
        q_start = q_end;
        if (*q_start == '|') q_start++;
    }
    
    // Tạo UI giống như create_exam_page
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    // Header với timer
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
                        "<span size='20000' weight='bold' foreground='#2c3e50'>📝 EXAM (RESUMED)</span>");
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    
    timer_label = gtk_label_new(NULL);
    gtk_box_pack_end(GTK_BOX(header_box), timer_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    
    // Scroll window cho câu hỏi
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);
    
    GtkWidget *questions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(questions_box, 10);
    gtk_widget_set_margin_bottom(questions_box, 10);
    gtk_widget_set_margin_start(questions_box, 10);
    gtk_widget_set_margin_end(questions_box, 10);
    
    // Tạo UI cho từng câu hỏi
    for (int i = 0; i < total_questions; i++) {
        GtkWidget *q_frame = gtk_frame_new(NULL);
        GtkWidget *q_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_margin_start(q_box, 10);
        gtk_widget_set_margin_end(q_box, 10);
        gtk_widget_set_margin_top(q_box, 10);
        gtk_widget_set_margin_bottom(q_box, 10);
        
        // Question text
        char q_text[600];
        snprintf(q_text, sizeof(q_text), "<b>Question %d:</b> %s", i + 1, questions[i].text);
        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_text);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0.0);
        gtk_box_pack_start(GTK_BOX(q_box), q_label, FALSE, FALSE, 0);
        
        // Radio buttons cho options
        GSList *group = NULL;
        const char *labels[] = {"A", "B", "C", "D"};
        
        for (int j = 0; j < 4; j++) {
            char opt_text[150];
            snprintf(opt_text, sizeof(opt_text), "%s. %s", labels[j], questions[i].options[j]);
            
            GtkWidget *radio = gtk_radio_button_new_with_label(group, opt_text);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
            
            // Restore saved answer
            if (saved_answers[i] == j) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);
            }
            
            g_signal_connect(radio, "toggled", G_CALLBACK(on_answer_selected), GINT_TO_POINTER(i));
            
            question_radios[i * 4 + j] = radio;
            gtk_box_pack_start(GTK_BOX(q_box), radio, FALSE, FALSE, 0);
        }
        
        gtk_container_add(GTK_CONTAINER(q_frame), q_box);
        gtk_box_pack_start(GTK_BOX(questions_box), q_frame, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(scroll), questions_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    // Submit button
    GtkWidget *submit_btn = gtk_button_new_with_label("SUBMIT EXAM");
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_exam_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), submit_btn, FALSE, FALSE, 0);
    
    // Show UI - Remove old widget properly
    GtkWidget *old_child = gtk_bin_get_child(GTK_BIN(main_window));
    if (old_child) {
        gtk_container_remove(GTK_CONTAINER(main_window), old_child);
    }
    gtk_container_add(GTK_CONTAINER(main_window), vbox);
    gtk_widget_show_all(main_window);
    
    // Start timer với remaining time
    // Điều chỉnh exam_start_time để timer đếm ngược đúng
    exam_start_time = time(NULL) - ((exam_duration * 60) - remaining_seconds);
    timer_id = g_timeout_add(1000, update_timer, NULL);
    
    printf("[INFO] Resume exam completed - timer started\n");
}
