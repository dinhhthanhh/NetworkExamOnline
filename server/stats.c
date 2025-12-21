#include "stats.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

void get_leaderboard(int socket_fd, int limit)
{
  char query[500];
  sqlite3_stmt *stmt;

  snprintf(query, sizeof(query),
           "SELECT u.id, u.username, COALESCE(SUM(r.score), 0) as total_score, COUNT(r.id) as tests_completed "
           "FROM users u LEFT JOIN results r ON u.id = r.user_id "
           "GROUP BY u.id ORDER BY total_score DESC LIMIT %d;",
           limit);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    char response[4096];
    strcpy(response, "LEADERBOARD|");

    int rank = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      int user_id = sqlite3_column_int(stmt, 0);
      const char *username = (const char *)sqlite3_column_text(stmt, 1);
      int total_score = sqlite3_column_int(stmt, 2);
      int tests_completed = sqlite3_column_int(stmt, 3);

      char entry[300];
      snprintf(entry, sizeof(entry), "#%d|%s|Score:%d|Tests:%d|",
               rank, username, total_score, tests_completed);
      strcat(response, entry);
      rank++;
    }

    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);
}

void get_user_statistics(int socket_fd, int user_id)
{
  char query[500];
  sqlite3_stmt *stmt;

  snprintf(query, sizeof(query),
           "SELECT COUNT(id) as total_tests, AVG(CAST(score AS FLOAT)/total_questions) as avg_score, "
           "MAX(score) as max_score, SUM(score) as total_score "
           "FROM results WHERE user_id = %d;",
           user_id);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      int total_tests = sqlite3_column_int(stmt, 0);
      double avg_score = sqlite3_column_double(stmt, 1);
      int max_score = sqlite3_column_int(stmt, 2);
      int total_score = sqlite3_column_int(stmt, 3);

      char response[300];
      snprintf(response, sizeof(response),
               "USER_STATS|Tests:%d|AvgScore:%.2f%%|MaxScore:%d|TotalScore:%d\n",
               total_tests, avg_score * 100, max_score, total_score);
      send(socket_fd, response, strlen(response), 0);
    }
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);
}

void get_category_stats(int socket_fd, int user_id)
{
  char query[800];
  sqlite3_stmt *stmt;

  snprintf(query, sizeof(query),
           "SELECT q.category, COUNT(DISTINCT r.id) as tests, "
           "SUM(CASE WHEN r.score > 0 THEN 1 ELSE 0 END) as passed "
           "FROM results r JOIN questions q ON r.room_id = q.id "
           "WHERE r.user_id = %d GROUP BY q.category;",
           user_id);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    char response[2048];
    strcpy(response, "CATEGORY_STATS|");

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      const char *category = (const char *)sqlite3_column_text(stmt, 0);
      int tests = sqlite3_column_int(stmt, 1);
      int passed = sqlite3_column_int(stmt, 2);

      char entry[200];
      snprintf(entry, sizeof(entry), "%s:%d/%d|", category, passed, tests);
      strcat(response, entry);
    }

    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);
}

void get_difficulty_stats(int socket_fd, int user_id)
{
  char query[800];
  sqlite3_stmt *stmt;

  snprintf(query, sizeof(query),
           "SELECT q.difficulty, COUNT(DISTINCT r.id) as tests, "
           "AVG(CAST(r.score AS FLOAT)/r.total_questions) as pass_rate "
           "FROM results r JOIN questions q ON r.room_id = q.id "
           "WHERE r.user_id = %d GROUP BY q.difficulty;",
           user_id);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    char response[2048];
    strcpy(response, "DIFFICULTY_STATS|");

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      const char *difficulty = (const char *)sqlite3_column_text(stmt, 0);
      int tests = sqlite3_column_int(stmt, 1);
      double pass_rate = sqlite3_column_double(stmt, 2);

      char entry[200];
      snprintf(entry, sizeof(entry), "%s:%d:%.1f%%|", difficulty, tests, pass_rate * 100);
      strcat(response, entry);
    }

    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);
}

void get_user_test_history(int socket_fd, int user_id)
{
  char query[800];
  sqlite3_stmt *stmt;

  snprintf(query, sizeof(query),
           "SELECT r.id, rm.name, r.score, r.total_questions, "
           "r.time_taken, datetime(r.completed_at, 'localtime') "
           "FROM results r "
           "JOIN rooms rm ON r.room_id = rm.id "
           "WHERE r.user_id = %d "
           "ORDER BY r.completed_at DESC LIMIT 20;",
           user_id);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    char response[8192];
    strcpy(response, "TEST_HISTORY|");

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      int result_id = sqlite3_column_int(stmt, 0);
      const char *room_name = (const char *)sqlite3_column_text(stmt, 1);
      int score = sqlite3_column_int(stmt, 2);
      int total = sqlite3_column_int(stmt, 3);
      int time_taken = sqlite3_column_int(stmt, 4);
      const char *completed = (const char *)sqlite3_column_text(stmt, 5);

      char entry[400];
      snprintf(entry, sizeof(entry), "%d|%s|%d|%d|%d|%s|",
               result_id, room_name, score, total, time_taken, completed ? completed : "N/A");
      strcat(response, entry);
    }

    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);
}
