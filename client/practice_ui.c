#include "practice_ui.h"
#include "net.h"
#include "ui.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtk.h>

extern GtkWidget *main_window;
extern int sock_fd;

// Global state
static int practice_duration = 0;
static int practice_total_questions = 0;
static time_t practice_start_time = 0;
static guint practice_timer_id = 0;
static GtkWidget *practice_timer_label = NULL;
static GtkWidget **practice_question_radios = NULL;
static GtkWidget **practice_question_frames = NULL;
static int is_submitting = 0;

typedef struct {
    int question_id;
    char text[512];
    char options[4][128];
    int correct_option; // 1=A, 2=B, 3=C, 4=D. Lưu đáp án đúng để check local
} PracticeQuestion;

static PracticeQuestion *practice_questions = NULL;

void on_submit_practice_clicked(GtkWidget *widget, gpointer data);

// --- Helper Functions ---

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
    on_submit_practice_clicked(NULL, NULL);
    return FALSE;
}

// --- Callbacks ---

static gboolean update_practice_timer(gpointer data) {
    if (!practice_start_time || is_submitting) return FALSE;

    time_t now = time(NULL);
    long elapsed = now - practice_start_time;
    long remaining = (practice_duration * 60) - elapsed;

    if (remaining <= 0) {
        if (practice_timer_label) {
             gtk_label_set_markup(GTK_LABEL(practice_timer_label),
                            "<span foreground='red' size='16000' weight='bold'> 00:00</span>");
        }
        if (practice_timer_id > 0) {
            g_source_remove(practice_timer_id);
            practice_timer_id = 0;
        }
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
    
    if (practice_timer_label) {
        gtk_label_set_markup(GTK_LABEL(practice_timer_label), timer_text);
    }

    return TRUE;
}

// Callback xử lý chọn đáp án và hiển thị Đúng/Sai ngay lập tức
static void on_practice_answer_selected(GtkWidget *widget, gpointer data) {
    if (is_submitting) return;
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

    int question_index = GPOINTER_TO_INT(data);
    int question_id = practice_questions[question_index].question_id;
    int correct_ans = practice_questions[question_index].correct_option; 

    int selected = -1;
    for (int i = 0; i < 4; i++) {
        GtkWidget *radio = practice_question_radios[question_index * 4 + i];
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio))) {
            selected = i + 1; // Chuyển về 1-based (1,2,3,4)
            break;
        }
    }
    if (selected == -1) return;

    // --- LOGIC HIỂN THỊ MÀU SẮC (CSS) ---
    if (practice_question_frames && practice_question_frames[question_index]) {
        GtkWidget *frame = practice_question_frames[question_index];
        GtkStyleContext *ctx = gtk_widget_get_style_context(frame);
        
        gtk_style_context_remove_class(ctx, "correct-frame");
        gtk_style_context_remove_class(ctx, "wrong-frame");
        gtk_style_context_remove_class(ctx, "question-answered");

        if (correct_ans > 0) {
            // Nếu có dữ liệu đáp án đúng từ server
            if (selected == correct_ans) {
                gtk_style_context_add_class(ctx, "correct-frame"); // Xanh
            } else {
                gtk_style_context_add_class(ctx, "wrong-frame");   // Đỏ
            }
        } else {
            gtk_style_context_add_class(ctx, "question-answered");
        }
    }

    // Vẫn gửi lên server để lưu trạng thái (nếu cần resume hoặc thống kê)
    char msg[128];
    snprintf(msg, sizeof(msg), "SAVE_PRACTICE_ANSWER|%d|%d\n", question_id, selected);
    send_message(msg);
}

