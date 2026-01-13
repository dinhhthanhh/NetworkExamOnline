#include "exam_ui.h"
#include "net.h"
#include "room_ui.h"
#include "broadcast.h"
#include "ui_utils.h"
#include <gtk/gtk.h>
#include <time.h>
#include <fcntl.h>

extern GtkWidget *main_window;
extern int sock_fd;
extern int current_user_id;

// Global state
static int exam_room_id = 0;
static int exam_duration = 0;
static time_t exam_start_time = 0;
static guint timer_id = 0;
static GtkWidget *exam_timer_label = NULL;
static GtkWidget **question_frames = NULL; // Array of question frames for highlighting
static int *answered_questions = NULL; // Track which questions are answered (1=answered, 0=not)
static int *selected_answers = NULL;   // Track selected option per question (-1 if none)
static int total_questions = 0;

// Waiting screen state
static GtkWidget *waiting_dialog = NULL;
static int waiting_room_id = 0;

typedef struct {
    int question_id;
    char text[512];
    char options[4][128];
    char difficulty[20];
} Question;

static Question *questions = NULL;
static int current_question_index = 0;

// Forward declarations
static void start_exam_ui(int room_id);
static void start_exam_ui_from_response(int room_id, char *buffer);
static gboolean show_room_deleted_notification_idle(gpointer user_data);
static void on_exam_nav_clicked(GtkWidget *widget, gpointer data);
static void on_exam_change_question(int new_index);
void show_exam_question_screen(void);

// Callback when ROOM_STARTED broadcast received
static void on_room_started_broadcast(int room_id, long start_time) {
    
    // Check if we're waiting for this room
    if (waiting_room_id == room_id && waiting_dialog != NULL) {
        
        // Close waiting dialog
        gtk_dialog_response(GTK_DIALOG(waiting_dialog), GTK_RESPONSE_ACCEPT);
        
        // Stop listening to broadcasts
        broadcast_stop_listener();
        
        // Start the actual exam
        start_exam_ui(room_id);
    }
}

// Callback when ROOM_DELETED broadcast received
static void on_room_deleted_broadcast(int room_id) {
    
    // Check if we're currently in this room
    if (exam_room_id == room_id) {
        // Stop timer
        if (timer_id > 0) {
            g_source_remove(timer_id);
            timer_id = 0;
        }
        
        // Stop broadcast listener
        broadcast_stop_listener();
        
        // Show notification using g_idle_add to run in main thread
        g_idle_add((GSourceFunc)show_room_deleted_notification_idle, GINT_TO_POINTER(room_id));
    }
    
    // Check if we're waiting for this room
    if (waiting_room_id == room_id && waiting_dialog != NULL) {
        gtk_dialog_response(GTK_DIALOG(waiting_dialog), GTK_RESPONSE_CANCEL);
        broadcast_stop_listener();
        waiting_room_id = 0;
        
        g_idle_add((GSourceFunc)show_room_deleted_notification_idle, GINT_TO_POINTER(room_id));
    }
}

// Helper function to show room deleted notification in main thread
static gboolean show_room_deleted_notification_idle(gpointer user_data) {
    int room_id = GPOINTER_TO_INT(user_data);
    
    // ===== THÊM MỚI: Stop timer first =====
    if (timer_id > 0) {
        g_source_remove(timer_id);
        timer_id = 0;
    }
    
    // Stop broadcast listener
    if (broadcast_is_listening()) {
        broadcast_stop_listener();
    }
    
    // Cleanup exam UI
    cleanup_exam_ui();
    
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_WARNING,
                                               GTK_BUTTONS_OK,
                                               "Room Deleted");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "Room #%d has been deleted by the administrator.\nYou will be returned to the main menu.",
                                             room_id);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    // ===== THÊM MỚI: Ensure blocking =====
    if (client.socket_fd > 0) {
        int flags = fcntl(client.socket_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
        }
    }
    
    // Return to main menu
    create_test_mode_screen();
    
    return FALSE; // Remove this idle callback
}

