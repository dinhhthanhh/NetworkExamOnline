#include "ui_utils.h"
#include <string.h>

void show_view(GtkWidget *view)
{
    if (current_view)
        gtk_container_remove(GTK_CONTAINER(main_window), current_view);
    current_view = view;
    gtk_container_add(GTK_CONTAINER(main_window), view);
    gtk_widget_show_all(main_window);
}

void style_button(GtkWidget *button, const char *hex_color)
{
    // Sử dụng style context để đặt background color trực tiếp
    GtkStyleContext *context = gtk_widget_get_style_context(button);
    
    // Tạo CSS provider
    GtkCssProvider *provider = gtk_css_provider_new();
    char css[256];
    
    // Tạo class động dựa trên màu
    char class_name[32];
    snprintf(class_name, sizeof(class_name), "styled-button-%s", hex_color + 1);
    
    // Thêm class vào button
    gtk_style_context_add_class(context, class_name);
    
    // Tạo CSS cho class này
    snprintf(css, sizeof(css),
             ".%s { "
             "background: %s; "
             "color: white; "
             "font-weight: bold; "
             "border-radius: 6px; "
             "padding: 8px 16px; "
             "}"
             ".%s:hover { "
             "background: %s; "
             "opacity: 0.9; "
             "}",
             class_name, hex_color,
             class_name, hex_color);
    
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider(context,
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
}

void show_success_dialog(const char *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK,
                                               "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void show_error_dialog(const char *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK,
                                               "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void setup_leaderboard_css() {
    static gboolean css_loaded = FALSE;
    if (css_loaded) return;
    
    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css = 
        ".leaderboard-header {"
        "  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
        "  color: white;"
        "  font-weight: bold;"
        "  padding: 12px;"
        "  border-radius: 8px 8px 0 0;"
        "  font-size: 14px;"
        "}"
        ".leaderboard-row {"
        "  padding: 10px 12px;"
        "  border-bottom: 1px solid #e0e0e0;"
        "}"
        ".leaderboard-row:hover {"
        "  background-color: #f5f5f5;"
        "}"
        ".rank-1 {"
        "  background: linear-gradient(135deg, #FFD700 0%, #FFC400 100%);"
        "  color: #000;"
        "  font-weight: bold;"
        "}"
        ".rank-2 {"
        "  background: linear-gradient(135deg, #C0C0C0 0%, #A8A8A8 100%);"
        "  color: #000;"
        "  font-weight: bold;"
        "}"
        ".rank-3 {"
        "  background: linear-gradient(135deg, #CD7F32 0%, #B87333 100%);"
        "  color: #000;"
        "  font-weight: bold;"
        "}"
        ".score-cell {"
        "  color: #27ae60;"
        "  font-weight: bold;"
        "}"
        ".tests-cell {"
        "  color: #3498db;"
        "}"
        ".username-cell {"
        "  color: #2c3e50;"
        "  font-weight: 500;"
        "}";
    
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    css_loaded = TRUE;
}

gboolean update_timer(gpointer data)
{
    if (test_active && time_remaining > 0)
    {
        time_remaining--;
        int mins = time_remaining / 60;
        int secs = time_remaining % 60;
        char time_str[50];
        snprintf(time_str, sizeof(time_str), "⏱️ Time: %02d:%02d", mins, secs);
        gtk_label_set_text(GTK_LABEL(timer_label), time_str);
        if (time_remaining == 0)
        {
            test_active = 0;
            for (int i = 0; i < 4; i++)
                gtk_widget_set_sensitive(option_buttons[i], FALSE);
        }
        return TRUE;
    }
    return FALSE;
}
