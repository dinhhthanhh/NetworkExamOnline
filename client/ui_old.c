#include "ui.h"
#include "net.h"
#include "exam_ui.h"
#include <string.h>
#include <stdlib.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <cairo.h>

ClientData client;
GtkWidget *main_window;
GtkWidget *current_view;
GtkWidget *question_label;
GtkWidget *option_buttons[4];
GtkWidget *timer_label;
GtkWidget *rooms_list;
GtkWidget *status_label;
GtkWidget *selected_room_label;
GtkTextBuffer *g_practice_buffer = NULL;
GtkWidget *room_list; 

int time_remaining = 0;
int test_active = 0;
int selected_room_id = -1;
int current_user_id = -1;

typedef struct
{
    GtkWidget *user_entry;
    GtkWidget *pass_entry;
} LoginEntries;

typedef struct {
    GtkWidget *room_combo;
    GtkWidget *question_entry;
    GtkWidget *opt1_entry;
    GtkWidget *opt2_entry;
    GtkWidget *opt3_entry;
    GtkWidget *opt4_entry;
} QuestionFormData;

typedef struct {
    int total_tests;
    double avg_score;
    int max_score;
    int total_score;
} StatsData;
static StatsData stats = {0};

// Thêm struct để lưu trữ dữ liệu leaderboard
typedef struct {
    char rank[10];
    char username[50];
    int score;
    int tests;
} LeaderboardEntry;

// Hàm parse dữ liệu leaderboard từ server
GArray* parse_leaderboard_data(const char *buffer) {
    GArray *array = g_array_new(FALSE, FALSE, sizeof(LeaderboardEntry));
    
    if (strncmp(buffer, "LEADERBOARD|", 12) != 0) {
        return array;
    }
    
    char *copy = g_strdup(buffer);
    char *token = strtok(copy, "|");
    
    // Bỏ qua phần "LEADERBOARD"
    token = strtok(NULL, "|");
    
    while (token != NULL) {
        LeaderboardEntry entry;
        
        // Lấy rank
        if (token[0] == '#') {
            strncpy(entry.rank, token, sizeof(entry.rank) - 1);
            entry.rank[sizeof(entry.rank) - 1] = '\0';
        } else {
            break;
        }
        
        // Lấy username
        token = strtok(NULL, "|");
        if (!token) break;
        strncpy(entry.username, token, sizeof(entry.username) - 1);
        entry.username[sizeof(entry.username) - 1] = '\0';
        
        // Lấy score (Score:XXX)
        token = strtok(NULL, "|");
        if (!token) break;
        if (strncmp(token, "Score:", 6) == 0) {
            entry.score = atoi(token + 6);
        } else {
            entry.score = 0;
        }
        
        // Lấy tests (Tests:XXX)
        token = strtok(NULL, "|");
        if (!token) break;
        if (strncmp(token, "Tests:", 6) == 0) {
            entry.tests = atoi(token + 6);
        } else {
            entry.tests = 0;
        }
        
        g_array_append_val(array, entry);
        
        // Tiếp tục với người tiếp theo
        token = strtok(NULL, "|");
    }
    
    g_free(copy);
    return array;
}

// Hàm tạo CSS cho leaderboard
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

// Hàm parse dữ liệu từ server
void parse_stats_data(const char *buffer) {
    stats.total_tests = 0;
    stats.avg_score = 0.0;
    stats.max_score = 0;
    stats.total_score = 0;
    
    // Tìm từng field trong buffer
    const char *ptr = buffer;
    
    // Tìm Tests
    ptr = strstr(buffer, "Tests:");
    if (ptr) {
        stats.total_tests = atoi(ptr + 6);
    }
    
    // Tìm AvgScore
    ptr = strstr(buffer, "AvgScore:");
    if (ptr) {
        stats.avg_score = atof(ptr + 9);
    }
    
    // Tìm MaxScore
    ptr = strstr(buffer, "MaxScore:");
    if (ptr) {
        stats.max_score = atoi(ptr + 9);
    }
    
    // Tìm TotalScore
    ptr = strstr(buffer, "TotalScore:");
    if (ptr) {
        stats.total_score = atoi(ptr + 11);
    }
}

