#ifndef QUESTION_UI_H
#define QUESTION_UI_H

#include <gtk/gtk.h>

// Exam questions management
void show_exam_question_manager(GtkWidget *widget, gpointer data);
void show_add_exam_question_dialog(GtkWidget *widget, gpointer data);
void show_import_exam_csv_dialog(GtkWidget *widget, gpointer data);
void show_edit_exam_question_dialog(GtkWidget *widget, gpointer data);

// Practice questions management
void show_practice_question_manager(GtkWidget *widget, gpointer data);
void show_add_practice_question_dialog(GtkWidget *widget, gpointer data);
void show_import_practice_csv_dialog(GtkWidget *widget, gpointer data);
void show_edit_practice_question_dialog(GtkWidget *widget, gpointer data);

#endif // QUESTION_UI_H
