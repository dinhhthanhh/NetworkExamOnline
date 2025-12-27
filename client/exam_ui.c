#include "exam_ui.h"
#include "net.h"
#include "room_ui.h"
#include "ui.h"
#include "ui_utils.h"
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
static int total_questions = 0;
static GtkWidget **question_radios = NULL;
static GtkWidget **question_frames = NULL;
static int is_submitting = 0; 
extern GtkWidget *timer_label;

typedef struct {
    int question_id;
    char text[512];
    char options[4][128];
    char difficulty[32];
} Question;

static Question *questions = NULL;

// Safe string tokenizer that doesn't modify original string
static char* safe_strtok(char **str, const char *delim) {
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

static gboolean auto_submit_wrapper(gpointer data) {
    on_submit_exam_clicked(NULL, NULL);
    return FALSE; // Chạy 1 lần rồi thôi
}

// Timer callback - đếm ngược (exam-specific)
static gboolean exam_update_timer(gpointer data) {
    if (!exam_start_time || is_submitting) return FALSE;
    
    time_t now = time(NULL);
    long elapsed = now - exam_start_time;
    long remaining = (exam_duration * 60) - elapsed;

    if (remaining <= 0) {
        // Hết giờ - Set timer label về 00:00
        if (timer_label) {
            gtk_label_set_markup(GTK_LABEL(timer_label), 
                            "<span foreground='red' size='16000' weight='bold'> 00:00</span>");
        }

        timer_id = 0; 

        g_idle_add(auto_submit_wrapper, NULL);
        
        return FALSE;
    }
    
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    
    char timer_text[128];
    const char *color = (remaining < 300) ? "red" : "#2c3e50";
        snprintf(timer_text, sizeof(timer_text),
                 "<span foreground='%s' size='16000' weight='bold'>Time %02d:%02d</span>",
                 color, minutes, seconds);
    
    if (timer_label) {
        gtk_label_set_markup(GTK_LABEL(timer_label), timer_text);
    }
    
    return TRUE;
}

// Callback khi user chọn đáp án - auto save
void on_answer_selected(GtkWidget *widget, gpointer data) {
    if (questions == NULL || is_submitting) {
        return;
    }
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        return;
    }
    
    int question_index = GPOINTER_TO_INT(data);
    int question_id = questions[question_index].question_id;

    /* Highlight question frame when answered */
    if (question_frames && question_frames[question_index]) {
        GtkStyleContext *ctx =
            gtk_widget_get_style_context(question_frames[question_index]);
        gtk_style_context_add_class(ctx, "question-answered");
    }
        
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
    // Display uses 1-4 for A-D; send 1-4 to server
    snprintf(msg, sizeof(msg), "SAVE_ANSWER|%d|%d|%d\n", 
             exam_room_id, question_id, selected + 1);
    send_message(msg);

    // Answer saved (debug prints removed)
}