// Hàm vẽ đồ thị
static gboolean on_draw_chart(GtkWidget *widget, cairo_t *cr, gpointer data) {
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    
    // Background
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    
    if (stats.total_tests == 0) {
        // Hiển thị thông báo khi chưa có dữ liệu
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 16);
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_move_to(cr, width/2 - 100, height/2);
        cairo_show_text(cr, "📈 No statistics yet. Start testing!");
        return FALSE;
    }
    
    // Vẽ biểu đồ tròn cho Average Score
    int center_x = width / 3;
    int center_y = height / 2;
    int radius = (height < width/2) ? height/3 : width/6;
    
    double score_angle = (stats.avg_score * 2 * G_PI) / 100.0;
    
    // Vẽ phần background (màu xám nhạt)
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_arc(cr, center_x, center_y, radius, 0, 2 * G_PI);
    cairo_fill(cr);
    
    // Vẽ phần điểm số (màu gradient từ đỏ -> vàng -> xanh)
    double r, g, b;
    if (stats.avg_score < 50) {
        // Đỏ đến vàng
        r = 0.91;
        g = 0.3 + (stats.avg_score / 50.0) * 0.6;
        b = 0.24;
    } else {
        // Vàng đến xanh lá
        r = 0.91 - ((stats.avg_score - 50) / 50.0) * 0.73;
        g = 0.9 - ((stats.avg_score - 50) / 50.0) * 0.1;
        b = 0.24 + ((stats.avg_score - 50) / 50.0) * 0.2;
    }
    
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, center_x, center_y);
    cairo_arc(cr, center_x, center_y, radius, -G_PI/2, -G_PI/2 + score_angle);
    cairo_line_to(cr, center_x, center_y);
    cairo_fill(cr);
    
    // Vẽ viền trắng cho biểu đồ
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 3);
    cairo_arc(cr, center_x, center_y, radius, 0, 2 * G_PI);
    cairo_stroke(cr);
    
    // Vẽ text phần trăm ở giữa
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 24);
    cairo_set_source_rgb(cr, 0.17, 0.24, 0.31);
    
    char percent_text[20];
    snprintf(percent_text, sizeof(percent_text), "%.1f%%", stats.avg_score);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, percent_text, &extents);
    cairo_move_to(cr, center_x - extents.width/2, center_y + extents.height/2);
    cairo_show_text(cr, percent_text);
    
    // Vẽ thông tin chi tiết bên phải
    int info_x = width * 2 / 3;
    int info_y = height / 5;
    int spacing = 50;
    
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16);
    cairo_set_source_rgb(cr, 0.17, 0.24, 0.31);
    
    // Total Tests
    cairo_move_to(cr, info_x, info_y);
    char tests_text[100];
    snprintf(tests_text, sizeof(tests_text), "📝 Total Tests: %d", stats.total_tests);
    cairo_show_text(cr, tests_text);
    
    // Average Score
    info_y += spacing;
    cairo_move_to(cr, info_x, info_y);
    char avg_text[100];
    snprintf(avg_text, sizeof(avg_text), "📊 Average Score: %.2f%%", stats.avg_score);
    cairo_show_text(cr, avg_text);
    
    // Max Score
    info_y += spacing;
    cairo_move_to(cr, info_x, info_y);
    char max_text[100];
    snprintf(max_text, sizeof(max_text), "🏆 Best Score: %d", stats.max_score);
    cairo_show_text(cr, max_text);
    
    // Total Score
    info_y += spacing;
    cairo_move_to(cr, info_x, info_y);
    char total_text[100];
    snprintf(total_text, sizeof(total_text), "⭐ Total Score: %d", stats.total_score);
    cairo_show_text(cr, total_text);
    
    // Vẽ progress bar cho average score
    info_y += spacing * 1.2;
    int bar_width = 250;
    int bar_height = 30;
    
    // Label
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, info_x, info_y - 10);
    cairo_show_text(cr, "Performance:");
    
    // Background bar
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_rectangle(cr, info_x, info_y, bar_width, bar_height);
    cairo_fill(cr);
    
    // Progress bar (màu gradient tùy theo điểm)
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, info_x, info_y, bar_width * (stats.avg_score / 100.0), bar_height);
    cairo_fill(cr);
    
    // Bar border
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_set_line_width(cr, 2);
    cairo_rectangle(cr, info_x, info_y, bar_width, bar_height);
    cairo_stroke(cr);
    
    // Text trong bar
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_font_size(cr, 12);
    char bar_text[20];
    snprintf(bar_text, sizeof(bar_text), "%.1f%%", stats.avg_score);
    cairo_text_extents(cr, bar_text, &extents);
    cairo_move_to(cr, info_x + (bar_width * stats.avg_score / 100.0) - extents.width - 5, 
                  info_y + bar_height/2 + extents.height/2);
    cairo_show_text(cr, bar_text);
    
    return FALSE;
}

