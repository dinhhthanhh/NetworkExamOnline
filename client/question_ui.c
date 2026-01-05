#include "question_ui.h"
#include "ui.h"
#include "ui_utils.h"
#include "net.h"
#include "client_common.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

// Forward declarations
extern void show_manage_practice_rooms(void);

// Global variables cho UI state
static int current_room_id = -1;
static int current_practice_id = -1;

// ==================== EXAM QUESTION MANAGEMENT ====================

// Show exam question manager screen
void show_exam_question_manager(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    current_room_id = room_id;
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    char title_text[128];
    snprintf(title_text, sizeof(title_text), 
             "<span foreground='#2c3e50' weight='bold' size='20480'>📝 Exam Room Questions (ID: %d)</span>", 
             room_id);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), title_text);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    // Button box for actions
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *add_btn = gtk_button_new_with_label("➕ Add Question Manually");
    style_button(add_btn, "#27ae60");
    gtk_box_pack_start(GTK_BOX(button_box), add_btn, TRUE, TRUE, 0);
    g_signal_connect(add_btn, "clicked", G_CALLBACK(show_add_exam_question_dialog), GINT_TO_POINTER(room_id));
    
    GtkWidget *import_btn = gtk_button_new_with_label("📂 Import from CSV");
    style_button(import_btn, "#3498db");
    gtk_box_pack_start(GTK_BOX(button_box), import_btn, TRUE, TRUE, 0);
    g_signal_connect(import_btn, "clicked", G_CALLBACK(show_import_exam_csv_dialog), GINT_TO_POINTER(room_id));
    
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    // Flush old socket data to prevent stale responses
    flush_socket_buffer(client.socket_fd);
    
    // Request questions from server
    char msg[64];
    snprintf(msg, sizeof(msg), "GET_ROOM_QUESTIONS|%d\n", room_id);
    send_message(msg);
    
    // Use larger buffer for question lists
    char *buffer = malloc(BUFFER_SIZE * 16);  // Increased for large datasets
    if (!buffer) {
        show_error_dialog("Memory allocation failed!");
        return;
    }
    
    // Use complete message receiver to handle TCP fragmentation
    ssize_t n = receive_complete_message(buffer, BUFFER_SIZE * 16, 50);
    if (n <= 0) {
        show_error_dialog("Failed to receive question list");
        free(buffer);
        return;
    }

    // Scrolled window for questions list
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Parse questions
    if (strncmp(buffer, "ROOM_QUESTIONS_LIST", 19) == 0) {
        // Create a copy for strtok since it modifies the string
        char *buffer_copy = strdup(buffer);
        if (!buffer_copy) {
            free(buffer);
            show_error_dialog("Memory error");
            return;
        }
        
        char *saveptr1;
        char *token = strtok_r(buffer_copy, "|", &saveptr1);
        token = strtok_r(NULL, "|", &saveptr1); // room_id
        
        int q_count = 0;
        while ((token = strtok_r(NULL, "|", &saveptr1)) != NULL) {
            // Parse: qid:text:optA:optB:optC:optD:correct:difficulty:category
            char *q_data = strdup(token);
            if (!q_data) continue;
            
            char *saveptr2;
            char *qid_str = strtok_r(q_data, ":", &saveptr2);
            if (!qid_str) { free(q_data); continue; }
            int qid = atoi(qid_str);
            
            char *text = strtok_r(NULL, ":", &saveptr2);
            char *optA = strtok_r(NULL, ":", &saveptr2);
            char *optB = strtok_r(NULL, ":", &saveptr2);
            char *optC = strtok_r(NULL, ":", &saveptr2);
            char *optD = strtok_r(NULL, ":", &saveptr2);
            char *correct_str = strtok_r(NULL, ":", &saveptr2);
            char *difficulty = strtok_r(NULL, ":", &saveptr2);
            char *category = strtok_r(NULL, ":", &saveptr2);
            
            // Validate all fields exist
            if (!text || !optA || !optB || !optC || !optD || !correct_str || !difficulty || !category) {
                free(q_data);
                continue;
            }
            
            // Create question row
            GtkWidget *row = gtk_list_box_row_new();
            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_widget_set_margin_top(hbox, 10);
            gtk_widget_set_margin_bottom(hbox, 10);
            gtk_widget_set_margin_start(hbox, 15);
            gtk_widget_set_margin_end(hbox, 15);
            gtk_container_add(GTK_CONTAINER(row), hbox);

            // Question info
            char info_text[1024];
            char correct_letter = 'A' + atoi(correct_str);
            snprintf(info_text, sizeof(info_text),
                    "<b>Q%d:</b> %s\n"
                    "<span size='small'>A: %s | B: %s | C: %s | D: %s\n"
                    "✔️ Correct: <span foreground='green'><b>%c</b></span> | "
                    "📊 %s | 📁 %s</span>",
                    q_count + 1, text, optA, optB, optC, optD, 
                    correct_letter, difficulty, category);
            
            GtkWidget *info_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(info_label), info_text);
            gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
            gtk_label_set_xalign(GTK_LABEL(info_label), 0);
            gtk_box_pack_start(GTK_BOX(hbox), info_label, TRUE, TRUE, 0);

            // Edit button
            GtkWidget *edit_btn = gtk_button_new_with_label("✏️ Edit");
            style_button(edit_btn, "#f39c12");
            gtk_widget_set_size_request(edit_btn, 100, 35);
            g_signal_connect(edit_btn, "clicked", 
                           G_CALLBACK(show_edit_exam_question_dialog), 
                           GINT_TO_POINTER(qid));
            gtk_box_pack_end(GTK_BOX(hbox), edit_btn, FALSE, FALSE, 0);

            gtk_container_add(GTK_CONTAINER(list_box), row);
            q_count++;
            free(q_data);
        }
        
        if (q_count == 0) {
            GtkWidget *empty_label = gtk_label_new("📭 No questions in this room yet. Add some!");
            gtk_widget_set_margin_top(empty_label, 50);
            gtk_widget_set_margin_bottom(empty_label, 50);
            gtk_container_add(GTK_CONTAINER(list_box), empty_label);
        }
        
        free(buffer_copy);
    } else {
        GtkWidget *error_label = gtk_label_new("❌ Failed to load questions");
        gtk_widget_set_margin_top(error_label, 50);
        gtk_widget_set_margin_bottom(error_label, 50);
        gtk_container_add(GTK_CONTAINER(list_box), error_label);
    }

    // Back button
    GtkWidget *back_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(back_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), back_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_admin_panel), NULL);
    
    // Cleanup
    free(buffer);
    
    show_view(vbox);
}