// Timer callback - đếm ngược
static gboolean update_exam_timer(gpointer data) {
    if (!exam_start_time) return FALSE;
    
    time_t now = time(NULL);
    long elapsed = now - exam_start_time;
    long remaining = (exam_duration * 60) - elapsed;
    
    if (remaining <= 0) {
        // Hết giờ - auto submit
        if (exam_timer_label && GTK_IS_WIDGET(exam_timer_label)) {
            gtk_label_set_markup(GTK_LABEL(exam_timer_label),
                "<span foreground='red' size='16000' weight='bold'>TIME'S UP!</span>");
        }
        
        // Stop timer trước
        if (timer_id > 0) {
            g_source_remove(timer_id);
            timer_id = 0;
        }
        
        // CRITICAL: Stop broadcast listener before submitting
        if (broadcast_is_listening()) {
            broadcast_stop_listener();
        }

        // If connection is already lost, abort auto-submit gracefully
        if (client.socket_fd <= 0 || !check_connection()) {
            cleanup_exam_ui();

            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                             GTK_DIALOG_MODAL,
                                                             GTK_MESSAGE_ERROR,
                                                             GTK_BUTTONS_OK,
                                                             "Time's Up (Offline)");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                                                     "Exam time is over but the connection to the server was lost.\nPlease reconnect to see your final result.");
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);

            create_test_mode_screen();
            return FALSE;
        }

        // CRITICAL: Ensure blocking mode
        if (client.socket_fd > 0) {
            int flags = fcntl(client.socket_fd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
            }
        }

        // Lưu room_id trước khi cleanup
        int submit_room_id = exam_room_id;


        // CRITICAL: Flush socket buffer before submit to clear stale responses
        flush_socket_buffer(client.socket_fd);

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
                                                             "Time's Up! Exam Auto-Submitted!");
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
             "<span foreground='%s' size='16000' weight='bold'>%02d:%02d</span>",
             color, minutes, seconds);
    
    if (exam_timer_label && GTK_IS_WIDGET(exam_timer_label)) {
        gtk_label_set_markup(GTK_LABEL(exam_timer_label), timer_text);
    }
    
    return TRUE; // Continue timer
}

// Callback khi user chọn đáp án - auto save
void on_answer_selected(GtkWidget *widget, gpointer data) {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        return; // Chỉ xử lý khi radio được chọn (không phải bỏ chọn)
    }
    
    int question_index = GPOINTER_TO_INT(data);
    if (question_index < 0 || question_index >= total_questions) {
        return;
    }

    int question_id = questions[question_index].question_id;

    // Lấy đáp án được chọn (0=A, 1=B, 2=C, 3=D) từ widget
    int selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "answer_index"));
    if (selected < 0 || selected > 3) {
        return;
    }
    
    // Mark question as answered
    if (answered_questions) {
        answered_questions[question_index] = 1;
    }

    if (selected_answers && question_index >= 0 && question_index < total_questions) {
        selected_answers[question_index] = selected;
    }
    
    // Gửi SAVE_ANSWER đến server
    char msg[128];
    snprintf(msg, sizeof(msg), "SAVE_ANSWER|%d|%d|%d\n", 
             exam_room_id, question_id, selected);
    send_message(msg);
    
    // QUAN TRỌNG: Phải đọc response để tránh bị lẫn buffer khi submit
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0 && strncmp(buffer, "SAVE_ANSWER_OK", 14) == 0) {
    } else {
    }
    
    // Refresh screen to update navigation colors
    show_exam_question_screen();
}