// Callback submit bài thi
void on_submit_exam_clicked(GtkWidget *widget, gpointer data) {
    // Tránh double submit
    if (is_submitting) {
        // Already submitting, ignore repeated requests
        return;
    }
    
    // 1. Stop timer ngay lập tức nếu còn chạy
    if (timer_id > 0) {
        g_source_remove(timer_id);
        timer_id = 0;
    }

    // 2. Kiểm tra room ID trước khi submit
    if (exam_room_id == 0) {
        show_error_dialog("Internal Error", "Invalid exam room ID. Submit aborted.");
        return;
    }

    int submit_room_id = exam_room_id;
    int is_timeout = (widget == NULL); // NULL = auto-submit do hết giờ
    
    // Confirm dialog (show answered count) - only when user clicked Submit
    if (!is_timeout) {
        int answered = 0;
        if (question_radios && total_questions > 0) {
            for (int i = 0; i < total_questions; i++) {
                for (int j = 0; j < 4; j++) {
                    GtkWidget *r = question_radios[i * 4 + j];
                    if (r && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r))) {
                        answered++;
                        break;
                    }
                }
            }
        }

        int unanswered = total_questions - answered;
        char confirm_msg[256];
        snprintf(confirm_msg, sizeof(confirm_msg),
             "You have answered %d/%d questions. Unanswered: %d.\nAre you sure you want to submit?",
             answered, total_questions, unanswered);

        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_YES_NO,
                                                   "%s", confirm_msg);

        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

            if (response != GTK_RESPONSE_YES) {
            // User cancelled - restart timer if there's remaining time
            if (exam_start_time > 0) {
                timer_id = g_timeout_add(1000, exam_update_timer, NULL);
            }
            return;
        }
    }

    is_submitting = 1;
    
    // Flush socket buffer trước khi gửi
    flush_socket_buffer(client.socket_fd);
    
    // Gửi SUBMIT_TEST
    char msg[64];
    snprintf(msg, sizeof(msg), "SUBMIT_TEST|%d\n", submit_room_id);
    send_message(msg);
    
    // Nhận response với timeout
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    ssize_t n = receive_message_timeout(buffer, sizeof(buffer), 10);
    
    // Submit response received
    
    if (n > 0 && strncmp(buffer, "SUBMIT_TEST_OK", 14) == 0) {
        // Parse response an toàn
        // Format: SUBMIT_TEST_OK|score|total|time_minutes
        char *buffer_copy = strdup(buffer);
        char *ptr = buffer_copy;
        
        char *token = safe_strtok(&ptr, "|"); // "SUBMIT_TEST_OK"
        int score = 0, total = 0, time_taken = 0;
        
        if ((token = safe_strtok(&ptr, "|")) != NULL) {
            score = atoi(token);
        }
        if ((token = safe_strtok(&ptr, "|")) != NULL) {
            total = atoi(token);
        }
        if ((token = safe_strtok(&ptr, "|")) != NULL) {
            time_taken = atoi(token);
        }
        
        free(buffer_copy);
        
        // Show result dialog
        GtkWidget *result_dialog = gtk_message_dialog_new(
            GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            widget != NULL ? " TIME'S UP!" : " Exam Submitted!");
        
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(result_dialog),
            "Score: %d/%d (%.1f%%)\nTime: %d minutes",
            score, total, total > 0 ? (score * 100.0) / total : 0, time_taken);
        
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
        
    } else {
        // Error case
        GtkWidget *error_dialog = gtk_message_dialog_new(
            GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            " Submit failed");
        
        if (n > 0) {
            gtk_message_dialog_format_secondary_text(
                GTK_MESSAGE_DIALOG(error_dialog),
                "Server response: %s", buffer);
        } else {
            gtk_message_dialog_format_secondary_text(
                GTK_MESSAGE_DIALOG(error_dialog),
                "No response from server. Connection lost.");
        }
        
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
    }

    // Cleanup SAU KHI dialog đóng để tránh use-after-free
    cleanup_exam_ui();

    g_idle_add(go_back_to_room, NULL);
    
}

