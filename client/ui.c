#include "ui.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>
#include <gdk/gdk.h>

ClientData client;
GtkWidget *main_window;
GtkWidget *current_view;
GtkWidget *question_label;
GtkWidget *option_buttons[4];
GtkWidget *timer_label;
GtkWidget *rooms_list;
GtkWidget *status_label;
GtkWidget *selected_room_label;

int time_remaining = 0;
int test_active = 0;
int selected_room_id = -1;

typedef struct
{
    GtkWidget *user_entry;
    GtkWidget *pass_entry;
} LoginEntries;

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

static void on_room_selected(GtkListBox *listbox, GtkListBoxRow *row, gpointer user_data) {
    if (row == NULL) return;

    // Lấy ID từ data gắn vào row
    const char *id_str = g_object_get_data(G_OBJECT(row), "room_id");
    if (id_str) {
        selected_room_id = atoi(id_str);
        char status_text[128];
        snprintf(status_text, sizeof(status_text),
                 "<span foreground='#27ae60' weight='bold'>✅ Selected Room: %d</span>", selected_room_id);
        gtk_label_set_markup(GTK_LABEL(selected_room_label), status_text);
    }
}

void show_view(GtkWidget *view)
{
    if (current_view)
        gtk_container_remove(GTK_CONTAINER(main_window), current_view);
    current_view = view;
    gtk_container_add(GTK_CONTAINER(main_window), view);
    gtk_widget_show_all(main_window);
}

void style_button(GtkWidget *btn, const char *color)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    char css[256];
    snprintf(css, sizeof(css),
             "button { background-color: %s; color: black; padding: 10px; font-weight: bold; border-radius: 5px; }",
             color);
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(btn),
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void on_login_clicked(GtkWidget *widget, gpointer data)
{
    LoginEntries *entries = (LoginEntries *)data;
    const char *username = gtk_entry_get_text(GTK_ENTRY(entries->user_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(entries->pass_entry));

    if (strlen(username) == 0 || strlen(password) == 0)
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                                   "⚠️ Please enter username and password");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    char cmd[200];
    snprintf(cmd, sizeof(cmd), "LOGIN|%s|%s\n", username, password);
    send_message(cmd);

    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strstr(buffer, "LOGIN_OK"))
    {
        strncpy(client.username, username, sizeof(client.username) - 1);
        create_main_menu();
    }
    else
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "❌ Login failed");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void on_register_clicked(GtkWidget *widget, gpointer data)
{
    LoginEntries *entries = (LoginEntries *)data;
    const char *username = gtk_entry_get_text(GTK_ENTRY(entries->user_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(entries->pass_entry));

    if (strlen(username) < 3 || strlen(password) < 3)
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                                   "⚠️ Min 3 characters");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    char cmd[200];
    snprintf(cmd, sizeof(cmd), "REGISTER|%s|%s\n", username, password);
    send_message(cmd);

    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strstr(buffer, "REGISTER_OK"))
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                   "✅ Registered! Please login.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    else
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "❌ Registration failed");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void create_login_screen()
{
    if (current_view)
    {
        gtk_container_remove(GTK_CONTAINER(main_window), current_view);
        current_view = NULL;
    }

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(vbox, 40);
    gtk_widget_set_margin_start(vbox, 50);
    gtk_widget_set_margin_end(vbox, 50);
    gtk_widget_set_margin_bottom(vbox, 40);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='32768'>🎓 ONLINE QUIZ SYSTEM</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 10);

    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep1, FALSE, FALSE, 0);

    GtkWidget *username_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(username_label), "<span foreground='#34495e' weight='bold' size='12000'>📧 Username:</span>");
    GtkWidget *username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(username_entry), "Enter username");
    gtk_box_pack_start(GTK_BOX(vbox), username_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), username_entry, FALSE, FALSE, 0);

    GtkWidget *password_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(password_label), "<span foreground='#34495e' weight='bold' size='12000'>🔐 Password:</span>");
    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Enter password");
    gtk_box_pack_start(GTK_BOX(vbox), password_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), password_entry, FALSE, FALSE, 0);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 10);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    GtkWidget *login_btn = gtk_button_new_with_label("🔓 LOGIN");
    GtkWidget *register_btn = gtk_button_new_with_label("✏️ REGISTER");

    style_button(login_btn, "#3498db");
    style_button(register_btn, "#27ae60");

    LoginEntries *entries = g_malloc(sizeof(LoginEntries));
    entries->user_entry = username_entry;
    entries->pass_entry = password_entry;

    g_signal_connect(login_btn, "clicked", G_CALLBACK(on_login_clicked), entries);
    g_signal_connect(register_btn, "clicked", G_CALLBACK(on_register_clicked), entries);

    gtk_box_pack_start(GTK_BOX(button_box), login_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), register_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    show_view(vbox);
}

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
// Callback for room button click
void on_room_button_clicked(GtkWidget *widget, gpointer data)
{
    int room_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "room_id"));
    const gchar *room_name = gtk_button_get_label(GTK_BUTTON(widget));

    selected_room_id = room_id;

    char markup[256];
    snprintf(markup, sizeof(markup), "<span foreground='#27ae60' weight='bold'>Selected Room: %d (%s)</span>", room_id, room_name);
    gtk_label_set_markup(GTK_LABEL(selected_room_label), markup);
}

