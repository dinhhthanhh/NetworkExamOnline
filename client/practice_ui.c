#include "practice_ui.h"
#include "net.h"
#include "ui.h"
#include <time.h>
#include <string.h>

extern GtkWidget *main_window;
extern int sock_fd;

// Global state
static int practice_duration = 0;
static int practice_total_questions = 0;
static time_t practice_start_time = 0;
static guint practice_timer_id = 0;
static GtkWidget *practice_timer_label = NULL;
static GtkWidget **practice_question_radios = NULL;

typedef struct {
    int question_id;
    char text[512];
    char options[4][128];
} PracticeQuestion;

static PracticeQuestion *practice_questions = NULL;

// Timer callback cho practice mode
static gboolean update_practice_timer(gpointer data) {
    if (!practice_start_time) return FALSE;
    
    time_t now = time(NULL);
    long elapsed = now - practice_start_time;
    long remaining = (practice_duration * 60) - elapsed;
    
    if (remaining <= 0) {
        // Hết giờ - auto submit
        gtk_label_set_markup(GTK_LABEL(practice_timer_label), 
                            "<span foreground='red' size='16000' weight='bold'>⏰ TIME'S UP!</span>");
        
        if (practice_timer_id > 0) {
            g_source_remove(practice_timer_id);
            practice_timer_id = 0;
        }
        
        // Auto submit practice
        char msg[64];
        snprintf(msg, sizeof(msg), "SUBMIT_PRACTICE\n");
        send_message(msg);
        
        char buffer[BUFFER_SIZE];
        ssize_t n = receive_message(buffer, sizeof(buffer));
        
        if (n > 0 && strncmp(buffer, "PRACTICE_RESULT", 15) == 0) {
            // Parse: PRACTICE_RESULT|score|total|time_minutes
            strtok(buffer, "|");
            int score = atoi(strtok(NULL, "|"));
            int total = atoi(strtok(NULL, "|"));
            int time_taken = atoi(strtok(NULL, "|"));
            
            cleanup_practice_ui();
            
            GtkWidget *result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                             GTK_DIALOG_MODAL,
                                                             GTK_MESSAGE_INFO,
                                                             GTK_BUTTONS_OK,
                                                             "⏰ Time's Up! Practice Auto-Submitted!");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                     "Score: %d/%d (%.1f%%)\nTime: %d minutes",
                                                     score, total, (score * 100.0) / total, time_taken);
            gtk_dialog_run(GTK_DIALOG(result_dialog));
            gtk_widget_destroy(result_dialog);
            
            create_main_menu(NULL, NULL);
        } else {
            cleanup_practice_ui();
            
            GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                             GTK_DIALOG_MODAL,
                                                             GTK_MESSAGE_ERROR,
                                                             GTK_BUTTONS_OK,
                                                             "❌ Auto-submit failed: %s",
                                                             buffer);
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
            
            create_main_menu(NULL, NULL);
        }
        
        return FALSE;
    }
    
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    
    char timer_text[128];
    const char *color = (remaining < 300) ? "red" : "#27ae60";
    snprintf(timer_text, sizeof(timer_text),
             "<span foreground='%s' size='16000' weight='bold'>⏱️ %02d:%02d</span>",
             color, minutes, seconds);
    
    gtk_label_set_markup(GTK_LABEL(practice_timer_label), timer_text);
    
    return TRUE;
}

// Callback khi user chọn đáp án
void on_practice_answer_selected(GtkWidget *widget, gpointer data) {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        return;
    }
    
    int question_index = GPOINTER_TO_INT(data);
    int question_id = practice_questions[question_index].question_id;
    
    int selected = -1;
    for (int i = 0; i < 4; i++) {
        GtkWidget *radio = practice_question_radios[question_index * 4 + i];
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio))) {
            selected = i;
            break;
        }
    }
    
    if (selected == -1) return;
    
    // Gửi SAVE_PRACTICE_ANSWER
    char msg[128];
    snprintf(msg, sizeof(msg), "SAVE_PRACTICE_ANSWER|%d|%d\n", question_id, selected);
    send_message(msg);
    
    printf("[DEBUG] Saved practice answer Q%d = %d\n", question_id, selected);
}