// Callback submit bài thi
void on_submit_exam_clicked(GtkWidget *widget, gpointer data) {
    // Stop timer
    if (timer_id > 0) {
        g_source_remove(timer_id);
        timer_id = 0;
    }
    
    // CRITICAL: Stop broadcast listener before submitting to ensure blocking socket
    if (broadcast_is_listening()) {
        broadcast_stop_listener();
    }
    
    // Lưu room_id trước khi cleanup (vì cleanup sẽ reset về 0)
    int submit_room_id = exam_room_id;
    
    // Confirm dialog (trừ khi auto-submit do hết giờ)
    if (widget != NULL) {
        // Count answered/unanswered questions
        int answered_count = 0;
        int unanswered_count = 0;
        
        if (answered_questions) {
            for (int i = 0; i < total_questions; i++) {
                if (answered_questions[i]) {
                    answered_count++;
                } else {
                    unanswered_count++;
                }
            }
        }
        
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_YES_NO,
                                                   "Submit Exam?");
        
        char detail_msg[256];
        snprintf(detail_msg, sizeof(detail_msg),
                 "Answered: %d/%d questions\n"
                 "Unanswered: %d questions\n\n"
                 "You cannot change answers after submission.\n"
                 "Are you sure you want to submit?",
                 answered_count, total_questions, unanswered_count);
        
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", detail_msg);
        
        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        if (response != GTK_RESPONSE_YES) {
            // Resume exam timer
            timer_id = g_timeout_add(1000, update_exam_timer, NULL);
            return;
        }
    }
    
    
    // CRITICAL: Ensure socket is in blocking mode before submit
    if (client.socket_fd > 0) {
        int flags = fcntl(client.socket_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
        }
    }
    
    // CRITICAL: Flush socket buffer before submit to clear stale responses
    flush_socket_buffer(client.socket_fd);
    
    // Gửi SUBMIT_TEST
    char msg[64];
    snprintf(msg, sizeof(msg), "SUBMIT_TEST|%d\n", submit_room_id);
    send_message(msg);
    
    // Nhận response (KHÔNG flush sau send!)
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
        
        // CRITICAL: Ensure socket is in blocking mode before returning to room list
        // This prevents crashes when LIST_ROOMS is called
        if (client.socket_fd > 0) {
            int flags = fcntl(client.socket_fd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
            }
        }
        
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
        
        // CRITICAL: Ensure socket is in blocking mode before returning to room list
        if (client.socket_fd > 0) {
            int flags = fcntl(client.socket_fd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
            }
        }
        
        // Redirect về room list
        create_test_mode_screen();
    }
}