void on_create_room_clicked(GtkWidget *widget, gpointer data)
{
    GtkDialog *dialog = GTK_DIALOG(gtk_dialog_new_with_buttons("Create Test Room",
                                                               GTK_WINDOW(main_window),
                                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                                               "_Create", GTK_RESPONSE_OK,
                                                               "_Cancel", GTK_RESPONSE_CANCEL,
                                                               NULL));

    GtkWidget *vbox = gtk_dialog_get_content_area(dialog);
    gtk_widget_set_margin_top(vbox, 15);
    gtk_widget_set_margin_start(vbox, 15);
    gtk_widget_set_margin_end(vbox, 15);
    gtk_widget_set_margin_bottom(vbox, 15);

    GtkWidget *room_label = gtk_label_new("Room Name:");
    GtkWidget *room_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(room_entry), "e.g., Math Quiz");
    gtk_box_pack_start(GTK_BOX(vbox), room_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), room_entry, FALSE, FALSE, 5);

    GtkWidget *questions_label = gtk_label_new("Number of Questions:");
    GtkWidget *questions_spin = gtk_spin_button_new_with_range(1, 50, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(questions_spin), 10);
    gtk_box_pack_start(GTK_BOX(vbox), questions_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), questions_spin, FALSE, FALSE, 5);

    GtkWidget *time_label = gtk_label_new("Time Limit (minutes):");
    GtkWidget *time_spin = gtk_spin_button_new_with_range(1, 180, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_spin), 30);
    gtk_box_pack_start(GTK_BOX(vbox), time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), time_spin, FALSE, FALSE, 5);

    gtk_widget_show_all(GTK_WIDGET(dialog));

    if (gtk_dialog_run(dialog) == GTK_RESPONSE_OK)
    {
        const char *room_name = gtk_entry_get_text(GTK_ENTRY(room_entry));
        int num_q = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(questions_spin));
        int time_limit = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(time_spin));

        if (strlen(room_name) > 0)
        {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "CREATE_ROOM|%s|%d|%d\n", room_name, num_q, time_limit);
            send_message(cmd);

            char buffer[BUFFER_SIZE];
            ssize_t n = receive_message(buffer, sizeof(buffer));

            if (n > 0 && strstr(buffer, "CREATE_ROOM_OK"))
            {
                GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                                   "Room created!");
                gtk_dialog_run(GTK_DIALOG(success_dialog));
                gtk_widget_destroy(success_dialog);
                create_test_mode_screen();
            }
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

// Original on_join_room_clicked remains unchanged
void on_join_room_clicked(GtkWidget *widget, gpointer data)
{
    if (selected_room_id < 0)
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                                   "Select a room first!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    char cmd[100];
    snprintf(cmd, sizeof(cmd), "JOIN_ROOM|%d\n", selected_room_id);
    send_message(cmd);

    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strstr(buffer, "JOIN_ROOM_OK"))
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                   "Joined!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    else
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "Failed to join!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