void on_submit_practice_clicked(GtkWidget *widget, gpointer data) {
    if (is_submitting) return;

    if (practice_timer_id > 0) {
        g_source_remove(practice_timer_id);
        practice_timer_id = 0;
    }

    int is_timeout = (widget == NULL);

    if (!is_timeout) {
        int answered = 0;
        if (practice_question_radios && practice_total_questions > 0) {
            for (int i=0; i<practice_total_questions; i++) {
                for (int j=0; j<4; j++) {
                    GtkWidget *r = practice_question_radios[i*4+j];
                    if (r && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r))) {
                        answered++;
                        break;
                    }
                }
            }
        }
        int unanswered = practice_total_questions - answered;
        
        char confirm_msg[256];
        snprintf(confirm_msg, sizeof(confirm_msg), 
            "You have answered %d/%d questions. Unanswered: %d.\nSubmit practice now?", 
            answered, practice_total_questions, unanswered);

        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_QUESTION,
                                                 GTK_BUTTONS_YES_NO,
                                                 "%s", confirm_msg);
        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (response != GTK_RESPONSE_YES) {
            if (practice_start_time > 0) {
                practice_timer_id = g_timeout_add(1000, update_practice_timer, NULL);
            }
            return;
        }
    }

    is_submitting = 1;
    flush_socket_buffer(sock_fd);
    send_message("SUBMIT_PRACTICE\n");
    char buffer[BUFFER_SIZE * 2];
    ssize_t n = 0;
    char *result_ptr = NULL;

    /* Read until we find PRACTICE_RESULT or timeout/error. Ignore intermediate ANSWER_SAVED messages. */
    int attempts = 0;
    while (attempts < 40) { /* ~4 seconds total if receive_message blocks briefly */
        n = receive_message(buffer, sizeof(buffer));
        if (n <= 0) { attempts++; usleep(100000); continue; }
        if (n < (ssize_t)sizeof(buffer)) buffer[n] = '\0';
        result_ptr = strstr(buffer, "PRACTICE_RESULT");
        if (result_ptr) break;
        /* otherwise ignore and keep reading (could be ANSWER_SAVED etc.) */
        attempts++;
        usleep(100000);
    }

    if (result_ptr) {
        char *ptr = result_ptr;
        strtok(ptr, "|"); 
        int score = atoi(strtok(NULL, "|"));
        int total = atoi(strtok(NULL, "|"));
        int time_taken = atoi(strtok(NULL, "|"));

        GtkWidget *result_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_INFO,
                                        GTK_BUTTONS_OK,
                                        is_timeout ? "TIME'S UP!" : "Practice Submitted!");
        
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(result_dialog),
                                                 "Final Score: %d/%d (%.1f%%)\nTime: %d minutes",
                                                 score, total, (total>0 ? (score*100.0)/total : 0), time_taken);
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
        
        cleanup_practice_ui();
        create_main_menu();
    } else {
        /* If we never got a PRACTICE_RESULT, show a clearer error message. */
        const char *err = (n > 0 && buffer[0]) ? buffer : "No response from server";
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       "Submit failed: %s", err);
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        
        cleanup_practice_ui();
        create_main_menu();
    }
}

// --- Main Setup ---