// Tạo UI exam page
void create_exam_page(int room_id) {
    exam_room_id = room_id;
    
    // Flush socket buffer before BEGIN_EXAM to clear stale data
    flush_socket_buffer(client.socket_fd);
    
    // Gửi BEGIN_EXAM để load questions
    char msg[64];
    snprintf(msg, sizeof(msg), "BEGIN_EXAM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    
    // ===== XỬ LÝ ERROR RESPONSES =====
    if (n > 0 && strncmp(buffer, "ERROR", 5) == 0) {
        char *error_msg = strchr(buffer, '|');
        const char *display_msg = error_msg ? error_msg + 1 : "Unknown error";
        
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "❌ Cannot join exam");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                                                 "%s", display_msg);
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        
        // Redirect về room list
        create_test_mode_screen();
        return;
    }
    
    // ===== XỬ LÝ EXAM_WAITING (LOGIC MỚI) =====
    if (n > 0 && strncmp(buffer, "EXAM_WAITING", 12) == 0) {
        
        waiting_room_id = room_id;
        
        // Register callbacks for broadcasts
        broadcast_on_room_started(on_room_started_broadcast);
        broadcast_on_room_deleted(on_room_deleted_broadcast);
        
        // Start listening for broadcasts for THIS SPECIFIC ROOM
        broadcast_start_listener_for_room(room_id);
        
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
<<<<<<< HEAD
            printf("[EXAM_UI] User cancelled waiting\n");
            
            // Redirect về room list
            create_test_mode_screen();
=======
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
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
    char *ptr = buffer;
    strtok(ptr, "|"); // Skip "BEGIN_EXAM_OK"
    int remaining_seconds = atoi(strtok(NULL, "|"));
    exam_duration = (remaining_seconds + 59) / 60; // Convert về phút (làm tròn lên)
    exam_start_time = time(NULL) - (exam_duration * 60 - remaining_seconds); // Điều chỉnh start_time
    
    
    // Đếm số câu hỏi
    total_questions = 0;
    while (strtok(NULL, "|\n") != NULL) {
        total_questions++;
    }
    
    
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
    question_frames = malloc(sizeof(GtkWidget*) * total_questions);
    answered_questions = calloc(total_questions, sizeof(int)); // Initialize to 0
    selected_answers = malloc(sizeof(int) * total_questions);
    if (selected_answers) {
        for (int i = 0; i < total_questions; i++) {
            selected_answers[i] = -1;
        }
    }
    
    // Parse lại từ original buffer để lấy questions
    ptr = original_buffer;
    strtok(ptr, "|"); // Skip "BEGIN_EXAM_OK"
    strtok(NULL, "|"); // Skip duration
    
    int q_idx = 0;
    char *q_token;
    while ((q_token = strtok(NULL, "|\n")) != NULL && q_idx < total_questions) {
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
        
        // Parse difficulty (optional, default to "Medium" if not provided)
        char *diff = strsep(&q_ptr, ":");
        if (diff && strlen(diff) > 0) {
            strncpy(questions[q_idx].difficulty, diff, sizeof(questions[q_idx].difficulty) - 1);
        } else {
            strcpy(questions[q_idx].difficulty, "Medium");
        }
        
        q_idx++;
    }
    
    total_questions = q_idx; // Update với số câu thực tế parse được
    free(original_buffer);
    
    
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
    
    // Register broadcast callback for ROOM_DELETED
    broadcast_on_room_deleted(on_room_deleted_broadcast);
    
    // Start listening for broadcasts during exam
    if (!broadcast_is_listening()) {
        broadcast_start_listener();
    }
    
    
    // Initialize current question index
    current_question_index = 0;
    
    // Show the exam screen
    show_exam_question_screen();
    
    // Start timer
    timer_id = g_timeout_add(1000, update_exam_timer, NULL);
}