// Callback khi click vào button của phòng
void on_room_button_clicked(GtkWidget *button, gpointer data)
{
    const char *room_id_str = g_object_get_data(G_OBJECT(button), "room_id");
    const char *room_name = gtk_button_get_label(GTK_BUTTON(button));
    
    if (room_id_str)
    {
        selected_room_id = atoi(room_id_str);
        
        // Cập nhật label hiển thị phòng đã chọn
        char markup[256];
        snprintf(markup, sizeof(markup), 
                 "<span foreground='#27ae60' weight='bold'>✅ Selected: %s (ID: %d)</span>", 
                 room_name, selected_room_id);
        gtk_label_set_markup(GTK_LABEL(selected_room_label), markup);
    }
}

void load_rooms_list()
{
    // Reset selected room
    selected_room_id = -1;
    gtk_label_set_markup(GTK_LABEL(selected_room_label), 
                        "<span foreground='#e74c3c'>❌ No room selected</span>");
    
    // Xóa hết các row cũ
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(rooms_list));
    for (iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    // Gửi yêu cầu lấy danh sách phòng
    send_message("LIST_ROOMS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && buffer[0] != '\0')
    {
        char *line = strtok(buffer, "\n");
        int room_count = 0;
        
        while (line != NULL)
        {
            if (strncmp(line, "ROOM|", 5) == 0)
            {
                // Parse: ROOM|id|name|time|status|question_status|owner
                char room_id[16], room_name[64], owner[32], status[32];
                int time_limit;
                
                sscanf(line, "ROOM|%15[^|]|%63[^|]|%d|%31[^|]|%*[^|]|%31s",
                       room_id, room_name, &time_limit, status, owner);

                // Tạo button với thông tin phòng
                char button_label[128];
                snprintf(button_label, sizeof(button_label), 
                        "🏠 %s | ⏱️ %dmin | 👤 %s", 
                        room_name, time_limit, owner);
                
                GtkWidget *btn = gtk_button_new_with_label(button_label);
                
                // Lưu room_id vào button
                g_object_set_data_full(G_OBJECT(btn), "room_id", 
                                      g_strdup(room_id), g_free);
                
                // Style cho button
                GtkCssProvider *provider = gtk_css_provider_new();
                char css[512];
                snprintf(css, sizeof(css),
                        "button { "
                        "  background: linear-gradient(135deg, #667eea 0%%, #764ba2 100%%);"
                        "  color: white;"
                        "  padding: 15px;"
                        "  font-weight: bold;"
                        "  border-radius: 8px;"
                        "  border: none;"
                        "  margin: 5px;"
                        "}"
                        "button:hover {"
                        "  background: linear-gradient(135deg, #764ba2 0%%, #667eea 100%%);"
                        "}");
                gtk_css_provider_load_from_data(provider, css, -1, NULL);
                gtk_style_context_add_provider(gtk_widget_get_style_context(btn),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                
                // Kết nối signal
                g_signal_connect(btn, "clicked", 
                               G_CALLBACK(on_room_button_clicked), NULL);

                // Thêm vào list
                GtkWidget *row = gtk_list_box_row_new();
                gtk_container_add(GTK_CONTAINER(row), btn);
                gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
                
                room_count++;
            }
            line = strtok(NULL, "\n");
        }
        
        if (room_count == 0)
        {
            GtkWidget *label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(label), 
                               "<span foreground='#95a5a6' size='large'>📭 No rooms available. Create one!</span>");
            GtkWidget *row = gtk_list_box_row_new();
            gtk_container_add(GTK_CONTAINER(row), label);
            gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
        }
    }
    else
    {
        GtkWidget *label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), 
                           "<span foreground='#e74c3c'>⚠️ Cannot load rooms. Check connection.</span>");
        GtkWidget *row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), label);
        gtk_list_box_insert(GTK_LIST_BOX(rooms_list), row, -1);
    }

    gtk_widget_show_all(GTK_WIDGET(rooms_list));
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
        // Parse response: LOGIN_OK|user_id|token
        char *token_start = strchr(buffer, '|');
        if (token_start) {
            token_start++; // skip first '|'
            current_user_id = atoi(token_start);
        }
        
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

    gtk_widget_set_name(login_btn, "login_btn");
    gtk_widget_set_name(register_btn, "register_btn");
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
                                                                   "✅ Room created!");
                gtk_dialog_run(GTK_DIALOG(success_dialog));
                gtk_widget_destroy(success_dialog);

                // Cập nhật danh sách phòng
                load_rooms_list();
            }
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

