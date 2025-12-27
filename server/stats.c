#include "stats.h"
#include "db.h"
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>

#define RESP_BIG   8192
#define RESP_MED   4096
#define RESP_SMALL 256

#define SAFE_TEXT(x) ((x) ? (const char *)(x) : "N/A")

extern sqlite3 *db;

/* =========================================================
   HELPERS
   ========================================================= */

static void send_response_safe(int socket_fd, const char *response)
{
    if (response && *response)
        send(socket_fd, response, strlen(response), 0);
}

static int append_safe(char *dest, size_t max, const char *src)
{
    size_t cur = strlen(dest);
    size_t add = strlen(src);

    if (cur + add + 1 >= max)
        return -1;

    strcat(dest, src);
    return 0;
}

/* =========================================================
   GLOBAL LEADERBOARD
   ========================================================= */

void get_leaderboard(int socket_fd, int limit)
{
    if (limit <= 0 || limit > 100) limit = 10;

    const char *sql =
        "SELECT u.username, "
        "COALESCE(SUM(r.score),0), "
        "COUNT(r.id) "
        "FROM users u "
        "LEFT JOIN results r ON u.id = r.user_id "
        "GROUP BY u.id "
        "ORDER BY 2 DESC, 3 DESC "
        "LIMIT ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("DB error: %s", sqlite3_errmsg(db));
        send(socket_fd, "LEADERBOARD_FAIL\n", 17, 0);
        return;
    }

    sqlite3_bind_int(stmt, 1, limit);

    char response[RESP_BIG] = "LEADERBOARD|";
    int rank = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char entry[RESP_SMALL];
        snprintf(entry, sizeof(entry),
            "#%d|%s|Score:%d|Tests:%d|",
            rank++,
            SAFE_TEXT(sqlite3_column_text(stmt, 0)),
            sqlite3_column_int(stmt, 1),
            sqlite3_column_int(stmt, 2));

        if (append_safe(response, sizeof(response), entry) != 0)
            break;
    }

    append_safe(response, sizeof(response), "\n");
    send_response_safe(socket_fd, response);
    sqlite3_finalize(stmt);
}

void get_user_test_history(int socket_fd, int user_id)
{
    const char *sql =
        "SELECT r.id, ro.name, r.score, r.total_questions, r.time_taken, r.submitted_at "
        "FROM results r "
        "JOIN rooms ro ON r.room_id = ro.id "
        "WHERE r.user_id=? "
        "ORDER BY r.submitted_at DESC "
        "LIMIT 20;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "TEST_HISTORY_FAIL\n", 18, 0);
        return;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    char response[4096] = "TEST_HISTORY|";

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char entry[256];
        const char *submitted_at = (const char *)sqlite3_column_text(stmt, 5);
        // Return time_taken as seconds (numeric) so client can format it
        snprintf(entry, sizeof(entry),
            "%d|%s|%d|%d|%lld|%s|",
            sqlite3_column_int(stmt, 0),
            SAFE_TEXT((const char *)sqlite3_column_text(stmt, 1)),
            sqlite3_column_int(stmt, 2),
            sqlite3_column_int(stmt, 3),
            (long long)sqlite3_column_int64(stmt, 4),
            SAFE_TEXT(submitted_at));

        if (append_safe(response, sizeof(response), entry) != 0)
            break;
    }

    append_safe(response, sizeof(response), "\n");
    send_response_safe(socket_fd, response);
    sqlite3_finalize(stmt);
}


/* =========================================================
   ROOM LEADERBOARD
   ========================================================= */

void get_room_leaderboard(int socket_fd, int room_id, int limit)
{
    if (limit <= 0 || limit > 100) limit = 20;

    const char *sql =
        "SELECT u.username, r.score, r.total_questions, "
        "r.time_taken, r.submit_reason "
        "FROM results r "
        "JOIN users u ON r.user_id = u.id "
        "WHERE r.room_id = ? "
        "ORDER BY r.score DESC, r.time_taken ASC "
        "LIMIT ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "ROOM_LEADERBOARD_FAIL\n", 22, 0);
        return;
    }

    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, limit);

    char response[8192];
    snprintf(response, sizeof(response),
             "ROOM_LEADERBOARD|%d|", room_id);

    int rank = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char entry[256];
        snprintf(entry, sizeof(entry),
                 "#%d|%s|Score:%d|Tests:%d|Time:%lldm|Reason:%s|",
                 rank++,
                 SAFE_TEXT((const char *)sqlite3_column_text(stmt, 0)),
                 sqlite3_column_int(stmt, 1),
                 sqlite3_column_int(stmt, 2),
                 (long long)(sqlite3_column_int64(stmt, 3) / 60),
                 SAFE_TEXT((const char *)sqlite3_column_text(stmt, 4)));

        if (append_safe(response, sizeof(response), entry) != 0)
            break;
    }

    append_safe(response, sizeof(response), "\n");
    send_response_safe(socket_fd, response);
    sqlite3_finalize(stmt);
}