// Show exam question screen with navigation
void show_exam_question_screen(void) {
    // Tạo UI
    GtkWidget *main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_widget_set_margin_top(main_hbox, 20);
    gtk_widget_set_margin_bottom(main_hbox, 20);
    gtk_widget_set_margin_start(main_hbox, 20);
    gtk_widget_set_margin_end(main_hbox, 20);
    
    // LEFT PANEL: Navigation (5 numbers per row)
    GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(left_panel, 180, -1);
    
    // Header with timer
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
                        "<span foreground='#2c3e50' size='14000' weight='bold'>📝 EXAM</span>");
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    
    exam_timer_label = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(header_box), exam_timer_label, FALSE, FALSE, 0);
    
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
    
    int rows = (total_questions + 4) / 5; // Round up
    for (int row = 0; row < rows; row++) {
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        
        for (int col = 0; col < 5; col++) {
            int idx = row * 5 + col;
            if (idx >= total_questions) break;
            
            char btn_text[16];
            snprintf(btn_text, sizeof(btn_text), "%d", idx + 1);
            
            GtkWidget *nav_btn = gtk_button_new_with_label(btn_text);
            gtk_widget_set_size_request(nav_btn, 45, 40);
            
            // Color based on answer status
            if (answered_questions[idx]) {
                style_button(nav_btn, "#3498db"); // Blue - answered
            } else {
                style_button(nav_btn, "#bdc3c7"); // Gray - not answered
            }
            
            g_signal_connect(nav_btn, "clicked", 
                G_CALLBACK(on_exam_nav_clicked), GINT_TO_POINTER(idx));
            
            gtk_box_pack_start(GTK_BOX(row_box), nav_btn, TRUE, TRUE, 0);
        }
        
        gtk_box_pack_start(GTK_BOX(nav_vbox), row_box, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(nav_scroll), nav_vbox);
    gtk_box_pack_start(GTK_BOX(left_panel), nav_scroll, TRUE, TRUE, 0);
    
    // Submit button
    GtkWidget *submit_btn = gtk_button_new_with_label("SUBMIT EXAM");
    gtk_widget_set_size_request(submit_btn, -1, 45);
    style_button(submit_btn, "#27ae60");
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_exam_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(left_panel), submit_btn, FALSE, FALSE, 0);
    
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
            current_question_index + 1, total_questions);
    GtkWidget *progress_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(progress_label), progress_text);
    gtk_box_pack_start(GTK_BOX(right_panel), progress_label, FALSE, FALSE, 0);
    
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(right_panel), sep2, FALSE, FALSE, 0);
    
    // Current question display
    Question *current_q = &questions[current_question_index];
    
    GtkWidget *q_frame = gtk_frame_new("Question");
    GtkWidget *q_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(q_box, 15);
    gtk_widget_set_margin_end(q_box, 15);
    gtk_widget_set_margin_top(q_box, 10);
    gtk_widget_set_margin_bottom(q_box, 10);
    
    // Question text with difficulty
    char q_text[700];
    // Use strcasecmp for case-insensitive comparison
    const char *diff_color = strcasecmp(current_q->difficulty, "Easy") == 0 ? "#27ae60" :
                            strcasecmp(current_q->difficulty, "Hard") == 0 ? "#e74c3c" : "#f39c12";
    snprintf(q_text, sizeof(q_text),
            "<span foreground='#2c3e50' weight='bold' size='12000'>Q%d. %s</span>\n"
            "<span foreground='%s' size='10000'><i>Difficulty: %s</i></span>",
            current_question_index + 1, current_q->text,
            diff_color, current_q->difficulty);
    
    GtkWidget *q_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(q_label), q_text);
    gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(q_label), 0);
    gtk_box_pack_start(GTK_BOX(q_box), q_label, FALSE, FALSE, 0);
    
    // Radio buttons cho options
    const char *labels[] = {"A", "B", "C", "D"};

    // Dummy radio ẩn để tạo group ban đầu, tránh auto-select
    GtkWidget *dummy_radio = gtk_radio_button_new(NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dummy_radio), TRUE);
    gtk_widget_set_no_show_all(dummy_radio, TRUE);
    gtk_widget_hide(dummy_radio);
    gtk_box_pack_start(GTK_BOX(q_box), dummy_radio, FALSE, FALSE, 0);

    GSList *group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(dummy_radio));

    int previously_selected = -1;
    if (selected_answers && current_question_index >= 0 && current_question_index < total_questions) {
        previously_selected = selected_answers[current_question_index];
    }

    for (int j = 0; j < 4; j++) {
        char option_text[200];
        snprintf(option_text, sizeof(option_text),
                "%s. %s", labels[j], current_q->options[j]);

        GtkWidget *radio = gtk_radio_button_new_with_label(group, option_text);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));

        // Khôi phục đáp án đã chọn (nếu có)
        if (previously_selected == j) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);
        }

        g_object_set_data(G_OBJECT(radio), "answer_index", GINT_TO_POINTER(j));
        g_signal_connect(radio, "toggled",
            G_CALLBACK(on_answer_selected),
            GINT_TO_POINTER(current_question_index));

        gtk_box_pack_start(GTK_BOX(q_box), radio, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(q_frame), q_box);
    gtk_box_pack_start(GTK_BOX(right_panel), q_frame, TRUE, TRUE, 0);
    
    // Navigation buttons (Previous/Next)
    GtkWidget *nav_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    if (current_question_index > 0) {
        GtkWidget *prev_btn = gtk_button_new_with_label("⬅️ Previous");
        style_button(prev_btn, "#95a5a6");
        g_signal_connect(prev_btn, "clicked",
            G_CALLBACK(on_exam_nav_clicked),
            GINT_TO_POINTER(current_question_index - 1));
        gtk_box_pack_start(GTK_BOX(nav_btn_box), prev_btn, TRUE, TRUE, 0);
    }
    
    if (current_question_index < total_questions - 1) {
        GtkWidget *next_btn = gtk_button_new_with_label("Next ➡️");
        style_button(next_btn, "#3498db");
        g_signal_connect(next_btn, "clicked",
            G_CALLBACK(on_exam_nav_clicked),
            GINT_TO_POINTER(current_question_index + 1));
        gtk_box_pack_start(GTK_BOX(nav_btn_box), next_btn, TRUE, TRUE, 0);
    }
    
    gtk_box_pack_start(GTK_BOX(right_panel), nav_btn_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_hbox), right_panel, TRUE, TRUE, 0);

    // Use shared view helper to manage main_window content safely
    show_view(main_hbox);
}

