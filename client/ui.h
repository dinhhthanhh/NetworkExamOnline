#ifndef UI_H
#define UI_H

#include "include/client_common.h"

void create_login_screen(void);
void create_main_menu(void);
void create_test_mode_screen(void);
void create_practice_screen(void);
void create_question_screen(void);
void create_stats_screen(void);
void create_leaderboard_screen(void);
void create_admin_panel(void);
void create_question_bank_screen(void);

/* Callbacks */
void on_login_clicked(GtkWidget *widget, gpointer data);
void on_register_clicked(GtkWidget *widget, gpointer data);

#endif // UI_H