// Add exam question manually
void show_add_exam_question_dialog(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    (void)room_id; // Will use current_room_id
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Exam Question",
                                                     GTK_WINDOW(main_window),
                                                     GTK_DIALOG_MODAL,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Add", GTK_RESPONSE_OK,
                                                     NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_set_margin_start(content, 20);
    gtk_widget_set_margin_end(content, 20);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 20);

    // Question text
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Question Text:"), FALSE, FALSE, 5);
    GtkWidget *question_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(question_entry), "Enter question text...");
    gtk_box_pack_start(GTK_BOX(content), question_entry, FALSE, FALSE, 5);

    // Options
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("\nOptions:"), FALSE, FALSE, 5);
    
    GtkWidget *optA_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(optA_entry), "Option A");
    gtk_box_pack_start(GTK_BOX(content), optA_entry, FALSE, FALSE, 3);

    GtkWidget *optB_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(optB_entry), "Option B");
    gtk_box_pack_start(GTK_BOX(content), optB_entry, FALSE, FALSE, 3);

    GtkWidget *optC_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(optC_entry), "Option C");
    gtk_box_pack_start(GTK_BOX(content), optC_entry, FALSE, FALSE, 3);

    GtkWidget *optD_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(optD_entry), "Option D");
    gtk_box_pack_start(GTK_BOX(content), optD_entry, FALSE, FALSE, 3);

    // Correct answer combo
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("\nCorrect Answer:"), FALSE, FALSE, 5);
    GtkWidget *correct_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(correct_combo), "A");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(correct_combo), "B");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(correct_combo), "C");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(correct_combo), "D");
    gtk_combo_box_set_active(GTK_COMBO_BOX(correct_combo), 0);
    gtk_box_pack_start(GTK_BOX(content), correct_combo, FALSE, FALSE, 5);

    // Difficulty combo
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Difficulty:"), FALSE, FALSE, 5);
    GtkWidget *diff_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "easy");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "medium");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "hard");
    gtk_combo_box_set_active(GTK_COMBO_BOX(diff_combo), 0);
    gtk_box_pack_start(GTK_BOX(content), diff_combo, FALSE, FALSE, 5);

    // Category entry
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Category:"), FALSE, FALSE, 5);
    GtkWidget *category_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(category_entry), "e.g., Math, Science, History...");
    gtk_box_pack_start(GTK_BOX(content), category_entry, FALSE, FALSE, 5);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *question = gtk_entry_get_text(GTK_ENTRY(question_entry));
        const char *optA = gtk_entry_get_text(GTK_ENTRY(optA_entry));
        const char *optB = gtk_entry_get_text(GTK_ENTRY(optB_entry));
        const char *optC = gtk_entry_get_text(GTK_ENTRY(optC_entry));
        const char *optD = gtk_entry_get_text(GTK_ENTRY(optD_entry));
        int correct = gtk_combo_box_get_active(GTK_COMBO_BOX(correct_combo));
        const char *difficulty = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(diff_combo));
        const char *category = gtk_entry_get_text(GTK_ENTRY(category_entry));

        if (strlen(question) == 0 || strlen(optA) == 0 || strlen(optB) == 0 ||
            strlen(optC) == 0 || strlen(optD) == 0 || strlen(category) == 0) {
            show_error_dialog("All fields are required!");
            gtk_widget_destroy(dialog);
            return;
        }

        // Send ADD_QUESTION command with user_id
        char msg[2048];
        snprintf(msg, sizeof(msg), "ADD_QUESTION|%d|%d|%s|%s|%s|%s|%s|%d|%s|%s\n",
                current_user_id, current_room_id, question, optA, optB, optC, optD, correct, difficulty, category);
        send_message(msg);

        char buffer[256];
        ssize_t n = receive_message(buffer, sizeof(buffer));

        if (n > 0 && strncmp(buffer, "QUESTION_ADDED", 14) == 0) {
            show_info_dialog("Question added successfully!");
            // Don't destroy dialog yet - will be destroyed after refresh
        } else {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Failed to add question: %s", 
                    (n > 0 && strstr(buffer, "ERROR|")) ? buffer + 6 : "Unknown error");
            show_error_dialog(error_msg);
        }
    }

    gtk_widget_destroy(dialog);
    
    // Always refresh to show current state
    show_exam_question_manager(NULL, GINT_TO_POINTER(current_room_id));
}