// Navigation callback
static void on_exam_nav_clicked(GtkWidget *widget, gpointer data) {
    int new_index = GPOINTER_TO_INT(data);
    on_exam_change_question(new_index);
}

// Change to a different question
static void on_exam_change_question(int new_index) {
    if (new_index < 0 || new_index >= total_questions) return;
    
    current_question_index = new_index;
    show_exam_question_screen();
}

void cleanup_exam_ui() {
    // CRITICAL: Stop broadcast listener first to prevent callbacks on destroyed widgets
    if (broadcast_is_listening()) {
        broadcast_stop_listener();
    }
    
    if (timer_id > 0) {
        g_source_remove(timer_id);
        timer_id = 0;
    }
    
        // The removal of the main_window child is now managed by higher-level screens.
    
    if (questions) {
        free(questions);
        questions = NULL;
    }

    if (question_frames) {
        free(question_frames);
        question_frames = NULL;
    }
    
    if (answered_questions) {
        free(answered_questions);
        answered_questions = NULL;
    }

    if (selected_answers) {
        free(selected_answers);
        selected_answers = NULL;
    }
    
    // ===== THÊM MỚI: Restore blocking mode =====
    if (client.socket_fd > 0) {
        int flags = fcntl(client.socket_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
        }
    }
    
    exam_room_id = 0;
    exam_duration = 0;
    exam_start_time = 0;
    total_questions = 0;
    current_question_index = 0;
}

