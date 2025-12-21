#ifndef UI_H
#define UI_H

#include "include/client_common.h"

// Main menu
void create_main_menu(void);

// Global variables (extern declarations)
extern ClientData client;
extern GtkWidget *main_window;
extern GtkWidget *current_view;
extern GtkWidget *question_label;
extern GtkWidget *option_buttons[4];
extern GtkWidget *timer_label;
extern GtkWidget *rooms_list;
extern GtkWidget *status_label;
extern GtkWidget *selected_room_label;
extern GtkTextBuffer *g_practice_buffer;
extern GtkWidget *room_list;
extern int time_remaining;
extern int test_active;
extern int selected_room_id;
extern int current_user_id;

// Include các module screens
void create_login_screen(void);
void create_test_mode_screen(void);
void create_practice_screen(void);
void create_stats_screen(void);
void create_leaderboard_screen(void);
void create_admin_panel(void);
void create_question_bank_screen(void);

#endif // UI_H
