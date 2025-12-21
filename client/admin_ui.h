#ifndef ADMIN_UI_H
#define ADMIN_UI_H

#include "include/client_common.h"

// Question form data structure
typedef struct {
    GtkWidget *room_combo;
    GtkWidget *question_entry;
    GtkWidget *opt1_entry;
    GtkWidget *opt2_entry;
    GtkWidget *opt3_entry;
    GtkWidget *opt4_entry;
    GtkWidget *difficulty_combo;
    GtkWidget *category_combo;
    GtkWidget *correct_radio[4];
} QuestionFormData;

// Admin screens
void create_admin_panel(void);
void create_question_bank_screen(void);
void create_csv_import_screen(void);

// Admin operations
void on_submit_question(GtkWidget *widget, gpointer user_data);
void on_import_csv_to_room(GtkWidget *widget, gpointer user_data);
void request_user_rooms(GtkWidget *room_combo);
void on_admin_create_room_clicked(GtkWidget *widget, gpointer data);

// Room control callbacks
void on_start_room_clicked(GtkWidget *widget, gpointer data);
void on_close_room_clicked(GtkWidget *widget, gpointer data);

// External global variables
extern int current_user_id;

#endif // ADMIN_UI_H