// Resume exam từ session cũ
void create_exam_page_from_resume(int room_id, char *resume_data) {
    exam_room_id = room_id;
    
    
<<<<<<< HEAD
    // Parse: RESUME_EXAM_OK|remaining_seconds|duration_minutes|q1_id:text:A:B:C:D:saved_answer|...
    // THÊM duration_minutes để tính exam_start_time chính xác
=======
    // Parse: RESUME_EXAM_OK|remaining_seconds|q1_id:text:A:B:C:D[:difficulty]:saved_answer|...
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
    
    // Make a copy to work with
    char *data_copy = strdup(resume_data);
    
<<<<<<< HEAD
    ptr += 15; // Skip "RESUME_EXAM_OK|"
    long remaining_seconds = atol(ptr);
    
    // Parse duration_minutes (field mới)
    ptr = strchr(ptr, '|');
    if (!ptr) {
        printf("[ERROR] No duration field\n");
        return;
    }
    ptr++; // Skip '|'
    int duration_minutes = atoi(ptr);
    
    // Tìm vị trí bắt đầu questions (sau duration_minutes)
    ptr = strchr(ptr, '|');
    if (!ptr) {
        printf("[ERROR] No questions data\n");
        return;
    }
    ptr++; // Skip '|'
    
    printf("[DEBUG] Remaining: %ld seconds, Duration: %d minutes\n", remaining_seconds, duration_minutes);
    
    // Set exam_duration từ server (CHÍNH XÁC)
    exam_duration = duration_minutes;
    
    // Tính exam_start_time ngược lại từ remaining và duration
    time_t now = time(NULL);
    long elapsed = (duration_minutes * 60) - remaining_seconds;
    exam_start_time = now - elapsed;
    
    printf("[DEBUG] Calculated start_time: %ld (now: %ld, elapsed: %ld)\n", 
           exam_start_time, now, elapsed);
=======
    // Parse response
    char *ptr = data_copy;
    strtok(ptr, "|"); // Skip "RESUME_EXAM_OK"
    int remaining_seconds = atoi(strtok(NULL, "|"));
    
    // Set timer
    exam_duration = (remaining_seconds + 59) / 60;
    exam_start_time = time(NULL) - (exam_duration * 60 - remaining_seconds);
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
    
    
    // Count questions
    total_questions = 0;
    while (strtok(NULL, "|\n") != NULL) {
        total_questions++;
    }
    
    
    if (total_questions == 0) {
        show_error_dialog("No questions found in resume data");
        free(data_copy);
        return;
    }
    
    // Allocate memory
    questions = malloc(sizeof(Question) * total_questions);
    question_frames = malloc(sizeof(GtkWidget*) * total_questions);
    answered_questions = calloc(total_questions, sizeof(int));
    selected_answers = malloc(sizeof(int) * total_questions);
    if (selected_answers) {
        for (int i = 0; i < total_questions; i++) {
            selected_answers[i] = -1;
        }
    }
    
    // Parse questions again from original buffer
    free(data_copy);
    data_copy = strdup(resume_data);
    ptr = data_copy;
    strtok(ptr, "|"); // Skip "RESUME_EXAM_OK"
    strtok(NULL, "|"); // Skip remaining_seconds
    
    int q_idx = 0;
    char *q_token;
    while ((q_token = strtok(NULL, "|\n")) != NULL && q_idx < total_questions) {
        // Parse: q_id:text:optA:optB:optC:optD[:difficulty]:saved_answer
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
        
        // Hiện tại server gửi chuỗi không có difficulty (chỉ có saved_answer),
        // nhưng để tương thích nếu sau này thêm difficulty, ta xử lý linh hoạt:
        char *difficulty_str = NULL;
        char *saved_str = NULL;

        if (q_ptr && strchr(q_ptr, ':')) {
            // Có thêm trường difficulty: phần còn lại = "difficulty:saved_answer"
            difficulty_str = strsep(&q_ptr, ":");
            saved_str = strsep(&q_ptr, ":");
        } else if (q_ptr) {
            // Chỉ có saved_answer
            saved_str = q_ptr;
        }

        // Gán difficulty (nếu không có thì default Medium)
        if (difficulty_str && strlen(difficulty_str) > 0) {
            strncpy(questions[q_idx].difficulty, difficulty_str,
                    sizeof(questions[q_idx].difficulty) - 1);
        } else {
            strcpy(questions[q_idx].difficulty, "Medium");
        }

        // Parse saved answer
        int saved_val = (saved_str ? atoi(saved_str) : -1);
        if (saved_val >= 0) {
            answered_questions[q_idx] = 1;
            if (selected_answers) {
                selected_answers[q_idx] = saved_val;
            }
        }
        
        printf("[DEBUG] Resuming question %d: id=%d, diff=%s, answered=%d\n",
               q_idx + 1, questions[q_idx].question_id, questions[q_idx].difficulty, answered_questions[q_idx]);
        q_idx++;
    }
    
    total_questions = q_idx;
    free(data_copy);
    
    if (total_questions == 0) {
        show_error_dialog("Failed to parse questions");
        cleanup_exam_ui();
        return;
    }
    
    // Register broadcast callbacks
    broadcast_on_room_deleted(on_room_deleted_broadcast);
    
    // Start listening for broadcasts
    if (!broadcast_is_listening()) {
        broadcast_start_listener();
    }
    
    // Initialize current question
    current_question_index = 0;
    
    // Show the exam screen using the new layout
    show_exam_question_screen();
    
    // Start timer
    timer_id = g_timeout_add(1000, update_exam_timer, NULL);
}
