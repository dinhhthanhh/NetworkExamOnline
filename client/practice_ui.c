#include "practice_ui.h"
#include "net.h"
#include "ui.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
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

typedef struct {
    int question_id;
    char text[512];
    char options[4][128];
} PracticeQuestion;

static PracticeQuestion *practice_questions = NULL;

// Timer callback
static gboolean update_practice_timer(gpointer data) {
    if (!practice_start_time) return FALSE;

    time_t now = time(NULL);
    long elapsed = now - practice_start_time;
    long remaining = (practice_duration * 60) - elapsed;

    if (remaining <= 0) {
        gtk_label_set_markup(GTK_LABEL(practice_timer_label),
                            "<span foreground='red' size='16000' weight='bold'>TIME'S UP!</span>");
        if (practice_timer_id > 0) {
            g_source_remove(practice_timer_id);
            practice_timer_id = 0;
        }
        // Auto-submit
        send_message("SUBMIT_PRACTICE\n");
        char buffer[BUFFER_SIZE];
        ssize_t n = receive_message(buffer, sizeof(buffer));

        if (n > 0 && strncmp(buffer, "PRACTICE_RESULT", 15) == 0) {
            strtok(buffer, "|");
            int score = atoi(strtok(NULL, "|"));
            int total = atoi(strtok(NULL, "|"));
            int time_taken = atoi(strtok(NULL, "|"));

            cleanup_practice_ui();

            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_INFO,
                                                       GTK_BUTTONS_OK,
                                                       "Time's Up! Practice Auto-Submitted!");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                     "Score: %d/%d (%.1f%%)\nTime: %d minutes",
                                                     score, total, (score * 100.0)/total, time_taken);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            create_main_menu();
        } else {
            cleanup_practice_ui();
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       "Auto-submit failed: %s", buffer);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            create_main_menu();
        }

        return FALSE;
    }

    int minutes = remaining / 60;
    int seconds = remaining % 60;
    char timer_text[128];
    const char *color = (remaining < 300) ? "red" : "#27ae60";
    snprintf(timer_text, sizeof(timer_text),
             "<span foreground='%s' size='16000' weight='bold'>⏱ %02d:%02d</span>",
             color, minutes, seconds);
    gtk_label_set_markup(GTK_LABEL(practice_timer_label), timer_text);

    return TRUE;
}

// Callback khi chọn đáp án
static void on_practice_answer_selected(GtkWidget *widget, gpointer data) {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

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

    char msg[128];
    snprintf(msg, sizeof(msg), "SAVE_PRACTICE_ANSWER|%d|%d\n", question_id, selected + 1);
    send_message(msg);
}

// Callback submit
static void on_submit_practice_clicked(GtkWidget *widget, gpointer data) {
    if (practice_timer_id > 0) {
        g_source_remove(practice_timer_id);
        practice_timer_id = 0;
    }

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

    send_message("SUBMIT_PRACTICE\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strncmp(buffer, "PRACTICE_RESULT", 15) == 0) {
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
                                                 "Score: %d/%d (%.1f%%)\nTime: %d minutes\n\nKeep practicing!",
                                                 score, total, (score*100.0)/total, time_taken);
        gtk_dialog_run(GTK_DIALOG(result_dialog));
        gtk_widget_destroy(result_dialog);
        create_main_menu();
    } else {
        cleanup_practice_ui();
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         "Submit failed: %s", buffer);
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        create_main_menu();
    }
}