// Callback submit practice
void on_submit_practice_clicked(GtkWidget *widget, gpointer data) {
    if (practice_timer_id > 0) {
        g_source_remove(practice_timer_id);
        practice_timer_id = 0;
    }
    
    if (widget != NULL) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_YES_NO,
                                                   "Submit your practice?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 "You can review your answers after submission.");
        
        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        if (response != GTK_RESPONSE_YES) {
            practice_timer_id = g_timeout_add(1000, update_practice_timer, NULL);
            return;
        }
    }
    
    // Gửi SUBMIT_PRACTICE
    char msg[64];
    snprintf(msg, sizeof(msg), "SUBMIT_PRACTICE\n");
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    if (n > 0 && strncmp(buffer, "PRACTICE_RESULT", 15) == 0) {
        // Parse: PRACTICE_RESULT|score|total|time_minutes
        strtok(buffer, "|");
        int score = atoi(strtok(NULL, "|"));
        int total = atoi(strtok(NULL, "|"));
        int time_taken = atoi(strtok(NULL, "|"));
        
        cleanup_practice_ui();
        
        GtkWidget *result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_INFO,
                                                         GTK_BUTTONS_OK,
                                                         "✅ Practice Submitted!");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                 "Score: %d/%d (%.1f%%)\nTime: %d minutes\n\n"
                                                 "Keep practicing to improve! 💪",
                                                 score, total, (score * 100.0) / total, time_taken);
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
        
        create_main_menu(NULL, NULL);
        
    } else {
        cleanup_practice_ui();
        
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "❌ Submit failed: %s",
                                                         buffer);
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        
        create_main_menu(NULL, NULL);
    }
}