// Import exam questions from CSV
void show_import_exam_csv_dialog(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    (void)room_id;
    
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Import Questions from CSV",
                                                    GTK_WINDOW(main_window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Import", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    // Add CSV filter
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "CSV files (*.csv)");
    gtk_file_filter_add_pattern(filter, "*.csv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filepath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // Read file to get size
        FILE *fp = fopen(filepath, "rb");
        if (!fp) {
            show_error_dialog("Cannot open file");
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }
        
        fseek(fp, 0, SEEK_END);
        long file_size = ftell(fp);
        fclose(fp);
        
        if (file_size <= 0 || file_size > 5 * 1024 * 1024) {
            show_error_dialog("File size invalid (max 5MB)");
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }
        
        // Extract filename only
        char *filename = strrchr(filepath, '/');
        if (!filename) filename = filepath;
        else filename++;
        
        // Flush old socket data before sending CSV command
        flush_socket_buffer(client.socket_fd);
        
        // Send IMPORT_CSV command with file_size
        char header[512];
        snprintf(header, sizeof(header), "IMPORT_CSV|%d|%s|%ld\n", 
                 current_room_id, filename, file_size);
        send_message(header);

        // Wait for READY
        char ack_buffer[64];
        ssize_t ack_n = receive_message(ack_buffer, sizeof(ack_buffer));
        if (ack_n <= 0 || strncmp(ack_buffer, "READY", 5) != 0) {
            show_error_dialog("Server not ready");
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }
        
        // Read and send file content
        fp = fopen(filepath, "rb");
        if (!fp) {
            show_error_dialog("Cannot read file");
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }
        
        char *file_buffer = malloc(file_size);
        if (!file_buffer) {
            show_error_dialog("Memory allocation failed");
            fclose(fp);
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }
        
        size_t read_bytes = fread(file_buffer, 1, file_size, fp);
        fclose(fp);
        
        if (read_bytes != (size_t)file_size) {
            show_error_dialog("Failed to read file completely");
            free(file_buffer);
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }
        
        // Send file content
        ssize_t sent = send(client.socket_fd, file_buffer, file_size, 0);
        free(file_buffer);
        
        if (sent != file_size) {
            show_error_dialog("Failed to upload file");
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }

        char buffer[512];
        ssize_t resp_n = receive_message(buffer, sizeof(buffer));

        if (resp_n > 0 && (strncmp(buffer, "IMPORT_CSV_OK", 13) == 0 || strncmp(buffer, "IMPORT_OK", 9) == 0)) {
            char *token = strtok(buffer, "|");
            token = strtok(NULL, "|");
            int count = token ? atoi(token) : 0;
            
            char success_msg[128];
            snprintf(success_msg, sizeof(success_msg), 
                    "Successfully imported %d questions!", count);
            show_info_dialog(success_msg);
            
            // Add small delay before refresh
            g_usleep(100000); // 100ms
        } else {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Failed to import CSV: %s", 
                    (resp_n > 0) ? buffer : "No response");
            show_error_dialog(error_msg);
        }

        g_free(filepath);
    }

    gtk_widget_destroy(dialog);
    
    // Always refresh after CSV operation
    show_exam_question_manager(NULL, GINT_TO_POINTER(current_room_id));
}