gboolean on_rooms_text_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (event->type == GDK_BUTTON_PRESS && event->button == 1)
    {
        GtkTextView *text_view = GTK_TEXT_VIEW(widget);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);

        int x, y;
        gtk_text_view_window_to_buffer_coords(text_view, GTK_TEXT_WINDOW_WIDGET,
                                              event->x, event->y, &x, &y);

        GtkTextIter iter;
        gtk_text_view_get_iter_at_location(text_view, &iter, x, y);

        GtkTextIter line_start = iter;
        gtk_text_iter_set_line_offset(&line_start, 0);

        GtkTextIter line_end = iter;
        gtk_text_iter_forward_to_line_end(&line_end);

        gchar *line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);

        if (strlen(line_text) > 0 && line_text[0] != '\0')
        {
            int room_id = -1;
            sscanf(line_text, "%d|", &room_id);

            if (room_id >= 0)
            {
                selected_room_id = room_id;
                char status_text[128];
                snprintf(status_text, sizeof(status_text),
                         "<span foreground='#27ae60' weight='bold'>Selected Room: %d</span>", room_id);
                gtk_label_set_markup(GTK_LABEL(selected_room_label), status_text);
            }
        }
        g_free(line_text);
    }
    return FALSE;
}

void create_test_mode_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>TEST MODE</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label), "<span foreground='#34495e'>Click on a room to select, then join it!</span>");
    gtk_box_pack_start(GTK_BOX(vbox), info_label, FALSE, FALSE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *create_btn = gtk_button_new_with_label("CREATE ROOM");
    GtkWidget *join_btn = gtk_button_new_with_label("JOIN ROOM");
    GtkWidget *refresh_btn = gtk_button_new_with_label("REFRESH");
    GtkWidget *back_btn = gtk_button_new_with_label("⬅BACK");

    style_button(create_btn, "#27ae60");
    style_button(join_btn, "#3498db");
    style_button(refresh_btn, "#f39c12");
    style_button(back_btn, "#95a5a6");

    gtk_box_pack_start(GTK_BOX(button_box), create_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), join_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), refresh_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    selected_room_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(selected_room_label), "<span foreground='#e74c3c'>No room selected</span>");
    gtk_box_pack_start(GTK_BOX(vbox), selected_room_label, FALSE, FALSE, 0);

    // Reset selected room ID on screen creation/refresh
    selected_room_id = -1;

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);  // Không scroll ngang, scroll dọc nếu dài

    // Use flowbox to hold room buttons, set to wrap every 3 rooms for grid-like layout (dynamic, not fixed grid)
    GtkWidget *rooms_flowbox = gtk_flow_box_new();
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(rooms_flowbox), TRUE);  // Buttons đều size
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(rooms_flowbox), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(rooms_flowbox), 3);  // 3 buttons per row, wrap xuống hàng mới khi đầy
    gtk_container_add(GTK_CONTAINER(scroll), rooms_flowbox);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Send and receive LIST_ROOMS
    send_message("LIST_ROOMS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    int has_rooms = 0;

    if (n > 0 && buffer[0] != '\0') {
        // Robust parsing using strstr to handle both space-separated and newline-separated responses
        char *ptr = strstr(buffer, "ROOM|");
        while (ptr != NULL) {
            char line[BUFFER_SIZE];
            char *end = strstr(ptr + 5, "ROOM|");  // +5 to skip "ROOM|"
            if (end != NULL) {
                strncpy(line, ptr, end - ptr);
                line[end - ptr] = '\0';
            } else {
                strcpy(line, ptr);
            }

            // Parse the line
            char *token = strtok(line, "|");
            if (token != NULL && strcmp(token, "ROOM") == 0) {
                token = strtok(NULL, "|");  // id
                if (token == NULL) {
                    ptr = end;
                    continue;
                }
                int room_id = atoi(token);

                token = strtok(NULL, "|");  // name
                if (token == NULL) {
                    ptr = end;
                    continue;
                }
                char room_name[256];
                strncpy(room_name, token, sizeof(room_name) - 1);
                room_name[sizeof(room_name) - 1] = '\0';

                // Create button with name as label, attach id as data
                GtkWidget *room_btn = gtk_button_new_with_label(room_name);
                style_button(room_btn, "#ecf0f1"); // Optional: custom style for room buttons
                gtk_widget_set_size_request(room_btn, 200, 50); // Fixed width and height for each room button
                g_object_set_data(G_OBJECT(room_btn), "room_id", GINT_TO_POINTER(room_id));
                g_signal_connect(room_btn, "clicked", G_CALLBACK(on_room_button_clicked), NULL);
                gtk_flow_box_insert(GTK_FLOW_BOX(rooms_flowbox), room_btn, -1);  // Thêm vào flow, tự fill hàng còn thiếu rồi wrap

                has_rooms = 1;
            }

            ptr = end;
        }
    }

    if (!has_rooms) {
        GtkWidget *no_rooms_label = gtk_label_new("No rooms. Create one!");
        gtk_flow_box_insert(GTK_FLOW_BOX(rooms_flowbox), no_rooms_label, -1);
    }

    g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_room_clicked), NULL);
    g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_room_clicked), NULL);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(create_test_mode_screen), NULL);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);

    show_view(vbox);
}

