#include "exam_room_creator.h"
#include "ui.h"
#include "ui_utils.h"
#include "net.h"
#include <string.h>

// Structure to hold question selection data
typedef struct {
    GtkWidget *easy_spin;
    GtkWidget *medium_spin;
    GtkWidget *hard_spin;
    GtkWidget *total_label;
} QuestionSelectionData;

// Callback when difficulty spinners change
void on_difficulty_changed(GtkSpinButton *spin_button, gpointer user_data) {
    QuestionSelectionData *qs_data = (QuestionSelectionData *)user_data;
    
    int easy = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(qs_data->easy_spin));
    int medium = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(qs_data->medium_spin));
    int hard = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(qs_data->hard_spin));
    int total = easy + medium + hard;
    
    char total_text[256];
    snprintf(total_text, sizeof(total_text), 
             "<span size='14000' weight='bold'>Total Questions: %d</span>", total);
    gtk_label_set_markup(GTK_LABEL(qs_data->total_label), total_text);
}

void create_exam_room_with_questions(GtkWidget *widget, gpointer user_data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Create Exam Room with Questions",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create Room", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 15);
    gtk_container_add(GTK_CONTAINER(content_area), main_box);
    
    // Title
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), 
        "<span size='16000' weight='bold'>Create New Exam Room</span>");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0);
    gtk_box_pack_start(GTK_BOX(main_box), title_label, FALSE, FALSE, 0);
    
    // Separator
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep1, FALSE, FALSE, 5);
    
    // Room info section
    GtkWidget *info_frame = gtk_frame_new("Basic Information");
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(info_box, 15);
    gtk_widget_set_margin_end(info_box, 15);
    gtk_widget_set_margin_top(info_box, 10);
    gtk_widget_set_margin_bottom(info_box, 10);
    
    // Room name
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *name_label = gtk_label_new("Room Name:");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0);
    gtk_widget_set_size_request(name_label, 120, -1);
    GtkWidget *room_name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(room_name_entry), "Enter exam room name");
    gtk_box_pack_start(GTK_BOX(name_box), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(name_box), room_name_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), name_box, FALSE, FALSE, 0);
    
    // Time limit
    GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *time_label = gtk_label_new("Time Limit (min):");
    gtk_label_set_xalign(GTK_LABEL(time_label), 0);
    gtk_widget_set_size_request(time_label, 120, -1);
    GtkWidget *time_spin = gtk_spin_button_new_with_range(1, 180, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_spin), 30);
    gtk_box_pack_start(GTK_BOX(time_box), time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_box), time_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_box), time_box, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(info_frame), info_box);
    gtk_box_pack_start(GTK_BOX(main_box), info_frame, FALSE, FALSE, 5);
    
    // Question selection section
    GtkWidget *question_frame = gtk_frame_new("Question Selection");
    GtkWidget *question_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(question_box, 15);
    gtk_widget_set_margin_end(question_box, 15);
    gtk_widget_set_margin_top(question_box, 10);
    gtk_widget_set_margin_bottom(question_box, 10);
    
    // Info text
    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label), 
        "<span foreground='#7f8c8d' size='10000'><i>Select number of questions for each difficulty level. "
        "Questions will be randomly selected from the question bank.</i></span>");
    gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(info_label), 0);
    gtk_box_pack_start(GTK_BOX(question_box), info_label, FALSE, FALSE, 0);
    
    // Easy questions
    GtkWidget *easy_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *easy_label = gtk_label_new("Easy Questions:");
    gtk_label_set_xalign(GTK_LABEL(easy_label), 0);
    gtk_widget_set_size_request(easy_label, 150, -1);
    GtkWidget *easy_spin = gtk_spin_button_new_with_range(0, 50, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(easy_spin), 5);
    gtk_box_pack_start(GTK_BOX(easy_box), easy_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(easy_box), easy_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(question_box), easy_box, FALSE, FALSE, 0);
    
    // Medium questions
    GtkWidget *medium_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *medium_label = gtk_label_new("Medium Questions:");
    gtk_label_set_xalign(GTK_LABEL(medium_label), 0);
    gtk_widget_set_size_request(medium_label, 150, -1);
    GtkWidget *medium_spin = gtk_spin_button_new_with_range(0, 50, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(medium_spin), 3);
    gtk_box_pack_start(GTK_BOX(medium_box), medium_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(medium_box), medium_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(question_box), medium_box, FALSE, FALSE, 0);
    
    // Hard questions
    GtkWidget *hard_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *hard_label = gtk_label_new("Hard Questions:");
    gtk_label_set_xalign(GTK_LABEL(hard_label), 0);
    gtk_widget_set_size_request(hard_label, 150, -1);
    GtkWidget *hard_spin = gtk_spin_button_new_with_range(0, 50, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(hard_spin), 2);
    gtk_box_pack_start(GTK_BOX(hard_box), hard_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hard_box), hard_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(question_box), hard_box, FALSE, FALSE, 0);
    
    // Total count display
    GtkWidget *total_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(question_box), sep2, FALSE, FALSE, 5);
    GtkWidget *total_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(total_label), 
        "<span size='14000' weight='bold'>Total Questions: 10</span>");
    gtk_label_set_xalign(GTK_LABEL(total_label), 0.5);
    gtk_box_pack_start(GTK_BOX(total_box), total_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(question_box), total_box, FALSE, FALSE, 5);
    
    gtk_container_add(GTK_CONTAINER(question_frame), question_box);
    gtk_box_pack_start(GTK_BOX(main_box), question_frame, FALSE, FALSE, 5);
    
    // Setup data structure for callbacks
    QuestionSelectionData *qs_data = g_new(QuestionSelectionData, 1);
    qs_data->easy_spin = easy_spin;
    qs_data->medium_spin = medium_spin;
    qs_data->hard_spin = hard_spin;
    qs_data->total_label = total_label;
    
    // Connect signals
    g_signal_connect(easy_spin, "value-changed", G_CALLBACK(on_difficulty_changed), qs_data);
    g_signal_connect(medium_spin, "value-changed", G_CALLBACK(on_difficulty_changed), qs_data);
    g_signal_connect(hard_spin, "value-changed", G_CALLBACK(on_difficulty_changed), qs_data);
    
    gtk_widget_show_all(dialog);
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_ACCEPT) {
        const char *room_name = gtk_entry_get_text(GTK_ENTRY(room_name_entry));
        int time_limit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(time_spin));
        int easy_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(easy_spin));
        int medium_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(medium_spin));
        int hard_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(hard_spin));
        
        if (strlen(room_name) == 0) {
            show_error_dialog("Room name cannot be empty!");
        } else if (easy_count + medium_count + hard_count == 0) {
            show_error_dialog("Please select at least one question!");
        } else {
            // Send create room command with question counts
            char msg[512];
            snprintf(msg, sizeof(msg), "CREATE_ROOM|%s|%d|%d|%d|%d\n",
                     room_name, time_limit, easy_count, medium_count, hard_count);
            send_message(msg);
            
            char recv_buf[BUFFER_SIZE];
            ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
            
            if (n > 0 && strncmp(recv_buf, "CREATE_ROOM_OK|", 15) == 0) {
                char success_msg[1024];
                snprintf(success_msg, sizeof(success_msg),
                    "Exam room '%s' created successfully!\n\n"
                    "Questions: %d Easy + %d Medium + %d Hard = %d Total\n"
                    "Time Limit: %d minutes\n\n"
                    "Questions have been randomly selected from the question bank.",
                    room_name, easy_count, medium_count, hard_count, 
                    easy_count + medium_count + hard_count, time_limit);
                show_info_dialog(success_msg);
                
                // Automatically show admin panel to display the new room
                create_admin_panel();
            } else if (n > 0 && strncmp(recv_buf, "CREATE_ROOM_FAIL|", 17) == 0) {
                char *error = recv_buf + 17;
                show_error_dialog(error);
            } else {
                show_error_dialog("Failed to create exam room. Please try again.");
            }
        }
    }
    
    g_free(qs_data);
    gtk_widget_destroy(dialog);
}
