#include "practice_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include "room_ui.h"
#include <gtk/gtk.h>
#include <time.h>

extern GtkWidget *main_window;
extern int sock_fd;
extern int current_user_id;

// Global state cho practice
static int practice_room_id = 0;
static int practice_duration = 0;
static time_t practice_start_time = 0;
static guint practice_timer_id = 0;
static GtkWidget *practice_timer_label = NULL;
static GtkWidget **practice_radios = NULL;
static int practice_total_questions = 0;
static int practice_show_answer = 0;

typedef struct {
    int question_id;
    char text[512];
    char options[4][128];
    char difficulty[20];
} PracticeQuestion;

static PracticeQuestion *practice_questions = NULL;

// Timer callback for practice (if time limit is set)
// TODO: This will be used when practice timer is implemented
static gboolean update_practice_timer(gpointer data) __attribute__((unused));
static gboolean update_practice_timer(gpointer data) {
    if (!practice_start_time || practice_duration == 0) return FALSE;
    
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
        return FALSE;
    }
    
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    
    char timer_text[128];
    const char *color = (remaining < 300) ? "red" : "#27ae60";
    snprintf(timer_text, sizeof(timer_text),
             "<span foreground='%s' size='16000' weight='bold'>Time: %02d:%02d</span>",
             color, minutes, seconds);
    
    gtk_label_set_markup(GTK_LABEL(practice_timer_label), timer_text);
    return TRUE;
}

// Join practice room callback
void on_join_practice_room(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    printf("[PRACTICE] Joining practice room %d\n", room_id);
    
    // Use the same flow as exam but with practice logic
    create_exam_page(room_id);
}

// Callback khi chọn đáp án trong practice
void on_practice_answer_selected(GtkWidget *widget, gpointer data) {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        return;
    }
    
    int question_index = GPOINTER_TO_INT(data);
    int question_id = practice_questions[question_index].question_id;
    
    // Tìm đáp án được chọn
    int selected = -1;
    for (int i = 0; i < 4; i++) {
        GtkWidget *radio = practice_radios[question_index * 4 + i];
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio))) {
            selected = i;
            break;
        }
    }
    
    if (selected == -1) return;
    
    // Gửi SAVE_ANSWER
    char msg[128];
    snprintf(msg, sizeof(msg), "SAVE_ANSWER|%d|%d|%d\n", 
             practice_room_id, question_id, selected);
    send_message(msg);
    
    // Đọc response
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0 && strncmp(buffer, "SAVE_ANSWER_OK", 14) == 0) {
        // Check if practice mode with show_answer
        if (strchr(buffer, '|') != NULL) {
            // Parse: SAVE_ANSWER_OK|is_correct|correct_answer
            strtok(buffer, "|");
            int is_correct = atoi(strtok(NULL, "|"));
            int correct_answer = atoi(strtok(NULL, "|\n"));
            
            // Apply CSS styles to radio buttons for color feedback
            for (int i = 0; i < 4; i++) {
                GtkWidget *radio = practice_radios[question_index * 4 + i];
                GtkStyleContext *context = gtk_widget_get_style_context(radio);
                
                // Remove all previous classes
                gtk_style_context_remove_class(context, "answer-correct");
                gtk_style_context_remove_class(context, "answer-wrong");
                
                if (i == selected) {
                    if (is_correct) {
                        // Correct answer - green
                        gtk_style_context_add_class(context, "answer-correct");
                    } else {
                        // Wrong answer - red
                        gtk_style_context_add_class(context, "answer-wrong");
                    }
                } else if (i == correct_answer && !is_correct) {
                    // Show correct answer
                    gtk_style_context_add_class(context, "answer-correct");
                }
            }
            
            printf("[PRACTICE] Q%d: selected=%d, correct=%d (%s)\n", 
                   question_id, selected, correct_answer, is_correct ? "CORRECT" : "WRONG");
        }
    }
}

// Cleanup practice UI
void cleanup_practice_ui() {
    if (practice_timer_id > 0) {
        g_source_remove(practice_timer_id);
        practice_timer_id = 0;
    }
    
    if (practice_questions) {
        free(practice_questions);
        practice_questions = NULL;
    }
    
    if (practice_radios) {
        free(practice_radios);
        practice_radios = NULL;
    }
    
    practice_room_id = 0;
    practice_duration = 0;
    practice_start_time = 0;
    practice_total_questions = 0;
    practice_show_answer = 0;
}