void create_practice_screen() {
    // 1. CSS Styles: Thêm class Xanh (correct) và Đỏ (wrong)
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".correct-frame { "
        "   background-color: #d4efdf; "
        "   color: black; "
        "} "
        ".correct-frame border { "          
        "   border: 3px solid #27ae60; "
        "} "
        ".wrong-frame { "
        "   background-color: #fadbd8; "
        "   color: black; "
        "} "
        ".wrong-frame border { "
        "   border: 3px solid #c0392b; "
        "} "

        ".question-answered { "
        "   background-color: #ecf0f1; "
        "} ",
        -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    // 2. Network requests (Giữ nguyên)
    flush_socket_buffer(sock_fd);
    send_message("LIST_ROOMS\n");
    char rooms_buf[BUFFER_SIZE*2];
    ssize_t rn = receive_message(rooms_buf, sizeof(rooms_buf));
    if (rn > 0 && rn < (ssize_t)sizeof(rooms_buf)) rooms_buf[rn] = '\0';

    GtkWidget *sel_dialog = gtk_dialog_new_with_buttons("Select Practice Room",
                                                        GTK_WINDOW(main_window),
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        "Start", GTK_RESPONSE_ACCEPT,
                                                        "Cancel", GTK_RESPONSE_CANCEL,
                                                        NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(sel_dialog));
    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "-1", "Any (Random)");

    if (rn > 0 && strncmp(rooms_buf, "LIST_ROOMS_OK", 13) == 0) {
        char *copy = strdup(rooms_buf);
        char *line = strtok(copy, "\n");
        while (line) {
            if (strncmp(line, "ROOM|", 5) == 0) {
                char id[16], name[128];
                if (sscanf(line, "ROOM|%15[^|]|%127[^\n]", id, name) >= 1) {
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), id, name);
                }
            }
            line = strtok(NULL, "\n");
        }
        free(copy);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_container_add(GTK_CONTAINER(content), combo);
    gtk_widget_show_all(content);
    int resp = gtk_dialog_run(GTK_DIALOG(sel_dialog));
    
    char room_id_str[16] = "";
    if (resp == GTK_RESPONSE_ACCEPT) {
        const char *active = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
        if (active) strncpy(room_id_str, active, sizeof(room_id_str)-1);
    }
    gtk_widget_destroy(sel_dialog);
    if (room_id_str[0] == '\0') { create_main_menu(); return; }

    char begin_cmd[64];
    if (strcmp(room_id_str, "-1") == 0) snprintf(begin_cmd, sizeof(begin_cmd), "BEGIN_PRACTICE\n");
    else snprintf(begin_cmd, sizeof(begin_cmd), "BEGIN_PRACTICE|%s\n", room_id_str);

    send_message(begin_cmd);
    char buffer[BUFFER_SIZE * 2];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0 && n < (ssize_t)sizeof(buffer)) buffer[n] = '\0';

    if (n <= 0 || strncmp(buffer, "PRACTICE_OK", 11) != 0) {
        create_main_menu();
        return;
    }

    // 3. Parsing (Cập nhật để đọc thêm Đáp án đúng)
    char *buffer_copy = strdup(buffer);
    if (!buffer_copy) { create_main_menu(); return; }
    
    char *ptr = buffer_copy;
    char *token = safe_strtok(&ptr, "|"); // PRACTICE_OK
    token = safe_strtok(&ptr, "|"); // Duration
    if (!token) { free(buffer_copy); create_main_menu(); return; }
    practice_duration = atoi(token);
    practice_start_time = time(NULL);

    practice_total_questions = 0;
    char *count_ptr = strdup(ptr);
    char *temp_cursor = count_ptr;
    while(temp_cursor && *temp_cursor) {
        char *q_data = safe_strtok(&temp_cursor, "|");
        if (q_data && strlen(q_data) > 0) practice_total_questions++;
    }
    free(count_ptr);

    if (practice_total_questions == 0) { free(buffer_copy); create_main_menu(); return; }

    practice_questions = calloc(practice_total_questions, sizeof(PracticeQuestion));
    practice_question_radios = calloc(practice_total_questions * 4, sizeof(GtkWidget*));
    practice_question_frames = calloc(practice_total_questions, sizeof(GtkWidget*));

    int q_idx = 0;
    while (ptr && q_idx < practice_total_questions) {
        char *q_data = safe_strtok(&ptr, "|");
        if (!q_data) break;

        char *q_ptr = q_data;
        // Format mong đợi: id:text:A:B:C:D:DapAn(1-4)
        
        char *id_str = safe_strtok(&q_ptr, ":");
        if (!id_str) continue;
        practice_questions[q_idx].question_id = atoi(id_str);

        char *text = safe_strtok(&q_ptr, ":");
        if (!text) continue;
        strncpy(practice_questions[q_idx].text, text, sizeof(practice_questions[q_idx].text)-1);

        for(int i=0; i<4; i++) {
            char *opt = safe_strtok(&q_ptr, ":");
            if (opt) strncpy(practice_questions[q_idx].options[i], opt, sizeof(practice_questions[q_idx].options[i])-1);
            else strcpy(practice_questions[q_idx].options[i], "???");
        }

        // --- ĐỌC ĐÁP ÁN ĐÚNG ---
        char *correct_str = safe_strtok(&q_ptr, ":");
        if (correct_str) {
            practice_questions[q_idx].correct_option = atoi(correct_str);
        } else {
            practice_questions[q_idx].correct_option = 0; // Không có dữ liệu
        }

        q_idx++;
    }
    practice_total_questions = q_idx;
    free(buffer_copy);

    // 4. UI Rendering (Giữ nguyên)
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='20000' weight='bold'>PRACTICE MODE</span>");
    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);

    practice_timer_label = gtk_label_new(NULL);
    gtk_box_pack_end(GTK_BOX(header_box), practice_timer_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);

    GtkWidget *viewport = gtk_viewport_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), viewport);

    GtkWidget *questions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(questions_box, 10);
    gtk_widget_set_margin_bottom(questions_box, 10);
    gtk_widget_set_margin_start(questions_box, 10);
    gtk_widget_set_margin_end(questions_box, 10);

    const char *labels[] = {"A", "B", "C", "D"};

    for (int i=0; i<practice_total_questions; i++) {
        GtkWidget *frame = gtk_frame_new(NULL);
        practice_question_frames[i] = frame;
        
        GtkWidget *q_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

        gtk_container_add(GTK_CONTAINER(frame), q_vbox);
        gtk_widget_set_margin_top(q_vbox, 10);
        gtk_widget_set_margin_bottom(q_vbox, 10);
        gtk_widget_set_margin_start(q_vbox, 15);
        gtk_widget_set_margin_end(q_vbox, 15);

        char q_text[700];
        snprintf(q_text, sizeof(q_text), "<b>%d. %s</b>", i+1, practice_questions[i].text);
        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_text);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_box_pack_start(GTK_BOX(q_vbox), q_label, FALSE, FALSE, 0);

        GSList *group = NULL;
        GtkWidget *dummy = gtk_radio_button_new(NULL);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(dummy));

        for (int j=0; j<4; j++) {
            char opt_text[160];
            snprintf(opt_text, sizeof(opt_text), "%s. %s", labels[j], practice_questions[i].options[j]);
            GtkWidget *radio = gtk_radio_button_new_with_label(group, opt_text);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
            g_signal_connect(radio, "toggled", G_CALLBACK(on_practice_answer_selected), GINT_TO_POINTER(i));
            gtk_box_pack_start(GTK_BOX(q_vbox), radio, FALSE, FALSE, 0);
            practice_question_radios[i*4+j] = radio;
        }
        gtk_box_pack_start(GTK_BOX(questions_box), frame, FALSE, FALSE, 0);
    }

    gtk_container_add(GTK_CONTAINER(viewport), questions_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    GtkWidget *submit_btn = gtk_button_new_with_label("SUBMIT PRACTICE");
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_practice_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), submit_btn, FALSE, FALSE, 0);

    show_view(vbox);
    is_submitting = 0;
    practice_timer_id = g_timeout_add(1000, update_practice_timer, NULL);
}

void cleanup_practice_ui() {
    if (practice_timer_id > 0) g_source_remove(practice_timer_id);
    practice_timer_id = 0;
    practice_timer_label = NULL;

    if (current_view) {
        gtk_widget_destroy(current_view);
        current_view = NULL;
    }

    if (practice_questions) { free(practice_questions); practice_questions = NULL; }
    if (practice_question_radios) { free(practice_question_radios); practice_question_radios = NULL; }
    if (practice_question_frames) { free(practice_question_frames); practice_question_frames = NULL; }

    practice_duration = 0;
    practice_total_questions = 0;
    practice_start_time = 0;
    is_submitting = 0;
}