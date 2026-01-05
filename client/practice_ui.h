#ifndef PRACTICE_UI_H
#define PRACTICE_UI_H

#include "include/client_common.h"
#include <time.h>

// Practice room structure
typedef struct {
    int practice_id;
    char room_name[100];
    char creator_name[50];
    int time_limit;
    int show_answers;
    int is_open;
    int num_questions;
    int active_participants;
} PracticeRoomInfo;

// Practice question structure
typedef struct {
    int id;
    char text[500];
    char options[4][200];
    char difficulty[20];
    int user_answer;  // -1 = not answered
    int is_correct;   // -1 = unknown, 0 = wrong, 1 = correct
} PracticeQuestion;

// Practice session data
typedef struct {
    int practice_id;
    int session_id;
    char room_name[100];
    int time_limit;
    int show_answers;
    int num_questions;
    PracticeQuestion questions[100];
    int current_question;
    time_t start_time;
    int is_finished;
    int score;
} PracticeSession;

extern PracticeSession current_practice;

// User UI Functions
void show_practice_list_screen(void);
void show_practice_room_screen(void);
void show_practice_results_screen(void);

// Admin functions
void show_admin_practice_menu(void);
void show_create_practice_dialog(void);
void show_manage_practice_rooms(void);
void show_practice_participants(int practice_id);
void on_close_practice_clicked(GtkWidget *widget, gpointer data);
void on_open_practice_clicked(GtkWidget *widget, gpointer data);
void on_view_participants_clicked(GtkWidget *widget, gpointer data);
void on_delete_practice_clicked(GtkWidget *widget, gpointer data);
void on_create_practice_clicked(GtkWidget *widget, gpointer data);

#endif // PRACTICE_NEW_UI_H
