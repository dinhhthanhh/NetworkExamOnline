#include "ui.h"
#include "ui_utils.h"
#include "auth_ui.h"
#include "room_ui.h"
#include "stats_ui.h"
#include "admin_ui.h"
#include "practice_ui.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <cairo.h>

// Global variables
ClientData client;
GtkWidget *main_window;
GtkWidget *current_view;
GtkWidget *question_label;
GtkWidget *option_buttons[4];
GtkWidget *timer_label;
GtkWidget *rooms_list;
GtkWidget *status_label;
GtkWidget *selected_room_label;
GtkWidget *room_list; 

int time_remaining = 0;
int test_active = 0;
int selected_room_id = -1;
int current_user_id = -1;

void create_main_menu()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top(vbox, 25);
    gtk_widget_set_margin_start(vbox, 30);
    gtk_widget_set_margin_end(vbox, 30);
    gtk_widget_set_margin_bottom(vbox, 25);

    GtkWidget *welcome_label = gtk_label_new(NULL);
    gchar *welcome_text = g_strdup_printf("👤 Welcome, <b>%s</b>!",
                                          client.username[0] ? client.username : "User");
    gtk_label_set_markup(GTK_LABEL(welcome_label), welcome_text);
    g_free(welcome_text);
    gtk_box_pack_start(GTK_BOX(vbox), welcome_label, FALSE, FALSE, 5);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *test_mode_btn = gtk_button_new_with_label("📝 Test Mode - Create & Join");
    GtkWidget *practice_btn = gtk_button_new_with_label("🎯 Practice Mode");
    GtkWidget *stats_btn = gtk_button_new_with_label("📊 My Statistics");
    GtkWidget *leaderboard_btn = gtk_button_new_with_label("🏆 Leaderboard");
    GtkWidget *qbank_btn = gtk_button_new_with_label("📚 Question Bank");
    GtkWidget *admin_btn = gtk_button_new_with_label("⚙️ Admin Panel");
    GtkWidget *logout_btn = gtk_button_new_with_label("🚪 Logout");

    style_button(test_mode_btn, "#3498db");
    style_button(practice_btn, "#9b59b6");
    style_button(stats_btn, "#e74c3c");
    style_button(leaderboard_btn, "#f39c12");
    style_button(qbank_btn, "#16a085");
    style_button(admin_btn, "#8e44ad");
    style_button(logout_btn, "#95a5a6");

    gtk_box_pack_start(GTK_BOX(vbox), test_mode_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), practice_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), stats_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), leaderboard_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), qbank_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), admin_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), logout_btn, FALSE, FALSE, 20);

    g_signal_connect(test_mode_btn, "clicked", G_CALLBACK(create_test_mode_screen), NULL);
    g_signal_connect(practice_btn, "clicked", G_CALLBACK(create_practice_screen), NULL);
    g_signal_connect(stats_btn, "clicked", G_CALLBACK(create_stats_screen), NULL);
    g_signal_connect(leaderboard_btn, "clicked", G_CALLBACK(create_leaderboard_screen), NULL);
    g_signal_connect(qbank_btn, "clicked", G_CALLBACK(create_question_bank_screen), NULL);
    g_signal_connect(admin_btn, "clicked", G_CALLBACK(create_admin_panel), NULL);
    g_signal_connect(logout_btn, "clicked", G_CALLBACK(create_login_screen), NULL);

    show_view(vbox);
}
