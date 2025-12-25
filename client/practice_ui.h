#ifndef PRACTICE_UI_H
#define PRACTICE_UI_H

#include <gtk/gtk.h>

// Tạo màn hình chế độ luyện tập
void create_practice_screen(void);

// Cleanup khi thoát practice mode
void cleanup_practice_ui(void);

// Callback functions
void on_practice_answer_selected(GtkWidget *widget, gpointer data);
void on_submit_practice_clicked(GtkWidget *widget, gpointer data);

#endif // PRACTICE_UI_H