// Edit exam question dialog
void show_edit_exam_question_dialog(GtkWidget *widget, gpointer data) {
    int question_id = GPOINTER_TO_INT(data);
    int room_id = current_room_id;
    
    // First, fetch the question details from server
    char fetch_msg[128];
    snprintf(fetch_msg, sizeof(fetch_msg), "GET_QUESTION_DETAIL|%d|%d\n", room_id, question_id);
    send_message(fetch_msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    if (n <= 0 || strncmp(buffer, "QUESTION_DETAIL|", 16) != 0) {
        show_error_dialog("Failed to fetch question details");
        return;
    }
    
    // Parse: QUESTION_DETAIL|qid|text|optA|optB|optC|optD|correct|difficulty|category
    char *saveptr;
    strtok_r(buffer, "|", &saveptr); // Skip "QUESTION_DETAIL"
    strtok_r(NULL, "|", &saveptr); // Skip qid
    char *old_text = strtok_r(NULL, "|", &saveptr);
    char *old_optA = strtok_r(NULL, "|", &saveptr);
    char *old_optB = strtok_r(NULL, "|", &saveptr);
    char *old_optC = strtok_r(NULL, "|", &saveptr);
    char *old_optD = strtok_r(NULL, "|", &saveptr);
    char *old_correct = strtok_r(NULL, "|", &saveptr);
    char *old_difficulty = strtok_r(NULL, "|", &saveptr);
    char *old_category = strtok_r(NULL, "|", &saveptr);
    
    if (!old_text || !old_optA || !old_optB || !old_optC || !old_optD || 
        !old_correct || !old_difficulty || !old_category) {
        show_error_dialog("Invalid question data");
        return;
    }
    
    // Create edit dialog
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Edit Exam Question",
                                                     GTK_WINDOW(main_window),
                                                     GTK_DIALOG_MODAL,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Save", GTK_RESPONSE_OK,
                                                     NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 550);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_set_margin_start(content, 20);
    gtk_widget_set_margin_end(content, 20);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 20);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content), vbox);

    // Question text
    GtkWidget *q_label = gtk_label_new("Question:");
    gtk_label_set_xalign(GTK_LABEL(q_label), 0);
    GtkWidget *q_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(q_entry), old_text);
    gtk_box_pack_start(GTK_BOX(vbox), q_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), q_entry, FALSE, FALSE, 0);

    // Options
    GtkWidget *opt_label[4];
    GtkWidget *opt_entry[4];
    const char *opt_names[] = {"Option A:", "Option B:", "Option C:", "Option D:"};
    const char *old_opts[] = {old_optA, old_optB, old_optC, old_optD};
    
    for (int i = 0; i < 4; i++) {
        opt_label[i] = gtk_label_new(opt_names[i]);
        gtk_label_set_xalign(GTK_LABEL(opt_label[i]), 0);
        opt_entry[i] = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(opt_entry[i]), old_opts[i]);
        gtk_box_pack_start(GTK_BOX(vbox), opt_label[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), opt_entry[i], FALSE, FALSE, 0);
    }

    // Correct answer radio buttons
    GtkWidget *correct_label = gtk_label_new("Correct Answer:");
    gtk_label_set_xalign(GTK_LABEL(correct_label), 0);
    gtk_box_pack_start(GTK_BOX(vbox), correct_label, FALSE, FALSE, 5);
    
    GtkWidget *radio_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *correct_radio[4];
    for (int i = 0; i < 4; i++) {
        char label[16];
        snprintf(label, sizeof(label), "%c", 'A' + i);
        if (i == 0) {
            correct_radio[i] = gtk_radio_button_new_with_label(NULL, label);
        } else {
            correct_radio[i] = gtk_radio_button_new_with_label_from_widget(
                GTK_RADIO_BUTTON(correct_radio[0]), label);
        }
        gtk_box_pack_start(GTK_BOX(radio_box), correct_radio[i], FALSE, FALSE, 0);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(correct_radio[atoi(old_correct)]), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), radio_box, FALSE, FALSE, 0);

    // Difficulty
    GtkWidget *diff_label = gtk_label_new("Difficulty:");
    gtk_label_set_xalign(GTK_LABEL(diff_label), 0);
    GtkWidget *diff_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "easy");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "medium");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "hard");
    
    // Set active based on old value
    if (strcasecmp(old_difficulty, "easy") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(diff_combo), 0);
    else if (strcasecmp(old_difficulty, "medium") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(diff_combo), 1);
    else gtk_combo_box_set_active(GTK_COMBO_BOX(diff_combo), 2);
    
    gtk_box_pack_start(GTK_BOX(vbox), diff_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), diff_combo, FALSE, FALSE, 0);

    // Category
    GtkWidget *cat_label = gtk_label_new("Category:");
    gtk_label_set_xalign(GTK_LABEL(cat_label), 0);
    GtkWidget *cat_combo = gtk_combo_box_text_new();
    const char *categories[] = {"Math", "Science", "Geography", "History", "Literature", "Art", "Other"};
    for (int i = 0; i < 7; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cat_combo), categories[i]);
        if (strcasecmp(old_category, categories[i]) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(cat_combo), i);
        }
    }
    gtk_box_pack_start(GTK_BOX(vbox), cat_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), cat_combo, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        // Get new values
        const char *new_text = gtk_entry_get_text(GTK_ENTRY(q_entry));
        const char *new_optA = gtk_entry_get_text(GTK_ENTRY(opt_entry[0]));
        const char *new_optB = gtk_entry_get_text(GTK_ENTRY(opt_entry[1]));
        const char *new_optC = gtk_entry_get_text(GTK_ENTRY(opt_entry[2]));
        const char *new_optD = gtk_entry_get_text(GTK_ENTRY(opt_entry[3]));
        
        int new_correct = 0;
        for (int i = 0; i < 4; i++) {
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(correct_radio[i]))) {
                new_correct = i;
                break;
            }
        }
        
        const char *new_difficulty = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(diff_combo));
        const char *new_category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cat_combo));
        
        // Send update to server
        char update_msg[2048];
        snprintf(update_msg, sizeof(update_msg),
                 "UPDATE_EXAM_QUESTION|%d|%d|%s|%s|%s|%s|%s|%d|%s|%s\n",
                 room_id, question_id, new_text, new_optA, new_optB, new_optC, new_optD,
                 new_correct, new_difficulty, new_category);
        send_message(update_msg);

        char response[256];
        receive_message(response, sizeof(response));

        if (strncmp(response, "UPDATE_QUESTION_OK", 18) == 0) {
            show_info_dialog("Question updated successfully!");
            gtk_widget_destroy(dialog);
            show_exam_question_manager(NULL, GINT_TO_POINTER(current_room_id)); // Refresh
        } else {
            show_error_dialog("Failed to update question");
        }
    }

    gtk_widget_destroy(dialog);
}

