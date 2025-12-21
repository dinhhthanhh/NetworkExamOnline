#ifndef STATS_UI_H
#define STATS_UI_H

#include "include/client_common.h"

// Statistics structure
typedef struct {
    int total_tests;
    double avg_score;
    int max_score;
    int total_score;
} StatsData;

// Leaderboard entry structure
typedef struct {
    char rank[10];
    char username[50];
    int score;
    int tests;
} LeaderboardEntry;

// Statistics screens
void create_stats_screen(void);
void create_leaderboard_screen(void);

// Data parsing
void parse_stats_data(const char *buffer);
GArray* parse_leaderboard_data(const char *buffer);

// Drawing
gboolean on_draw_chart(GtkWidget *widget, cairo_t *cr, gpointer data);

// External global variables
extern StatsData stats;

#endif // STATS_UI_H
