#include "stats_ui.h"
#include "ui_utils.h"
#include "ui.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>
#include <cairo.h>
#include <gdk/gdk.h>
#include <glib.h>

StatsData stats = {0};

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

gboolean on_draw_chart(GtkWidget *widget, cairo_t *cr, gpointer data) {
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

    // Flush old responses before new request
    flush_socket_buffer(client.socket_fd);
    
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

    // Separator trước lịch sử test
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 10);

    // Test History Section
    GtkWidget *history_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(history_title), 
        "<span foreground='#2c3e50' weight='bold' size='16384'>📝 Recent Test History</span>");
    gtk_box_pack_start(GTK_BOX(vbox), history_title, FALSE, FALSE, 5);

    // Scroll window cho test history
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 200);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 300);

    GtkWidget *history_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(scroll), history_box);

    // Flush old responses before new request
    flush_socket_buffer(client.socket_fd);
    
    // Request test history từ server
    send_message("TEST_HISTORY\n");
    char history_buffer[BUFFER_SIZE * 4];
    ssize_t hn = receive_message(history_buffer, sizeof(history_buffer));

    if (hn > 0 && strncmp(history_buffer, "TEST_HISTORY", 12) == 0) {
        // Parse response: TEST_HISTORY|result_id|room_name|score|total|time|date|result_id2|...
        // Make a copy for safe parsing
        char *buffer_copy = strdup(history_buffer);
        char *data = buffer_copy;
        
        // Skip "TEST_HISTORY|"
        char *token = strchr(data, '|');
        if (token) {
            token++; // Skip first '|'
            data = token;
        }
        
        int count = 0;
        while (*data && count < 20) {
            // Parse one record: result_id|room_name|score|total|time|date|
            char *record_end = data;
            int field_count = 0;
            
            // Find 6 fields (one complete record)
            while (*record_end && field_count < 6) {
                if (*record_end == '|') field_count++;
                record_end++;
            }
            
            if (field_count < 6) break; // Incomplete record
            
            // Extract fields
            char fields[6][256];
            char *field_start = data;
            int f = 0;
            
            for (char *p = data; p < record_end && f < 6; p++) {
                if (*p == '|') {
                    size_t len = p - field_start;
                    if (len > 255) len = 255;
                    strncpy(fields[f], field_start, len);
                    fields[f][len] = '\0';
                    field_start = p + 1;
                    f++;
                }
            }
            
            if (f == 6) {
                int result_id = atoi(fields[0]);
                char *room_name = fields[1];
                int score = atoi(fields[2]);
                int total = atoi(fields[3]);
                int time_taken = atoi(fields[4]);
                char *completed = fields[5];
                
                // Tạo row cho mỗi test
                GtkWidget *test_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_widget_set_margin_start(test_row, 10);
                gtk_widget_set_margin_end(test_row, 10);
                gtk_widget_set_margin_top(test_row, 5);
                gtk_widget_set_margin_bottom(test_row, 5);
                
                // Icon và room name
                char room_text[256];
                snprintf(room_text, sizeof(room_text), "📚 %s", room_name);
                GtkWidget *room_label = gtk_label_new(room_text);
                gtk_label_set_xalign(GTK_LABEL(room_label), 0.0);
                gtk_widget_set_size_request(room_label, 250, -1);
                
                // Score
                char score_text[64];
                double percent = (total > 0) ? (score * 100.0 / total) : 0;
                snprintf(score_text, sizeof(score_text), "📊 %d/%d (%.1f%%)", score, total, percent);
                GtkWidget *score_label = gtk_label_new(score_text);
                gtk_widget_set_size_request(score_label, 150, -1);
                
                // Time
                char time_text[64];
                int minutes = time_taken / 60;
                int seconds = time_taken % 60;
                snprintf(time_text, sizeof(time_text), "⏱️ %dm %ds", minutes, seconds);
                GtkWidget *time_label = gtk_label_new(time_text);
                gtk_widget_set_size_request(time_label, 120, -1);
                
                // Date
                char date_text[128];
                snprintf(date_text, sizeof(date_text), "📅 %s", completed);
                GtkWidget *date_label = gtk_label_new(date_text);
                
                gtk_box_pack_start(GTK_BOX(test_row), room_label, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(test_row), score_label, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(test_row), time_label, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(test_row), date_label, TRUE, TRUE, 0);
                
                gtk_box_pack_start(GTK_BOX(history_box), test_row, FALSE, FALSE, 0);
                
                // Separator giữa các row
                GtkWidget *row_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                gtk_box_pack_start(GTK_BOX(history_box), row_sep, FALSE, FALSE, 0);
                
                count++;
            }
            
            data = record_end;
        }
        
        free(buffer_copy);
        
        if (count == 0) {
            GtkWidget *empty_label = gtk_label_new("No test history yet. Take your first test!");
            gtk_box_pack_start(GTK_BOX(history_box), empty_label, FALSE, FALSE, 20);
        }
    } else {
        GtkWidget *error_label = gtk_label_new("Failed to load test history");
        gtk_box_pack_start(GTK_BOX(history_box), error_label, FALSE, FALSE, 20);
    }

    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 10);

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
    
    style_button(refresh_btn, "#3498db");
    style_button(back_btn, "#95a5a6");
    
    gtk_box_pack_start(GTK_BOX(button_box), refresh_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), back_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 10);

    // Signal connections
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(create_leaderboard_screen), NULL);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(create_main_menu), NULL);

    show_view(vbox);
}