/* =========================================================
   USER STATISTICS
   ========================================================= */

void get_user_statistics(int socket_fd, int user_id)
{
    const char *sql =
        "SELECT COUNT(*), "
        "CASE WHEN SUM(total_questions)=0 THEN 0 "
        "ELSE AVG(score * 1.0 / total_questions) END, "
        "MAX(score), SUM(score), SUM(time_taken) "
        "FROM results WHERE user_id=?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return;

    sqlite3_bind_int(stmt, 1, user_id);

    char response[RESP_SMALL];
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int tests = sqlite3_column_int(stmt, 0);
        double avg = sqlite3_column_double(stmt, 1) * 100;
        int max = sqlite3_column_int(stmt, 2);
        int total = sqlite3_column_int(stmt, 3);
        long long time_min = (long long)(sqlite3_column_int64(stmt, 4) / 60);

        snprintf(response, sizeof(response),
            "USER_STATS|Tests:%d|AvgScore:%.2f|MaxScore:%d|TotalScore:%d|TotalTime:%lldm\n",
            tests, avg, max, total, time_min);
    } else {
        strcpy(response, "USER_STATS|Tests:0|AvgScore:0.00|MaxScore:0|TotalScore:0|TotalTime:0m\n");
    }

    send_response_safe(socket_fd, response);
    sqlite3_finalize(stmt);
}

/* =========================================================
   CATEGORY & DIFFICULTY (ACCURACY BY QUESTION)
   ========================================================= */

void get_category_stats(int socket_fd, int user_id)
{
    const char *sql =
      "SELECT q.category, "
      "COUNT(DISTINCT r.room_id), "
      "CASE WHEN COUNT(ua.id)=0 THEN 0 "
      "ELSE SUM(ua.selected_answer = q.correct_answer) * 1.0 / COUNT(ua.id) END "
      "FROM results r "
      "JOIN user_answers ua ON r.user_id=ua.user_id AND r.room_id=ua.room_id "
      "JOIN questions q ON ua.question_id=q.id "
      "WHERE r.user_id=? "
      "GROUP BY q.category;";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, user_id);

    char response[4096] = "CATEGORY_STATS|";

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char entry[256];
        snprintf(entry, sizeof(entry),
                 "%s|%d|%.1f%%|",
                 sqlite3_column_text(stmt, 0),
                 sqlite3_column_int(stmt, 1),
                 sqlite3_column_double(stmt, 2) * 100);
        if (append_safe(response, sizeof(response), entry) != 0)
            break;
    }

    append_safe(response, sizeof(response), "\n");
    send_response_safe(socket_fd, response);
    sqlite3_finalize(stmt);
}

void get_difficulty_stats(int socket_fd, int user_id)
{
    const char *sql =
        "SELECT q.difficulty, "
        "COUNT(DISTINCT r.room_id), "
        "CASE WHEN COUNT(ua.id)=0 THEN 0 "
        "ELSE SUM(ua.selected_answer = q.correct_answer) * 1.0 / COUNT(ua.id) END "
        "FROM results r "
        "JOIN user_answers ua "
        "  ON r.user_id = ua.user_id AND r.room_id = ua.room_id "
        "JOIN questions q ON ua.question_id = q.id "
        "WHERE r.user_id = ? "
        "GROUP BY q.difficulty;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "DB error: %s\n", sqlite3_errmsg(db));
        send(socket_fd, "DIFFICULTY_STATS_FAIL\n", 22, 0);
        return;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    char response[RESP_MED] = "DIFFICULTY_STATS|";

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char entry[RESP_SMALL];
        snprintf(entry, sizeof(entry),
            "%s|%d|%.1f%%|",
            SAFE_TEXT(sqlite3_column_text(stmt, 0)),
            sqlite3_column_int(stmt, 1),
            sqlite3_column_double(stmt, 2) * 100);

        if (append_safe(response, sizeof(response), entry) != 0)
            break;
    }

    append_safe(response, sizeof(response), "\n");
    send_response_safe(socket_fd, response);
    sqlite3_finalize(stmt);
}