// Tạo UI exam page
void create_exam_page(int room_id) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".question-answered {"
        "  background: #E8F8F5;"
        "  border: 2px solid #2ecc71;"
        "  border-radius: 8px;"
        "}"
        ".question-answered:hover {"
        "  background: #D1F2EB;"
        "}",
        -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    exam_room_id = room_id;
    is_submitting = 0;
    
    // Flush socket buffer trước khi gửi request mới
    flush_socket_buffer(client.socket_fd);
    
    // Gửi BEGIN_EXAM để load questions
    char msg[64];
    snprintf(msg, sizeof(msg), "BEGIN_EXAM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE * 2]; 
    memset(buffer, 0, sizeof(buffer));
    ssize_t n = receive_message_timeout(buffer, sizeof(buffer), 10);
    
    // BEGIN_EXAM response received
    
    if (n <= 0 || strncmp(buffer, "BEGIN_EXAM_OK", 13) != 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(
            GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            " Cannot start exam");
        
        if (n > 0 && strncmp(buffer, "ERROR", 5) == 0) {
            char *msg_ptr = strchr(buffer, '|');
            if (msg_ptr) {
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(error_dialog),
                    "%s", msg_ptr + 1);
            }
        }
        
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }
    
    // Parse response an toàn
    char *buffer_copy = strdup(buffer);
    char *ptr = buffer_copy;
    
    // Parse: BEGIN_EXAM_OK|duration|q1_id:text:A:B:C:D|q2_id:...
    char *token = safe_strtok(&ptr, "|"); // Skip "BEGIN_EXAM_OK"
    if (!token) goto parse_error;
    
    token = safe_strtok(&ptr, "|"); // Duration
    if (!token) goto parse_error;
    exam_duration = atoi(token);
    exam_start_time = time(NULL);
    
    // Exam duration received
    
    // Đếm số câu hỏi chính xác
    total_questions = 0;
    char *temp_ptr = strdup(ptr);
    char *count_ptr = temp_ptr;
    while (count_ptr && *count_ptr) {
        char *q_data = safe_strtok(&count_ptr, "|");
        if (q_data && strlen(q_data) > 0) {
            total_questions++;
        }
    }
    free(temp_ptr);
    
    // Total questions parsed
    
    if (total_questions == 0) {
        free(buffer_copy);
        GtkWidget *error_dialog = gtk_message_dialog_new(
            GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            " No questions in this room");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }
    
    // Allocate memory
    questions = calloc(total_questions, sizeof(Question));
    question_radios = calloc(total_questions * 4, sizeof(GtkWidget*));
    question_frames = calloc(total_questions, sizeof(GtkWidget*));
    
    if (!questions || !question_radios || !question_frames) {
        goto parse_error;
    }
    
    // Parse từng câu hỏi
    int q_idx = 0;
    while (ptr && q_idx < total_questions) {
        char *q_data = safe_strtok(&ptr, "|");
        if (!q_data) break;
        
        // Parse: id:text:optA:optB:optC:optD
        char *q_ptr = q_data;
        
        char *id_str = safe_strtok(&q_ptr, ":");
        if (!id_str) continue;
        questions[q_idx].question_id = atoi(id_str);
        
        char *text = safe_strtok(&q_ptr, ":");
        if (!text) continue;
        strncpy(questions[q_idx].text, text, sizeof(questions[q_idx].text) - 1);
        
        for (int i = 0; i < 4; i++) {
            char *opt = safe_strtok(&q_ptr, ":");
            if (opt) {
                strncpy(questions[q_idx].options[i], opt, 
                       sizeof(questions[q_idx].options[i]) - 1);
            } else {
                strcpy(questions[q_idx].options[i], "???");
            }
        }

        // Difficulty (optional)
        char *diff = safe_strtok(&q_ptr, ":");
        if (diff) {
            strncpy(questions[q_idx].difficulty, diff, sizeof(questions[q_idx].difficulty)-1);
        } else {
            questions[q_idx].difficulty[0] = '\0';
        }
        
        // Question parsed
        q_idx++;
    }
    
    total_questions = q_idx;
    free(buffer_copy);
    
    if (total_questions == 0) {
        goto parse_error;
    }
    
    // Successfully parsed questions
    
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
                        "<span size='20000' weight='bold'>EXAM</span>");
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
    
    GtkWidget *viewport = gtk_viewport_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), viewport);
    
    GtkWidget *questions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(questions_box, 10);
    gtk_widget_set_margin_bottom(questions_box, 10);
    gtk_widget_set_margin_start(questions_box, 10);
    gtk_widget_set_margin_end(questions_box, 10);
    
    // Hiển thị từng câu hỏi
    for (int i = 0; i < total_questions; i++) {
        GtkWidget *q_frame = gtk_frame_new(NULL);
        question_frames[i] = q_frame;
        GtkWidget *q_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_set_margin_top(q_vbox, 10);
        gtk_widget_set_margin_bottom(q_vbox, 10);
        gtk_widget_set_margin_start(q_vbox, 15);
        gtk_widget_set_margin_end(q_vbox, 15);
        
        // Question text + difficulty
        char q_text[700];
        if (questions[i].difficulty[0] != '\0') {
                char *diff_markup = difficulty_markup(questions[i].difficulty);
                snprintf(q_text, sizeof(q_text), "<b>%d. %s</b> %s",
                    i + 1, questions[i].text, diff_markup);
                g_free(diff_markup);
        } else {
            snprintf(q_text, sizeof(q_text), "<b>%d. %s</b>", i + 1, questions[i].text);
        }
        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_text);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_box_pack_start(GTK_BOX(q_vbox), q_label, FALSE, FALSE, 0);
        
        // Radio buttons
        GSList *group = NULL;
        const char *labels[] = {"A", "B", "C", "D"};

        GtkWidget *dummy = gtk_radio_button_new(NULL);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(dummy));

        for (int j = 0; j < 4; j++) {
            char option_text[150];
            snprintf(option_text, sizeof(option_text),
                    "%s. %s", labels[j], questions[i].options[j]);

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
    }
    
    gtk_container_add(GTK_CONTAINER(viewport), questions_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    // Submit button
    GtkWidget *submit_btn = gtk_button_new_with_label("SUBMIT EXAM");
    gtk_widget_set_size_request(submit_btn, 200, 50);
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_exam_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), submit_btn, FALSE, FALSE, 0);
    
    // Show UI via show_view to keep `current_view` in sync
    show_view(vbox);
    
    // Start timer
    timer_id = g_timeout_add(1000, exam_update_timer, NULL);
    return;