// Hàm hiển thị dialog
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


void request_user_rooms(GtkWidget *room_combo)
{
    char request[256];
    snprintf(request, sizeof(request), "GET_USER_ROOMS|%d\n", current_user_id);
    send_message(request);
    
    // Nhận response và populate combo box
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        // Parse: ROOMS_LIST|room_id1:room_name1|room_id2:room_name2|...
        if (strstr(buffer, "ROOMS_LIST")) {
            char *token = strtok(buffer + 11, "|"); // skip "ROOMS_LIST|"
            while (token != NULL) {
                char *colon = strchr(token, ':');
                if (colon) {
                    *colon = '\0';
                    char *room_id = token;
                    char *room_name = colon + 1;
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), room_id, room_name);
                }
                token = strtok(NULL, "|");
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(room_combo), 0);
        } else if (strstr(buffer, "NO_ROOMS")) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(room_combo), "-1", "No rooms created yet");
            gtk_widget_set_sensitive(room_combo, FALSE);
        }
    }
}

// Callback khi submit question
void on_submit_question(GtkWidget *widget, gpointer user_data)
{
    QuestionFormData *data = (QuestionFormData *)user_data;
    
    // Lấy room_id từ combo box
    const char *room_id_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(data->room_combo));
    if (!room_id_str || atoi(room_id_str) == -1) {
        show_error_dialog("Please select a valid room!");
        return;
    }
    
    // Lấy dữ liệu từ form
    const char *question = gtk_entry_get_text(GTK_ENTRY(data->question_entry));
    const char *opt1 = gtk_entry_get_text(GTK_ENTRY(data->opt1_entry));
    const char *opt2 = gtk_entry_get_text(GTK_ENTRY(data->opt2_entry));
    const char *opt3 = gtk_entry_get_text(GTK_ENTRY(data->opt3_entry));
    const char *opt4 = gtk_entry_get_text(GTK_ENTRY(data->opt4_entry));
    
    // Validation
    if (strlen(question) == 0 || strlen(opt1) == 0 || strlen(opt2) == 0 || 
        strlen(opt3) == 0 || strlen(opt4) == 0) {
        show_error_dialog("Please fill all fields!");
        return;
    }
    
    // Gửi request đến server
    char request[2048];
    snprintf(request, sizeof(request), 
             "ADD_QUESTION|%d|%s|%s|%s|%s|%s\n",
             atoi(room_id_str), question, opt1, opt2, opt3, opt4);
    send_message(request);
    
    // Nhận response
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        if (strstr(buffer, "QUESTION_ADDED")) {
            show_success_dialog("Question added successfully!");
            // Reset form
            gtk_entry_set_text(GTK_ENTRY(data->question_entry), "");
            gtk_entry_set_text(GTK_ENTRY(data->opt1_entry), "");
            gtk_entry_set_text(GTK_ENTRY(data->opt2_entry), "");
            gtk_entry_set_text(GTK_ENTRY(data->opt3_entry), "");
            gtk_entry_set_text(GTK_ENTRY(data->opt4_entry), "");
        } else {
            show_error_dialog("Failed to add question!");
        }
    }
}

// Hàm tham gia phòng - ĐÃ SỬA
void on_join_room_clicked(GtkWidget *widget, gpointer data)
{
    if (selected_room_id < 0)
    {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                                   "⚠️ Please select a room first!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    // Gửi lệnh JOIN_ROOM với room_id đã chọn
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "JOIN_ROOM|%d\n", selected_room_id);
    send_message(cmd);

    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && strstr(buffer, "JOIN_ROOM_OK"))
    {
        // Joined successfully - Start exam immediately
        create_exam_page(selected_room_id);
    }
    else
    {
        char *error_msg = "Failed to join room";
        if (strstr(buffer, "|")) {
            error_msg = strchr(buffer, '|') + 1;
        }
        
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "❌ %s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void create_test_mode_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>📝 TEST MODE</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label), "<span foreground='#34495e' size='large'>💡 Click on a room button to select, then click JOIN ROOM</span>");
    gtk_box_pack_start(GTK_BOX(vbox), info_label, FALSE, FALSE, 5);

    // Button box
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *create_btn = gtk_button_new_with_label("➕ CREATE ROOM");
    GtkWidget *join_btn = gtk_button_new_with_label("🚪 JOIN ROOM");
    GtkWidget *refresh_btn = gtk_button_new_with_label("🔄 REFRESH");
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");

    style_button(create_btn, "#27ae60");
    style_button(join_btn, "#3498db");
    style_button(refresh_btn, "#f39c12");
    style_button(back_btn, "#95a5a6");

    gtk_box_pack_start(GTK_BOX(button_box), create_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), join_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), refresh_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    // Label phòng đang chọn
    selected_room_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(selected_room_label), 
                        "<span foreground='#e74c3c' size='large'>❌ No room selected</span>");
    gtk_box_pack_start(GTK_BOX(vbox), selected_room_label, FALSE, FALSE, 5);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 5);

    // ListBox cho danh sách phòng
    rooms_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(rooms_list), GTK_SELECTION_NONE);
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(rooms_list));
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 350);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Load danh sách phòng
    load_rooms_list();

    // Signal connect
    g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_room_clicked), NULL);
    g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_room_clicked), NULL);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(load_rooms_list), NULL);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);

    show_view(vbox);
}

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

