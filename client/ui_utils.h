#ifndef UI_UTILS_H
#define UI_UTILS_H

#include "include/client_common.h"

// UI management
void show_view(GtkWidget *view);
void style_button(GtkWidget *button, const char *hex_color);

// Dialog helpers
void show_success_dialog(const char *message);
void show_error_dialog(const char *message);
void show_info_dialog(const char *message);
void show_connection_lost_dialog(void);

// CSS setup
void setup_leaderboard_css(void);

// Timer
gboolean update_timer(gpointer data);

// External global variables needed
extern GtkWidget *current_view;
extern GtkWidget *main_window;
extern GtkWidget *timer_label;
extern GtkWidget *option_buttons[4];
extern int time_remaining;
extern int test_active;

#endif // UI_UTILS_H