// Display list of practice rooms
void create_practice_screen()
{
    // Setup CSS for answer feedback colors
    static gboolean css_loaded = FALSE;
    if (!css_loaded) {
        GtkCssProvider *provider = gtk_css_provider_new();
        const gchar *css = 
            ".answer-correct {"
            "  background-color: rgba(39, 174, 96, 0.3);"  /* Green */
            "  border: 2px solid #27ae60;"
            "}"
            ".answer-wrong {"
            "  background-color: rgba(231, 76, 60, 0.3);"  /* Red */
            "  border: 2px solid #e74c3c;"
            "}";
        gtk_css_provider_load_from_data(provider, css, -1, NULL);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                 GTK_STYLE_PROVIDER(provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        css_loaded = TRUE;
    }
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span foreground='#2c3e50' weight='bold' size='20480'>PRACTICE MODE - Select a Room</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 5);

    // Info
    GtkWidget *info = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info),
        "<span foreground='#27ae60' size='11000'>Practice rooms allow you to learn without pressure!\n"
        "Some rooms may show correct answers immediately.</span>");
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 5);

    // Buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *refresh_btn = gtk_button_new_with_label("REFRESH");
    GtkWidget *back_btn = gtk_button_new_with_label("BACK");
    style_button(refresh_btn, "#f39c12");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), refresh_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 5);

    // List practice rooms
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 350);
    
    GtkWidget *list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Request practice rooms (is_practice=1)
    send_message("LIST_ROOMS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0) {
        char *line = strtok(buffer, "\n");
        int room_count = 0;
        
        while (line != NULL) {
            if (strncmp(line, "ROOM|", 5) == 0) {
                // Parse: ROOM|id|name|time|status|question_status|owner|is_practice
                char *room_data = strdup(line);
                strtok(room_data, "|");
                int room_id = atoi(strtok(NULL, "|"));
                char *room_name = strtok(NULL, "|");
                strtok(NULL, "|");  // time
                char *status = strtok(NULL, "|");
                strtok(NULL, "|");  // question_status
                strtok(NULL, "|");  // owner
                int is_practice = atoi(strtok(NULL, "|"));
                
                // Skip exam rooms - only show practice rooms
                if (is_practice == 0) {
                    free(room_data);
                    line = strtok(NULL, "\n");
                    continue;
                }
                
                // Skip closed practice rooms - only show Started ones
                if (strcmp(status, "Closed") == 0) {
                    free(room_data);
                    line = strtok(NULL, "\n");
                    continue;
                }
                
                free(room_data);
                
                // Re-parse for display
                room_data = strdup(line);
                strtok(room_data, "|");
                room_id = atoi(strtok(NULL, "|"));
                room_name = strtok(NULL, "|");
                
                GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_widget_set_margin_top(row_box, 10);
                gtk_widget_set_margin_bottom(row_box, 10);
                gtk_widget_set_margin_start(row_box, 15);
                gtk_widget_set_margin_end(row_box, 15);
                
                char label_text[256];
                snprintf(label_text, sizeof(label_text), "<b>%s</b> (ID: %d)", room_name, room_id);
                GtkWidget *label = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(label), label_text);
                gtk_label_set_xalign(GTK_LABEL(label), 0);
                gtk_box_pack_start(GTK_BOX(row_box), label, TRUE, TRUE, 0);
                
                GtkWidget *join_btn = gtk_button_new_with_label("START PRACTICE");
                style_button(join_btn, "#27ae60");
                g_object_set_data(G_OBJECT(join_btn), "room_id", GINT_TO_POINTER(room_id));
                g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_practice_room), GINT_TO_POINTER(room_id));
                gtk_box_pack_end(GTK_BOX(row_box), join_btn, FALSE, FALSE, 0);
                
                GtkWidget *row = gtk_list_box_row_new();
                gtk_container_add(GTK_CONTAINER(row), row_box);
                gtk_list_box_insert(GTK_LIST_BOX(list_box), row, -1);
                room_count++;
                
                free(room_data);
            }
            line = strtok(NULL, "\n");
        }
        
        if (room_count == 0) {
            GtkWidget *empty = gtk_label_new("📭 No practice rooms available yet");
            gtk_container_add(GTK_CONTAINER(list_box), empty);
        }
    }

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}