// ==================== PRACTICE QUESTION MANAGEMENT ====================

// Show practice question manager screen
void show_practice_question_manager(GtkWidget *widget, gpointer data) {
    int practice_id = GPOINTER_TO_INT(data);
    current_practice_id = practice_id;
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    char title_text[128];
    snprintf(title_text, sizeof(title_text), 
             "<span foreground='#9b59b6' weight='bold' size='20480'>🎯 Practice Room Questions (ID: %d)</span>", 
             practice_id);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), title_text);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    // Button box for actions
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *add_btn = gtk_button_new_with_label("➕ Add Question Manually");
    style_button(add_btn, "#27ae60");
    gtk_box_pack_start(GTK_BOX(button_box), add_btn, TRUE, TRUE, 0);
    g_signal_connect(add_btn, "clicked", G_CALLBACK(show_add_practice_question_dialog), GINT_TO_POINTER(practice_id));
    
    GtkWidget *import_btn = gtk_button_new_with_label("📂 Import from CSV");
    style_button(import_btn, "#3498db");
    gtk_box_pack_start(GTK_BOX(button_box), import_btn, TRUE, TRUE, 0);
    g_signal_connect(import_btn, "clicked", G_CALLBACK(show_import_practice_csv_dialog), GINT_TO_POINTER(practice_id));
    
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    // Request questions from server
    char msg[64];
    snprintf(msg, sizeof(msg), "GET_PRACTICE_QUESTIONS|%d\n", practice_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE * 4];
    receive_message(buffer, sizeof(buffer));

    // Scrolled window for questions list
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Parse questions (similar format to exam)
    if (strncmp(buffer, "PRACTICE_QUESTIONS_LIST", 23) == 0) {
        char *token = strtok(buffer, "|");
        token = strtok(NULL, "|"); // practice_id
        
        int q_count = 0;
        while ((token = strtok(NULL, "|")) != NULL) {
            // Parse: qid:text:optA:optB:optC:optD:correct:difficulty:category
            char *q_data = strdup(token);
            char *saveptr;
            
            int qid = atoi(strtok_r(q_data, ":", &saveptr));
            char *text = strtok_r(NULL, ":", &saveptr);
            char *optA = strtok_r(NULL, ":", &saveptr);
            char *optB = strtok_r(NULL, ":", &saveptr);
            char *optC = strtok_r(NULL, ":", &saveptr);
            char *optD = strtok_r(NULL, ":", &saveptr);
            char *correct_str = strtok_r(NULL, ":", &saveptr);
            char *difficulty = strtok_r(NULL, ":", &saveptr);
            char *category = strtok_r(NULL, ":", &saveptr);
            
            if (!text || !optA || !optB || !optC || !optD || !correct_str) {
                free(q_data);
                continue;
            }
            
            // Create question row
            GtkWidget *row = gtk_list_box_row_new();
            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_widget_set_margin_top(hbox, 10);
            gtk_widget_set_margin_bottom(hbox, 10);
            gtk_widget_set_margin_start(hbox, 15);
            gtk_widget_set_margin_end(hbox, 15);
            gtk_container_add(GTK_CONTAINER(row), hbox);

            // Question info
            char info_text[1024];
            char correct_letter = 'A' + atoi(correct_str);
            snprintf(info_text, sizeof(info_text),
                    "<b>Q%d:</b> %s\n"
                    "<span size='small'>A: %s | B: %s | C: %s | D: %s\n"
                    "✔️ Correct: <span foreground='green'><b>%c</b></span> | "
                    "📊 %s | 📁 %s</span>",
                    q_count + 1, text, optA, optB, optC, optD, 
                    correct_letter, difficulty ? difficulty : "N/A", category ? category : "N/A");
            
            GtkWidget *info_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(info_label), info_text);
            gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
            gtk_label_set_xalign(GTK_LABEL(info_label), 0);
            gtk_box_pack_start(GTK_BOX(hbox), info_label, TRUE, TRUE, 0);

            // Edit button
            GtkWidget *edit_btn = gtk_button_new_with_label("✏️ Edit");
            style_button(edit_btn, "#f39c12");
            gtk_widget_set_size_request(edit_btn, 100, 35);
            g_signal_connect(edit_btn, "clicked", 
                           G_CALLBACK(show_edit_practice_question_dialog), 
                           GINT_TO_POINTER(qid));
            gtk_box_pack_end(GTK_BOX(hbox), edit_btn, FALSE, FALSE, 0);

            gtk_container_add(GTK_CONTAINER(list_box), row);
            q_count++;
            free(q_data);
        }
        
        if (q_count == 0) {
            GtkWidget *empty_label = gtk_label_new("📭 No questions in this practice room yet. Add some!");
            gtk_widget_set_margin_top(empty_label, 50);
            gtk_widget_set_margin_bottom(empty_label, 50);
            gtk_container_add(GTK_CONTAINER(list_box), empty_label);
        }
    } else {
        GtkWidget *error_label = gtk_label_new("❌ Failed to load questions");
        gtk_widget_set_margin_top(error_label, 50);
        gtk_widget_set_margin_bottom(error_label, 50);
        gtk_container_add(GTK_CONTAINER(list_box), error_label);
    }

    // Back button
    GtkWidget *back_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(back_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), back_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(show_manage_practice_rooms), NULL);
    show_view(vbox);
}

