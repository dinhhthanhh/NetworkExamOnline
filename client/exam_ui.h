#ifndef EXAM_UI_H
#define EXAM_UI_H

#include <gtk/gtk.h>

// Tạo UI exam page khi user join room
void create_exam_page(int room_id);

// Tạo UI exam page từ resume data
void create_exam_page_from_resume(int room_id, char *resume_data);

// Callback khi user chọn đáp án
void on_answer_selected(GtkWidget *widget, gpointer data);

// Callback khi submit exam
void on_submit_exam_clicked(GtkWidget *widget, gpointer data);

// Cleanup
void cleanup_exam_ui();

#endif