void create_practice_screen() {
    // Gửi BEGIN_PRACTICE để load questions
    send_message("BEGIN_PRACTICE\n");
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    printf("[DEBUG] BEGIN_PRACTICE response: %s\n", buffer);
    
    if (n <= 0 || strncmp(buffer, "PRACTICE_OK", 11) != 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "❌ Cannot start practice");
        
        if (strncmp(buffer, "ERROR", 5) == 0) {
            char *msg = strchr(buffer, '|');
            if (msg) {
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                                                         "%s", msg + 1);
            }
        }
        
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        create_main_menu(NULL, NULL);
        return;
    }
    
    // Lưu copy buffer
    char *original_buffer = strdup(buffer);
    
    // Parse: PRACTICE_OK|duration|q1_id:text:A:B:C:D|q2_id:...
    char *ptr = buffer;
    strtok(ptr, "|");
    practice_duration = atoi(strtok(NULL, "|"));
    practice_start_time = time(NULL);
    
    // Đếm số câu hỏi
    practice_total_questions = 0;
    while (strtok(NULL, "|") != NULL) {
        practice_total_questions++;
    }
    
    if (practice_total_questions == 0) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "❌ No practice questions available");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        free(original_buffer);
        create_main_menu(NULL, NULL);
        return;
    }
    
    // Allocate memory
    practice_questions = malloc(sizeof(PracticeQuestion) * practice_total_questions);
    practice_question_radios = malloc(sizeof(GtkWidget*) * practice_total_questions * 4);
    
    // Parse lại questions
    ptr = original_buffer;
    strtok(ptr, "|");
    strtok(NULL, "|");
    
    int q_idx = 0;
    char *q_token;
    while ((q_token = strtok(NULL, "|")) != NULL && q_idx < practice_total_questions) {
        char *q_ptr = q_token;
        
        char *id_str = strsep(&q_ptr, ":");
        if (!id_str || !q_ptr) continue;
        practice_questions[q_idx].question_id = atoi(id_str);
        
        char *text = strsep(&q_ptr, ":");
        if (!text || !q_ptr) continue;
        strncpy(practice_questions[q_idx].text, text, sizeof(practice_questions[q_idx].text) - 1);
        
        for (int i = 0; i < 4; i++) {
            char *opt = strsep(&q_ptr, ":");
            if (!opt) {
                strcpy(practice_questions[q_idx].options[i], "???");
            } else {
                strncpy(practice_questions[q_idx].options[i], opt, 
                       sizeof(practice_questions[q_idx].options[i]) - 1);
            }
        }
        
        q_idx++;
    }
    
    practice_total_questions = q_idx;
    free(original_buffer);
    
    // Tạo UI
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    
    // Header
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
                        "<span size='20000' weight='bold'>🎯 PRACTICE MODE</span>");
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    
    practice_timer_label = gtk_label_new(NULL);
    gtk_box_pack_end(GTK_BOX(header_box), practice_timer_label, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    
    // Scrolled window
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);
    
    GtkWidget *viewport = gtk_viewport_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), viewport);
    
    GtkWidget *questions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(questions_box, 10);
    gtk_widget_set_margin_bottom(questions_box, 10);
    gtk_widget_set_margin_start(questions_box, 10);
    gtk_widget_set_margin_end(questions_box, 10);
    
    // Display questions
    for (int i = 0; i < practice_total_questions; i++) {
        GtkWidget *q_frame = gtk_frame_new(NULL);
        GtkWidget *q_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_set_margin_top(q_vbox, 10);
        gtk_widget_set_margin_bottom(q_vbox, 10);
        gtk_widget_set_margin_start(q_vbox, 15);
        gtk_widget_set_margin_end(q_vbox, 15);
        
        char q_text[600];
        snprintf(q_text, sizeof(q_text), "<b>%d. %s</b>", i + 1, practice_questions[i].text);
        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_text);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_box_pack_start(GTK_BOX(q_vbox), q_label, FALSE, FALSE, 0);
        
        GSList *group = NULL;
        const char *labels[] = {"A", "B", "C", "D"};
        
        for (int j = 0; j < 4; j++) {
            char option_text[150];
            snprintf(option_text, sizeof(option_text), "%s. %s", 
                    labels[j], practice_questions[i].options[j]);
            
            GtkWidget *radio = gtk_radio_button_new_with_label(group, option_text);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
            
            // QUAN TRỌNG: Không chọn mặc định
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), FALSE);
            
            g_signal_connect(radio, "toggled", 
                           G_CALLBACK(on_practice_answer_selected), 
                           GINT_TO_POINTER(i));
            
            gtk_box_pack_start(GTK_BOX(q_vbox), radio, FALSE, FALSE, 0);
            practice_question_radios[i * 4 + j] = radio;
        }
        
        gtk_container_add(GTK_CONTAINER(q_frame), q_vbox);
        gtk_box_pack_start(GTK_BOX(questions_box), q_frame, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(viewport), questions_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    // Submit button
    GtkWidget *submit_btn = gtk_button_new_with_label("📤 SUBMIT PRACTICE");
    gtk_widget_set_size_request(submit_btn, 200, 50);
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_practice_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), submit_btn, FALSE, FALSE, 0);
    
    // Show UI
    GtkWidget *old_child = gtk_bin_get_child(GTK_BIN(main_window));
    if (old_child) {
        gtk_container_remove(GTK_CONTAINER(main_window), old_child);
    }
    gtk_container_add(GTK_CONTAINER(main_window), vbox);
    gtk_widget_show_all(main_window);
    
    // Start timer
    practice_timer_id = g_timeout_add(1000, update_practice_timer, NULL);
}

void cleanup_practice_ui() {
    if (practice_timer_id > 0) {
        g_source_remove(practice_timer_id);
        practice_timer_id = 0;
    }
    
    GtkWidget *old_child = gtk_bin_get_child(GTK_BIN(main_window));
    if (old_child) {
        gtk_container_remove(GTK_CONTAINER(main_window), old_child);
    }
    
    if (practice_questions) {
        free(practice_questions);
        practice_questions = NULL;
    }
    
    if (practice_question_radios) {
        free(practice_question_radios);
        practice_question_radios = NULL;
    }
    
    practice_duration = 0;
    practice_start_time = 0;
    practice_total_questions = 0;
}