void show_add_practice_question_dialog(GtkWidget *widget, gpointer data) {
    int practice_id = GPOINTER_TO_INT(data);
    (void)practice_id; // Will use current_practice_id
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Practice Question",
                                                     GTK_WINDOW(main_window),
                                                     GTK_DIALOG_MODAL,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Add", GTK_RESPONSE_OK,
                                                     NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_set_margin_start(content, 20);
    gtk_widget_set_margin_end(content, 20);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 20);

    // Question text
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Question Text:"), FALSE, FALSE, 5);
    GtkWidget *question_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(question_entry), "Enter question text...");
    gtk_box_pack_start(GTK_BOX(content), question_entry, FALSE, FALSE, 5);

    // Options
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("\nOptions:"), FALSE, FALSE, 5);
    
    GtkWidget *optA_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(optA_entry), "Option A");
    gtk_box_pack_start(GTK_BOX(content), optA_entry, FALSE, FALSE, 3);

    GtkWidget *optB_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(optB_entry), "Option B");
    gtk_box_pack_start(GTK_BOX(content), optB_entry, FALSE, FALSE, 3);

    GtkWidget *optC_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(optC_entry), "Option C");
    gtk_box_pack_start(GTK_BOX(content), optC_entry, FALSE, FALSE, 3);

    GtkWidget *optD_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(optD_entry), "Option D");
    gtk_box_pack_start(GTK_BOX(content), optD_entry, FALSE, FALSE, 3);

    // Correct answer combo
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("\nCorrect Answer:"), FALSE, FALSE, 5);
    GtkWidget *correct_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(correct_combo), "A");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(correct_combo), "B");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(correct_combo), "C");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(correct_combo), "D");
    gtk_combo_box_set_active(GTK_COMBO_BOX(correct_combo), 0);
    gtk_box_pack_start(GTK_BOX(content), correct_combo, FALSE, FALSE, 5);

    // Difficulty combo
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Difficulty:"), FALSE, FALSE, 5);
    GtkWidget *diff_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "easy");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "medium");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(diff_combo), "hard");
    gtk_combo_box_set_active(GTK_COMBO_BOX(diff_combo), 0);
    gtk_box_pack_start(GTK_BOX(content), diff_combo, FALSE, FALSE, 5);

    // Category entry
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Category:"), FALSE, FALSE, 5);
    GtkWidget *category_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(category_entry), "e.g., Math, Science, History...");
    gtk_box_pack_start(GTK_BOX(content), category_entry, FALSE, FALSE, 5);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *question = gtk_entry_get_text(GTK_ENTRY(question_entry));
        const char *optA = gtk_entry_get_text(GTK_ENTRY(optA_entry));
        const char *optB = gtk_entry_get_text(GTK_ENTRY(optB_entry));
        const char *optC = gtk_entry_get_text(GTK_ENTRY(optC_entry));
        const char *optD = gtk_entry_get_text(GTK_ENTRY(optD_entry));
        int correct = gtk_combo_box_get_active(GTK_COMBO_BOX(correct_combo));
        const char *difficulty = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(diff_combo));
        const char *category = gtk_entry_get_text(GTK_ENTRY(category_entry));

        if (strlen(question) == 0 || strlen(optA) == 0 || strlen(optB) == 0 ||
            strlen(optC) == 0 || strlen(optD) == 0 || strlen(category) == 0) {
            show_error_dialog("All fields are required!");
            gtk_widget_destroy(dialog);
            return;
        }

        // Send CREATE_PRACTICE_QUESTION command
        char msg[2048];
        snprintf(msg, sizeof(msg), "CREATE_PRACTICE_QUESTION|%d|%s|%s|%s|%s|%s|%d|%s|%s\n",
                current_practice_id, question, optA, optB, optC, optD, correct, difficulty, category);
        send_message(msg);

        char buffer[256];
        receive_message(buffer, sizeof(buffer));

        if (strncmp(buffer, "ADD_PRACTICE_QUESTION_OK", 24) == 0 || 
            strncmp(buffer, "CREATE_PRACTICE_QUESTION_OK", 27) == 0) {
            show_info_dialog("Question added successfully!");
            gtk_widget_destroy(dialog);
            show_practice_question_manager(NULL, GINT_TO_POINTER(current_practice_id)); // Refresh
        } else {
            show_error_dialog("Failed to add question");
        }
    }

    gtk_widget_destroy(dialog);
}