void create_practice_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>PRACTICE MODE</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *filter_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(filter_label), "<span foreground='#34495e' weight='bold'>Select category and difficulty:</span>");
    gtk_box_pack_start(GTK_BOX(vbox), filter_label, FALSE, FALSE, 0);

    GtkWidget *filter_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget *category_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(category_label), "<span foreground='#34495e'>Category:</span>");
    GtkWidget *category_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(category_combo), "", "All");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(category_combo), "math", "Math");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(category_combo), "science", "Science");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(category_combo), "english", "English");
    gtk_combo_box_set_active(GTK_COMBO_BOX(category_combo), 0);
    gtk_box_pack_start(GTK_BOX(filter_box), category_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filter_box), category_combo, TRUE, TRUE, 0);

    GtkWidget *difficulty_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(difficulty_label), "<span foreground='#34495e'>Difficulty:</span>");
    GtkWidget *difficulty_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(difficulty_combo), "", "All");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(difficulty_combo), "easy", "Easy");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(difficulty_combo), "medium", "Medium");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(difficulty_combo), "hard", "Hard");
    gtk_combo_box_set_active(GTK_COMBO_BOX(difficulty_combo), 0);
    gtk_box_pack_start(GTK_BOX(filter_box), difficulty_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filter_box), difficulty_combo, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), filter_box, FALSE, FALSE, 0);

    GtkWidget *questions_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(questions_scroll), 350);
    GtkWidget *questions_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(questions_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(questions_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(questions_scroll), questions_view);
    gtk_box_pack_start(GTK_BOX(vbox), questions_scroll, TRUE, TRUE, 0);

    send_message("GET_PRACTICE_QUESTIONS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(questions_view));
    if (n > 0 && buffer[0] != '\0')
    {
        gtk_text_buffer_set_text(text_buffer, buffer, -1);
    }
    else
    {
        gtk_text_buffer_set_text(text_buffer, "📚 No practice questions available.", -1);
    }

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}

void create_stats_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>📊 MY STATISTICS</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *stats_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(stats_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(stats_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scroll), stats_view);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    send_message("USER_STATS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(stats_view));
    if (n > 0 && buffer[0] != '\0')
    {
        gtk_text_buffer_set_text(text_buffer, buffer, -1);
    }
    else
    {
        gtk_text_buffer_set_text(text_buffer, "📈 No statistics yet. Start testing!", -1);
    }

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}

void create_leaderboard_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span forecolor='#2c3e50' weight='bold' size='20480'>🏆 LEADERBOARD - TOP 10</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *board_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(board_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(board_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scroll), board_view);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    send_message("LEADERBOARD|10\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(board_view));
    if (n > 0 && buffer[0] != '\0')
    {
        gtk_text_buffer_set_text(text_buffer, buffer, -1);
    }
    else
    {
        gtk_text_buffer_set_text(text_buffer, "🥇 Leaderboard coming soon!", -1);
    }

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}

void create_question_bank_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>📚 QUESTION BANK</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *info = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info), "<span foreground='#27ae60' weight='bold'>✏️ Add new questions:</span>");
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 0);

    GtkWidget *form_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(form_box, 10);
    gtk_widget_set_margin_end(form_box, 10);

    GtkWidget *question_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(question_label), "<span foreground='#34495e'>Question:</span>");
    GtkWidget *question_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(question_entry), "Question text");
    gtk_box_pack_start(GTK_BOX(form_box), question_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), question_entry, FALSE, FALSE, 0);

    GtkWidget *category_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(category_label), "<span foreground='#34495e'>Category:</span>");
    GtkWidget *category_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(category_combo), "math", "Math");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(category_combo), "science", "Science");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(category_combo), "english", "English");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(category_combo), "other", "Other");
    gtk_combo_box_set_active(GTK_COMBO_BOX(category_combo), 0);
    gtk_box_pack_start(GTK_BOX(form_box), category_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), category_combo, FALSE, FALSE, 0);

    GtkWidget *difficulty_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(difficulty_label), "<span foreground='#34495e'>Difficulty:</span>");
    GtkWidget *difficulty_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(difficulty_combo), "easy", "⭐ Easy");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(difficulty_combo), "medium", "⭐⭐ Medium");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(difficulty_combo), "hard", "⭐⭐⭐ Hard");
    gtk_combo_box_set_active(GTK_COMBO_BOX(difficulty_combo), 1);
    gtk_box_pack_start(GTK_BOX(form_box), difficulty_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), difficulty_combo, FALSE, FALSE, 0);

    GtkWidget *options_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(options_label), "<span foreground='#34495e' weight='bold'>Answer Options:</span>");
    gtk_box_pack_start(GTK_BOX(form_box), options_label, FALSE, FALSE, 5);

    GtkWidget *opt1_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opt1_label), "<span foreground='#34495e'>Option 1 (Correct):</span>");
    GtkWidget *opt1_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(opt1_entry), "Correct answer");
    gtk_box_pack_start(GTK_BOX(form_box), opt1_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), opt1_entry, FALSE, FALSE, 0);

    GtkWidget *opt2_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opt2_label), "<span foreground='#34495e'>Option 2:</span>");
    GtkWidget *opt2_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(opt2_entry), "Wrong answer");
    gtk_box_pack_start(GTK_BOX(form_box), opt2_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), opt2_entry, FALSE, FALSE, 0);

    GtkWidget *opt3_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opt3_label), "<span foreground='#34495e'>Option 3:</span>");
    GtkWidget *opt3_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(opt3_entry), "Wrong answer");
    gtk_box_pack_start(GTK_BOX(form_box), opt3_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), opt3_entry, FALSE, FALSE, 0);

    GtkWidget *opt4_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opt4_label), "<span foreground='#34495e'>Option 4:</span>");
    GtkWidget *opt4_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(opt4_entry), "Wrong answer");
    gtk_box_pack_start(GTK_BOX(form_box), opt4_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), opt4_entry, FALSE, FALSE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *submit_btn = gtk_button_new_with_label("✅ ADD QUESTION");
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(submit_btn, "#27ae60");
    style_button(back_btn, "#95a5a6");

    gtk_box_pack_start(GTK_BOX(button_box), submit_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), button_box, FALSE, FALSE, 5);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), form_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}

void create_admin_panel()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>⚙️ ADMIN PANEL</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *admin_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(admin_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(admin_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scroll), admin_view);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    send_message("ADMIN_DASHBOARD\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(admin_view));
    if (n > 0 && buffer[0] != '\0')
    {
        gtk_text_buffer_set_text(text_buffer, buffer, -1);
    }
    else
    {
        gtk_text_buffer_set_text(text_buffer, "📊 Admin data loading...", -1);
    }

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}