void create_stats_screen()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    // Title
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span foreground='#2c3e50' weight='bold' size='20480'>📊 MY STATISTICS</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    // Separator
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    // Nhận dữ liệu từ server
    send_message("USER_STATS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    if (n > 0 && buffer[0] != '\0') {
        parse_stats_data(buffer);
    }

    // Drawing area cho đồ thị
    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 600, 400);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(on_draw_chart), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);

    // Button box
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
}

void create_leaderboard_screen() {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);

    // Title với icon
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *trophy_icon = gtk_label_new("🏆");
    gtk_widget_set_name(trophy_icon, "trophy-icon");
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span foreground='#2c3e50' weight='bold' size='20480'>LEADERBOARD - TOP PLAYERS</span>");
    
    gtk_box_pack_start(GTK_BOX(title_box), trophy_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title_box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), title_box, FALSE, FALSE, 5);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 10);

    // Thông tin cập nhật
    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label), 
        "<span foreground='#7f8c8d' size='small'>📊 Updated in real-time • Based on total score</span>");
    gtk_box_pack_start(GTK_BOX(vbox), info_label, FALSE, FALSE, 5);

    // Main container cho leaderboard
    GtkWidget *main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(main_container, "leaderboard-container");
    
    // Thiết lập CSS
    setup_leaderboard_css();

    // Header của bảng
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(header_box, "leaderboard-header");
    
    GtkWidget *rank_header = gtk_label_new("🏅 RANK");
    GtkWidget *user_header = gtk_label_new("👤 PLAYER");
    GtkWidget *score_header = gtk_label_new("⭐ SCORE");
    GtkWidget *tests_header = gtk_label_new("📝 TESTS");
    
    gtk_label_set_xalign(GTK_LABEL(rank_header), 0.5);
    gtk_label_set_xalign(GTK_LABEL(user_header), 0.0);
    gtk_label_set_xalign(GTK_LABEL(score_header), 0.5);
    gtk_label_set_xalign(GTK_LABEL(tests_header), 0.5);
    
    gtk_widget_set_hexpand(rank_header, TRUE);
    gtk_widget_set_hexpand(user_header, TRUE);
    gtk_widget_set_hexpand(score_header, TRUE);
    gtk_widget_set_hexpand(tests_header, TRUE);
    
    gtk_box_pack_start(GTK_BOX(header_box), rank_header, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header_box), user_header, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header_box), score_header, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header_box), tests_header, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_container), header_box, FALSE, FALSE, 0);

    // Scroll window cho nội dung
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
    
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(scroll), content_box);

    // Gửi yêu cầu và nhận dữ liệu từ server
    send_message("LEADERBOARD\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    if (n > 0 && buffer[0] != '\0') {
        // Parse dữ liệu
        GArray *entries = parse_leaderboard_data(buffer);
        
        if (entries->len > 0) {
            for (guint i = 0; i < entries->len; i++) {
                LeaderboardEntry entry = g_array_index(entries, LeaderboardEntry, i);
                
                // Tạo hàng cho mỗi người chơi
                GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
                gtk_widget_set_name(row_box, "leaderboard-row");
                
                // Thêm class cho top 3
                int rank_num = atoi(entry.rank + 1); // Bỏ ký tự '#'
                if (rank_num == 1) {
                    gtk_style_context_add_class(gtk_widget_get_style_context(row_box), "rank-1");
                } else if (rank_num == 2) {
                    gtk_style_context_add_class(gtk_widget_get_style_context(row_box), "rank-2");
                } else if (rank_num == 3) {
                    gtk_style_context_add_class(gtk_widget_get_style_context(row_box), "rank-3");
                }
                
                // Rank với icon đặc biệt cho top 3
                GtkWidget *rank_label;
                if (rank_num == 1) {
                    rank_label = gtk_label_new("🥇");
                } else if (rank_num == 2) {
                    rank_label = gtk_label_new("🥈");
                } else if (rank_num == 3) {
                    rank_label = gtk_label_new("🥉");
                } else {
                    rank_label = gtk_label_new(entry.rank);
                }
                gtk_label_set_xalign(GTK_LABEL(rank_label), 0.5);
                gtk_widget_set_hexpand(rank_label, TRUE);
                
                // Username
                GtkWidget *user_label = gtk_label_new(entry.username);
                gtk_label_set_xalign(GTK_LABEL(user_label), 0.0);
                gtk_widget_set_hexpand(user_label, TRUE);
                gtk_style_context_add_class(gtk_widget_get_style_context(user_label), "username-cell");
                
                // Score
                GtkWidget *score_label = gtk_label_new(NULL);
                char score_text[50];
                snprintf(score_text, sizeof(score_text), "%d", entry.score);
                gtk_label_set_text(GTK_LABEL(score_label), score_text);
                gtk_label_set_xalign(GTK_LABEL(score_label), 0.5);
                gtk_widget_set_hexpand(score_label, TRUE);
                gtk_style_context_add_class(gtk_widget_get_style_context(score_label), "score-cell");
                
                // Tests
                GtkWidget *tests_label = gtk_label_new(NULL);
                char tests_text[50];
                snprintf(tests_text, sizeof(tests_text), "%d", entry.tests);
                gtk_label_set_text(GTK_LABEL(tests_label), tests_text);
                gtk_label_set_xalign(GTK_LABEL(tests_label), 0.5);
                gtk_widget_set_hexpand(tests_label, TRUE);
                gtk_style_context_add_class(gtk_widget_get_style_context(tests_label), "tests-cell");
                
                // Thêm vào hàng
                gtk_box_pack_start(GTK_BOX(row_box), rank_label, TRUE, TRUE, 0);
                gtk_box_pack_start(GTK_BOX(row_box), user_label, TRUE, TRUE, 0);
                gtk_box_pack_start(GTK_BOX(row_box), score_label, TRUE, TRUE, 0);
                gtk_box_pack_start(GTK_BOX(row_box), tests_label, TRUE, TRUE, 0);
                
                // Thêm vào content box
                gtk_box_pack_start(GTK_BOX(content_box), row_box, FALSE, FALSE, 0);
            }
            
            g_array_free(entries, TRUE);
        } else {
            // Hiển thị thông báo nếu không có dữ liệu
            GtkWidget *empty_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(empty_label), 
                "<span foreground='#95a5a6' size='large'>📭 No players on leaderboard yet</span>");
            gtk_box_pack_start(GTK_BOX(content_box), empty_label, FALSE, FALSE, 20);
        }
    } else {
        // Hiển thị lỗi nếu không kết nối được
        GtkWidget *error_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(error_label), 
            "<span foreground='#e74c3c' size='large'>⚠️ Cannot load leaderboard. Check connection.</span>");
        gtk_box_pack_start(GTK_BOX(content_box), error_label, FALSE, FALSE, 20);
    }
    
    gtk_box_pack_start(GTK_BOX(main_container), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), main_container, TRUE, TRUE, 10);

    // Statistics summary (nếu có dữ liệu)
    GtkWidget *stats_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_margin_top(stats_box, 10);
    gtk_widget_set_margin_bottom(stats_box, 10);
    
    GtkWidget *total_players = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(total_players), 
        "<span foreground='#27ae60' weight='bold'>👥 Total Players: Loading...</span>");
    
    GtkWidget *avg_score = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(avg_score), 
        "<span foreground='#3498db' weight='bold'>📊 Avg Score: Calculating...</span>");
    
    gtk_box_pack_start(GTK_BOX(stats_box), total_players, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(stats_box), avg_score, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), stats_box, FALSE, FALSE, 0);

    // Button box
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *refresh_btn = gtk_button_new_with_label("🔄 REFRESH");
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    
    // Style cho buttons
    GtkCssProvider *btn_provider = gtk_css_provider_new();
    const gchar *btn_css = 
        "#refresh-btn {"
        "  background: linear-gradient(135deg, #3498db 0%, #2980b9 100%);"
        "  color: white;"
        "  padding: 8px 16px;"
        "  border-radius: 6px;"
        "  font-weight: bold;"
        "}"
        "#refresh-btn:hover {"
        "  background: linear-gradient(135deg, #2980b9 0%, #3498db 100%);"
        "}"
        "#back-btn {"
        "  background: linear-gradient(135deg, #95a5a6 0%, #7f8c8d 100%);"
        "  color: white;"
        "  padding: 8px 16px;"
        "  border-radius: 6px;"
        "  font-weight: bold;"
        "}"
        "#back-btn:hover {"
        "  background: linear-gradient(135deg, #7f8c8d 0%, #95a5a6 100%);"
        "}";
    
    gtk_css_provider_load_from_data(btn_provider, btn_css, -1, NULL);
    
    gtk_widget_set_name(refresh_btn, "refresh-btn");
    gtk_widget_set_name(back_btn, "back-btn");
    
    gtk_style_context_add_provider(gtk_widget_get_style_context(refresh_btn),
                                  GTK_STYLE_PROVIDER(btn_provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_style_context_add_provider(gtk_widget_get_style_context(back_btn),
                                  GTK_STYLE_PROVIDER(btn_provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    g_object_unref(btn_provider);
    
    gtk_box_pack_start(GTK_BOX(button_box), refresh_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 10);

    // Signal connections
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(create_leaderboard_screen), NULL);
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
    gtk_label_set_markup(GTK_LABEL(info), "<span foreground='#27ae60' weight='bold'>✏️ Add new questions to your rooms:</span>");
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 0);

    GtkWidget *form_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(form_box, 10);
    gtk_widget_set_margin_end(form_box, 10);

    // **MỚI: Dropdown chọn room**
    GtkWidget *room_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(room_label), "<span foreground='#e74c3c' weight='bold'>Select Your Room:</span>");
    GtkWidget *room_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(form_box), room_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form_box), room_combo, FALSE, FALSE, 0);

    // Request danh sách rooms từ server
    request_user_rooms(room_combo);

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
    gtk_label_set_markup(GTK_LABEL(opt1_label), "<span foreground='#27ae60'>Option 1 (Correct ✓):</span>");
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

    // Struct để truyền data vào callback
    QuestionFormData *data = g_malloc(sizeof(QuestionFormData));
    data->room_combo = room_combo;
    data->question_entry = question_entry;
    data->opt1_entry = opt1_entry;
    data->opt2_entry = opt2_entry;
    data->opt3_entry = opt3_entry;
    data->opt4_entry = opt4_entry;

    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_question), data);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    
    show_view(vbox);
}