void show_import_practice_csv_dialog(GtkWidget *widget, gpointer data) {
    int practice_id = GPOINTER_TO_INT(data);
    (void)practice_id;
    
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Import Questions from CSV",
                                                    GTK_WINDOW(main_window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Import", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    // Add CSV filter
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "CSV files (*.csv)");
    gtk_file_filter_add_pattern(filter, "*.csv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // Send IMPORT_PRACTICE_CSV command
        char msg[512];
        snprintf(msg, sizeof(msg), "IMPORT_PRACTICE_CSV|%d|%s\n", current_practice_id, filename);
        send_message(msg);

        char buffer[256];
        receive_message(buffer, sizeof(buffer));

        if (strncmp(buffer, "IMPORT_PRACTICE_CSV_OK", 22) == 0) {
            char *token = strtok(buffer, "|");
            token = strtok(NULL, "|");
            int count = atoi(token);
            
            char success_msg[128];
            snprintf(success_msg, sizeof(success_msg), 
                    "Successfully imported %d questions!", count);
            show_info_dialog(success_msg);
            
            gtk_widget_destroy(dialog);
            show_practice_question_manager(NULL, GINT_TO_POINTER(current_practice_id)); // Refresh
        } else {
            show_error_dialog("Failed to import CSV file");
        }

        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

void show_edit_practice_question_dialog(GtkWidget *widget, gpointer data) {
    int question_id = GPOINTER_TO_INT(data);
    int practice_id = current_practice_id;
    (void)practice_id;
    
    // TODO: Implement edit dialog similar to add dialog
    // For now, show info message
    char msg[128];
    snprintf(msg, sizeof(msg), "Edit question %d (feature coming soon)", question_id);
    show_info_dialog(msg);
}