parse_error:
    show_error_dialog("Error", "Failed to parse exam data");
    if (buffer_copy) free(buffer_copy);
    cleanup_exam_ui();
    
    GtkWidget *error_dialog = gtk_message_dialog_new(
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        " Failed to parse exam data");
    gtk_dialog_run(GTK_DIALOG(error_dialog));
    gtk_widget_destroy(error_dialog);
}

void cleanup_exam_ui() {
    // Cleaning up exam UI
    
    // Stop timer first
    if (timer_id > 0) {
        g_source_remove(timer_id);
        timer_id = 0;
    }
    
    // Nullify timer_label để tránh use-after-free trong timer callback
    timer_label = NULL;
    
    // Free allocated memory
    if (questions) {
        free(questions);
        questions = NULL;
    }
    
    if (question_radios) {
        free(question_radios);
        question_radios = NULL;
    }
    
    if (question_frames) {
        free(question_frames);
        question_frames = NULL;
    }
    
    // Reset state
    exam_room_id = 0;
    exam_duration = 0;
    exam_start_time = 0;
    total_questions = 0;
    is_submitting = 0;
    
    // Cleanup completed
}

// Resume exam từ session cũ (tương tự, cần fix giống trên)
void create_exam_page_from_resume(int room_id, char *resume_data) {
    if (!resume_data) return;

        // Load CSS cho question-answered
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".question-answered {"
        "  background: #E8F8F5;"
        "  border: 2px solid #2ecc71;"
        "  border-radius: 8px;"
        "}"
        ".question-answered:hover {"
        "  background: #D1F2EB;"
        "}",
        -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    exam_room_id = room_id;
    is_submitting = 0;

    char *buffer_copy = strdup(resume_data);
    char *ptr = buffer_copy;

    // RESUME_EXAM_OK
    char *token = safe_strtok(&ptr, "|");
    if (!token || strcmp(token, "RESUME_EXAM_OK") != 0) {
        goto parse_error;
    }

    // duration (minutes)
    token = safe_strtok(&ptr, "|");
    if (!token) goto parse_error;
    exam_duration = atoi(token);

    // elapsed seconds
    token = safe_strtok(&ptr, "|");
    if (!token) goto parse_error;
    int elapsed = atoi(token);

    exam_start_time = time(NULL) - elapsed;

    // Resume info received

    // Đếm số câu hỏi
    total_questions = 0;
    char *temp_ptr = strdup(ptr);
    char *count_ptr = temp_ptr;
    while (count_ptr && *count_ptr) {
        char *q_data = safe_strtok(&count_ptr, "|");
        if (q_data && strlen(q_data) > 0) {
            total_questions++;
        }
    }
    free(temp_ptr);

    if (total_questions == 0) goto parse_error;

    questions = calloc(total_questions, sizeof(Question));
    question_radios = calloc(total_questions * 4, sizeof(GtkWidget *));
    question_frames = calloc(total_questions, sizeof(GtkWidget *));

    if (!questions || !question_radios || !question_frames) {
        goto parse_error;
    }

    int q_idx = 0;
    int saved_answers[256] = {0}; // đủ dùng

    while (ptr && q_idx < total_questions) {
        char *q_data = safe_strtok(&ptr, "|");
        if (!q_data) break;

        char *q_ptr = q_data;

        // id
        char *id_str = safe_strtok(&q_ptr, ":");
        if (!id_str) continue;
        questions[q_idx].question_id = atoi(id_str);

        // text
        char *text = safe_strtok(&q_ptr, ":");
        if (!text) continue;
        strncpy(questions[q_idx].text, text,
                sizeof(questions[q_idx].text) - 1);

        // options A-D
        for (int i = 0; i < 4; i++) {
            char *opt = safe_strtok(&q_ptr, ":");
            if (opt) {
                strncpy(questions[q_idx].options[i], opt,
                        sizeof(questions[q_idx].options[i]) - 1);
            } else {
                strcpy(questions[q_idx].options[i], "???");
            }
        }

        // saved answer
        char *ans = safe_strtok(&q_ptr, ":");
        saved_answers[q_idx] = ans ? atoi(ans) : 0;

        q_idx++;
    }

    total_questions = q_idx;
    if (total_questions == 0) goto parse_error;

    /* ================== UI ================== */

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<span size='20000' weight='bold'>EXAM (RESUMED)</span>");
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);

    timer_label = gtk_label_new(NULL);
    gtk_box_pack_end(GTK_BOX(header_box), timer_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);

    GtkWidget *viewport = gtk_viewport_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), viewport);

    GtkWidget *questions_box =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);

    for (int i = 0; i < total_questions; i++) {
        GtkWidget *frame = gtk_frame_new(NULL);
        question_frames[i] = frame;

        GtkWidget *q_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_add(GTK_CONTAINER(frame), q_vbox);

        char q_text[600];
        snprintf(q_text, sizeof(q_text),
                 "<b>%d. %s</b>", i + 1, questions[i].text);

        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_text);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_box_pack_start(GTK_BOX(q_vbox), q_label, FALSE, FALSE, 0);

        GSList *group = NULL;
        GtkWidget *dummy = gtk_radio_button_new(NULL);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(dummy));

        for (int j = 0; j < 4; j++) {
            char opt_text[160];
            snprintf(opt_text, sizeof(opt_text),
                     "%c. %s", 'A' + j, questions[i].options[j]);

            GtkWidget *radio =
                gtk_radio_button_new_with_label(group, opt_text);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));

            g_signal_connect(radio, "toggled",
                             G_CALLBACK(on_answer_selected),
                             GINT_TO_POINTER(i));

            gtk_box_pack_start(GTK_BOX(q_vbox), radio, FALSE, FALSE, 0);
            question_radios[i * 4 + j] = radio;

            // Restore checked answer
            if (saved_answers[i] == j + 1) {
                gtk_toggle_button_set_active(
                    GTK_TOGGLE_BUTTON(radio), TRUE);

                GtkStyleContext *ctx =
                    gtk_widget_get_style_context(frame);
                gtk_style_context_add_class(ctx, "question-answered");
            }
        }

        gtk_box_pack_start(GTK_BOX(questions_box), frame, FALSE, FALSE, 0);
    }

    gtk_container_add(GTK_CONTAINER(viewport), questions_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    GtkWidget *submit =
        gtk_button_new_with_label("SUBMIT EXAM");
    g_signal_connect(submit, "clicked",
                     G_CALLBACK(on_submit_exam_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), submit, FALSE, FALSE, 0);

    // Use show_view so current_view is tracked correctly
    show_view(vbox);

    timer_id = g_timeout_add(1000, exam_update_timer, NULL);

    free(buffer_copy);
    return;

parse_error:
    if (buffer_copy) free(buffer_copy);
    cleanup_exam_ui();

    GtkWidget *err = gtk_message_dialog_new(
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        " Failed to resume exam");
    gtk_dialog_run(GTK_DIALOG(err));
    gtk_widget_destroy(err);
}