// Tạo màn hình practice
void create_practice_screen() {
    // --- Request room list ---
    flush_socket_buffer(sock_fd);
    send_message("LIST_ROOMS\n");
    char rooms_buf[BUFFER_SIZE*2];
    ssize_t rn = receive_message(rooms_buf, sizeof(rooms_buf));

    // --- Selection dialog ---
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

    // --- BEGIN_PRACTICE ---
    char begin_cmd[64];
    if (strcmp(room_id_str, "-1") == 0)
        snprintf(begin_cmd, sizeof(begin_cmd), "BEGIN_PRACTICE\n");
    else
        snprintf(begin_cmd, sizeof(begin_cmd), "BEGIN_PRACTICE|%s\n", room_id_str);

    send_message(begin_cmd);
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n <= 0 || strncmp(buffer, "PRACTICE_OK", 11) != 0) {
        GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK,
                                               "Cannot start practice");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        create_main_menu();
        return;
    }

    char *original = strdup(buffer);
    char *ptr = original;
    strtok(ptr, "|");  // skip PRACTICE_OK
    practice_duration = atoi(strtok(NULL, "|"));
    practice_start_time = time(NULL);

    // Count questions
    practice_total_questions = 0;
    while (strtok(NULL, "|") != NULL) practice_total_questions++;

    if (practice_total_questions == 0) { free(original); create_main_menu(); return; }

    practice_questions = malloc(sizeof(PracticeQuestion) * practice_total_questions);
    practice_question_radios = malloc(sizeof(GtkWidget*) * practice_total_questions * 4);

    // Parse questions
    ptr = original; strtok(ptr, "|"); strtok(NULL, "|");  // skip PRACTICE_OK|duration
    int q_idx = 0; char *q_token;
    while ((q_token = strtok(NULL, "|")) != NULL && q_idx < practice_total_questions) {
        char *q_ptr = q_token;
        char *id_str = strsep(&q_ptr, ":"); if (!id_str || !q_ptr) continue;
        practice_questions[q_idx].question_id = atoi(id_str);

        char *text = strsep(&q_ptr, ":"); if (!text || !q_ptr) continue;
        strncpy(practice_questions[q_idx].text, text, sizeof(practice_questions[q_idx].text)-1);

        for (int i=0;i<4;i++) {
            char *opt = strsep(&q_ptr, ":");
            if (!opt) strcpy(practice_questions[q_idx].options[i], "???");
            else strncpy(practice_questions[q_idx].options[i], opt, sizeof(practice_questions[q_idx].options[i])-1);
        }
        q_idx++;
    }
    practice_total_questions = q_idx;
    free(original);

    // --- Build UI ---
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='20000' weight='bold'>PRACTICE MODE</span>");
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

    practice_timer_label = gtk_label_new(NULL);
    gtk_box_pack_end(GTK_BOX(header), practice_timer_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);
    GtkWidget *viewport = gtk_viewport_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), viewport);
    GtkWidget *questions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_add(GTK_CONTAINER(viewport), questions_box);

    for (int i=0;i<practice_total_questions;i++) {
        GtkWidget *q_frame = gtk_frame_new(NULL);
        GtkWidget *q_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_add(GTK_CONTAINER(q_frame), q_vbox);

        char q_text[600];
        snprintf(q_text, sizeof(q_text), "<b>%d. %s</b>", i+1, practice_questions[i].text);
        GtkWidget *q_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(q_label), q_text);
        gtk_label_set_xalign(GTK_LABEL(q_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(q_label), TRUE);
        gtk_box_pack_start(GTK_BOX(q_vbox), q_label, FALSE, FALSE, 0);

        GSList *group = NULL;
        GtkWidget *dummy = gtk_radio_button_new(NULL);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(dummy));
        for (int j=0;j<4;j++) {
            char opt_text[150];
            snprintf(opt_text, sizeof(opt_text), "%c. %s", 'A'+j, practice_questions[i].options[j]);
            GtkWidget *radio = gtk_radio_button_new_with_label(group, opt_text);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
            g_signal_connect(radio, "toggled", G_CALLBACK(on_practice_answer_selected), GINT_TO_POINTER(i));
            gtk_box_pack_start(GTK_BOX(q_vbox), radio, FALSE, FALSE, 0);
            practice_question_radios[i*4+j] = radio;
        }
        gtk_box_pack_start(GTK_BOX(questions_box), q_frame, FALSE, FALSE, 0);
    }

    GtkWidget *submit_btn = gtk_button_new_with_label("SUBMIT PRACTICE");
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_practice_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), submit_btn, FALSE, FALSE, 0);

    GtkWidget *old = gtk_bin_get_child(GTK_BIN(main_window));
    if (old) gtk_container_remove(GTK_CONTAINER(main_window), old);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);
    gtk_widget_show_all(main_window);

    practice_timer_id = g_timeout_add(1000, update_practice_timer, NULL);
}

// Cleanup
void cleanup_practice_ui() {
    if (practice_timer_id>0) g_source_remove(practice_timer_id);
    practice_timer_id = 0;

    GtkWidget *old = gtk_bin_get_child(GTK_BIN(main_window));
    if (old) gtk_container_remove(GTK_CONTAINER(main_window), old);

    if (practice_questions) { free(practice_questions); practice_questions=NULL; }
    if (practice_question_radios) { free(practice_question_radios); practice_question_radios=NULL; }

    practice_duration=0; practice_total_questions=0; practice_start_time=0;
}