// Callback khi nhấn button Start Room
static void on_start_room_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    char msg[64];
    snprintf(msg, sizeof(msg), "START_ROOM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    GtkWidget *dialog;
    if (n > 0 && strncmp(buffer, "START_ROOM_OK", 13) == 0) {
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_OK,
                                       "✅ Room opened successfully!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        // Refresh admin panel
        create_admin_panel();
    } else {
        // Parse error message
        char *error_msg = strchr(buffer, '|');
        if (error_msg) {
            error_msg++; // Skip '|'
        } else {
            error_msg = "Failed to open room";
        }
        
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_OK,
                                       "❌ %s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

// Callback khi nhấn button Close Room
static void on_close_room_clicked(GtkWidget *widget, gpointer data) {
    int room_id = GPOINTER_TO_INT(data);
    
    // Confirm dialog
    GtkWidget *confirm_dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       GTK_MESSAGE_QUESTION,
                                                       GTK_BUTTONS_YES_NO,
                                                       "Are you sure you want to close this room?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(confirm_dialog),
                                             "This room will be removed from the list.");
    
    int response = gtk_dialog_run(GTK_DIALOG(confirm_dialog));
    gtk_widget_destroy(confirm_dialog);
    
    if (response != GTK_RESPONSE_YES) {
        return;
    }
    
    char msg[64];
    snprintf(msg, sizeof(msg), "CLOSE_ROOM|%d\n", room_id);
    send_message(msg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));
    
    GtkWidget *dialog;
    if (n > 0 && strncmp(buffer, "CLOSE_ROOM_OK", 13) == 0) {
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_OK,
                                       "✅ Room closed successfully!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        // Refresh admin panel
        create_admin_panel();
    } else {
        // Parse error message
        char *error_msg = strchr(buffer, '|');
        if (error_msg) {
            error_msg++; // Skip '|'
        } else {
            error_msg = "Failed to close room";
        }
        
        dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_OK,
                                       "❌ %s", error_msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void create_admin_panel()
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span foreground='#2c3e50' weight='bold' size='20480'>⚙️ ADMIN PANEL - My Rooms</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    // Request rooms từ server
    send_message("LIST_MY_ROOMS\n");
    char buffer[BUFFER_SIZE];
    ssize_t n = receive_message(buffer, sizeof(buffer));

    // Tạo scrolled window chứa list rooms
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 400);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Parse và hiển thị rooms
    if (n > 0 && strncmp(buffer, "LIST_MY_ROOMS_OK", 16) == 0) {
        char *line = strtok(buffer, "\n");
        line = strtok(NULL, "\n"); // Bỏ qua header line

        int room_count = 0;
        while (line != NULL) {
            if (strncmp(line, "ROOM|", 5) == 0) {
                // Parse: ROOM|id|name|duration|status|question_info
                char *room_data = strdup(line);
                strtok(room_data, "|"); // Skip "ROOM"
                
                int room_id = atoi(strtok(NULL, "|"));
                char *room_name = strtok(NULL, "|");
                int duration = atoi(strtok(NULL, "|"));
                char *status = strtok(NULL, "|");
                char *question_info = strtok(NULL, "|");

                // Tạo row cho room
                GtkWidget *row = gtk_list_box_row_new();
                GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_widget_set_margin_top(hbox, 10);
                gtk_widget_set_margin_bottom(hbox, 10);
                gtk_widget_set_margin_start(hbox, 15);
                gtk_widget_set_margin_end(hbox, 15);
                gtk_container_add(GTK_CONTAINER(row), hbox);

                // Room info
                char info_text[512];
                snprintf(info_text, sizeof(info_text),
                        "<b>%s</b> (ID: %d)\n"
                        "⏱️ Duration: %d minutes | 📝 %s | Status: %s",
                        room_name, room_id, duration, question_info, status);
                
                GtkWidget *info_label = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(info_label), info_text);
                gtk_label_set_xalign(GTK_LABEL(info_label), 0);
                gtk_box_pack_start(GTK_BOX(hbox), info_label, TRUE, TRUE, 0);

                // Action buttons - luôn hiển thị dựa trên status
                GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
                
                if (strcmp(status, "Open") == 0) {
                    // Room đang Open - hiển thị button CLOSE
                    GtkWidget *close_btn = gtk_button_new_with_label("🔒 CLOSE");
                    style_button(close_btn, "#e74c3c");
                    gtk_widget_set_size_request(close_btn, 120, 40);
                    g_signal_connect(close_btn, "clicked", 
                                   G_CALLBACK(on_close_room_clicked), 
                                   GINT_TO_POINTER(room_id));
                    gtk_box_pack_start(GTK_BOX(btn_box), close_btn, FALSE, FALSE, 0);
                } else {
                    // Room đang Closed - hiển thị button OPEN
                    GtkWidget *open_btn = gtk_button_new_with_label("🔓 OPEN");
                    style_button(open_btn, "#27ae60");
                    gtk_widget_set_size_request(open_btn, 120, 40);
                    g_signal_connect(open_btn, "clicked", 
                                   G_CALLBACK(on_start_room_clicked), 
                                   GINT_TO_POINTER(room_id));
                    gtk_box_pack_start(GTK_BOX(btn_box), open_btn, FALSE, FALSE, 0);
                }
                
                gtk_box_pack_end(GTK_BOX(hbox), btn_box, FALSE, FALSE, 0);

                gtk_container_add(GTK_CONTAINER(list_box), row);
                room_count++;
                free(room_data);
            }
            line = strtok(NULL, "\n");
        }

        if (room_count == 0) {
            GtkWidget *empty_label = gtk_label_new("📭 You haven't created any rooms yet");
            gtk_widget_set_margin_top(empty_label, 50);
            gtk_widget_set_margin_bottom(empty_label, 50);
            gtk_container_add(GTK_CONTAINER(list_box), empty_label);
        }
    } else {
        GtkWidget *error_label = gtk_label_new("❌ Failed to load rooms");
        gtk_widget_set_margin_top(error_label, 50);
        gtk_widget_set_margin_bottom(error_label, 50);
        gtk_container_add(GTK_CONTAINER(list_box), error_label);
    }

    // Back button
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back_btn = gtk_button_new_with_label("⬅️ BACK");
    style_button(back_btn, "#95a5a6");
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);
    show_view(vbox);
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
