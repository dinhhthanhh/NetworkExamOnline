#include "practice_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include <glib.h>

GtkTextBuffer *g_practice_buffer = NULL;

gboolean update_practice_text(gpointer data) {
    char *text = (char *)data;
    gtk_text_buffer_set_text(g_practice_buffer, text, -1);
    g_free(text);
    return FALSE;
}

gpointer practice_thread_func(gpointer data) {
    char recv_buf[BUFFER_SIZE];
    ssize_t n = receive_message(recv_buf, sizeof(recv_buf));
    char *result;

    if (n > 0 && recv_buf[0] != '\0') {
        result = g_strdup(recv_buf);
    } else {
        result = g_strdup("📚 No practice questions available.");
    }

    g_idle_add(update_practice_text, result);
    return NULL;
}

void create_practice_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>🎯 PRACTICE MODE</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *practice_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(practice_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(practice_view), GTK_WRAP_WORD);
    g_practice_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(practice_view));
    gtk_container_add(GTK_CONTAINER(scroll), practice_view);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    gtk_text_buffer_set_text(g_practice_buffer, "⏳ Loading practice questions...", -1);

    send_message("PRACTICE_MODE\n");
    g_thread_new("practice_thread", practice_thread_func, NULL);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}
