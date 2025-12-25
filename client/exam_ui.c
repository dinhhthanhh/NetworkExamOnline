#include "exam_ui.h"
#include "net.h"
#include "room_ui.h"
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
static GtkWidget *timer_label = NULL;
static GtkWidget **question_radios = NULL; // Array of radio button groups
static GtkWidget **question_frames = NULL;

typedef struct {
    int question_id;
    char text[512];
    char options[4][128];
} Question;

static Question *questions = NULL;

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
                                                             "❌ Auto-submit failed: %s",
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
             "<span foreground='%s' size='16000' weight='bold'>⏱️ %02d:%02d</span>",
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
    snprintf(msg, sizeof(msg), "SAVE_ANSWER|%d|%d|%d+1\n", 
             exam_room_id, question_id, selected);
    send_message(msg);
    
    // Không cần đợi response để UX mượt hơn
    printf("[DEBUG] Saved answer Q%d = %d+1\n", question_id, selected);
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
    
    // Confirm dialog (trừ khi auto-submit do hết giờ)
    if (widget != NULL) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_YES_NO,
                                                   "Submit your exam?");
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
    
    printf("[DEBUG] Submitting exam for room: %d\n", submit_room_id);
    
    // Gửi SUBMIT_TEST
    char msg[64];
    snprintf(msg, sizeof(msg), "SUBMIT_TEST|%d\n", submit_room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    if (n > 0 && strncmp(buffer, "SUBMIT_TEST_OK", 14) == 0) {
        // Parse: SUBMIT_TEST_OK|score|total|time_minutes
        strtok(buffer, "|"); // Skip "SUBMIT_TEST_OK"
        int score = atoi(strtok(NULL, "|"));
        int total = atoi(strtok(NULL, "|"));
        int time_taken = atoi(strtok(NULL, "|"));
        
        // Cleanup trước khi show dialog
        cleanup_exam_ui();
        
        GtkWidget *result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_INFO,
                                                         GTK_BUTTONS_OK,
                                                         "✅ Exam Submitted!");
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
                                                         "❌ Submit failed: %s",
                                                         buffer);
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        
        // Redirect về room list
        create_test_mode_screen();
    }
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
    
    // Gửi BEGIN_EXAM để load questions
    char msg[64];
    snprintf(msg, sizeof(msg), "BEGIN_EXAM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    printf("[DEBUG] BEGIN_EXAM response (%zd bytes): %s\n", n, buffer);
    
    if (n <= 0 || strncmp(buffer, "BEGIN_EXAM_OK", 13) != 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "❌ Cannot start exam");
        
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
    
    // Lưu copy của buffer NGAY từ đầu
    char *original_buffer = strdup(buffer);
    
    // Parse response: BEGIN_EXAM_OK|duration|q1_id:text:A:B:C:D|q2_id:...
    char *ptr = buffer;
    strtok(ptr, "|"); // Skip "BEGIN_EXAM_OK"
    exam_duration = atoi(strtok(NULL, "|"));
    exam_start_time = time(NULL);
    
    printf("[DEBUG] Duration: %d minutes\n", exam_duration);
    
    // Đếm số câu hỏi
    total_questions = 0;
    while (strtok(NULL, "|") != NULL) {
        total_questions++;
    }
    
    printf("[DEBUG] Total questions: %d\n", total_questions);
    
    if (total_questions == 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "❌ No questions in this room");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        return;
    }
    
    // Allocate memory
    questions = malloc(sizeof(Question) * total_questions);
    question_radios = malloc(sizeof(GtkWidget*) * total_questions * 4);
    question_frames = malloc(sizeof(GtkWidget*) * total_questions);
    
    // Parse lại từ original buffer để lấy questions
    ptr = original_buffer;
    strtok(ptr, "|"); // Skip "BEGIN_EXAM_OK"
    strtok(NULL, "|"); // Skip duration
    
    int q_idx = 0;
    char *q_token;
    while ((q_token = strtok(NULL, "|")) != NULL && q_idx < total_questions) {
        // Parse: q_id:text:optA:optB:optC:optD
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
        
        printf("[DEBUG] Q%d: %s\n", questions[q_idx].question_id, questions[q_idx].text);
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
                                                         "❌ No questions found");
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
                        "<span size='20000' weight='bold'>📝 EXAM</span>");
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
    
    printf("[DEBUG] Creating %d question widgets...\n", total_questions);
    
    // Hiển thị từng câu hỏi
    for (int i = 0; i < total_questions; i++) {
        GtkWidget *q_frame = gtk_frame_new(NULL);
        question_frames[i] = q_frame;
        GtkWidget *q_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_set_margin_top(q_vbox, 10);
        gtk_widget_set_margin_bottom(q_vbox, 10);
        gtk_widget_set_margin_start(q_vbox, 15);
        gtk_widget_set_margin_end(q_vbox, 15);
        
        // Question text
        char q_text[600];
        snprintf(q_text, sizeof(q_text), 
                "<b>%d. %s</b>", i + 1, questions[i].text);
        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_text);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_box_pack_start(GTK_BOX(q_vbox), q_label, FALSE, FALSE, 0);
        
        // Radio buttons cho options
        GSList *group = NULL;
        const char *labels[] = {"A", "B", "C", "D"};

        /* Dummy hidden radio to avoid auto-selection */
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
        
        printf("[DEBUG] Added question %d: %s\n", i+1, questions[i].text);
    }
    
    gtk_container_add(GTK_CONTAINER(viewport), questions_box);
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

    if (question_frames) {
    free(question_frames);
    question_frames = NULL;
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
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    
    GtkWidget *title = gtk_label_new(NULL);
    char title_text[128];
    snprintf(title_text, sizeof(title_text),
             "<span size='x-large' weight='bold'>📝 EXAM (Room %d) - RESUMED</span>",
             room_id);
    gtk_label_set_markup(GTK_LABEL(title), title_text);
    
    timer_label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(timer_label),
                        "<span foreground='#2c3e50' size='16000' weight='bold'>⏱️ Loading...</span>");
    
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header_box), timer_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    
    // Scroll window cho câu hỏi
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    GtkWidget *questions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_start(questions_box, 10);
    gtk_widget_set_margin_end(questions_box, 10);
    
    // Tạo UI cho từng câu hỏi
    for (int i = 0; i < total_questions; i++) {
        GtkWidget *q_frame = gtk_frame_new(NULL);
        question_frames[i] = q_frame;
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

        /* Dummy radio */
        GtkWidget *dummy = gtk_radio_button_new(NULL);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(dummy));

        for (int j = 0; j < 4; j++) {
            char opt_text[150];
            snprintf(opt_text, sizeof(opt_text),
                    "%s. %s", labels[j], questions[i].options[j]);

            GtkWidget *radio = gtk_radio_button_new_with_label(group, opt_text);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));

            /* Restore saved answer */
            if (saved_answers[i] == j) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);
            }

            if (saved_answers[i] >= 0 && question_frames && question_frames[i]) {
                GtkStyleContext *ctx =
                    gtk_widget_get_style_context(question_frames[i]);
                gtk_style_context_add_class(ctx, "question-answered");
            }

            g_signal_connect(radio, "toggled",
                            G_CALLBACK(on_answer_selected),
                            GINT_TO_POINTER(i));

            question_radios[i * 4 + j] = radio;
            gtk_box_pack_start(GTK_BOX(q_box), radio, FALSE, FALSE, 0);
        }
        
        gtk_container_add(GTK_CONTAINER(q_frame), q_box);
        gtk_box_pack_start(GTK_BOX(questions_box), q_frame, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(scroll), questions_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    // Submit button
    GtkWidget *submit_btn = gtk_button_new_with_label("✅ SUBMIT EXAM");
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
