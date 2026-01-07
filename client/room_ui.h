#ifndef ROOM_UI_H
#define ROOM_UI_H

#include "include/client_common.h"
#include <gtk/gtk.h>

// Room screens
void create_test_mode_screen(void);
void create_exam_room_with_questions(GtkWidget *widget, gpointer user_data);

// Room operations
void load_rooms_list(void);
void on_room_button_clicked(GtkWidget *button, gpointer data);
void on_join_room_clicked(GtkWidget *widget, gpointer data);
void on_create_room_clicked(GtkWidget *widget, gpointer data);

// External global variables
extern GtkWidget *rooms_list;
extern GtkWidget *selected_room_label;
extern int selected_room_id;

#endif // ROOM_UI_H
