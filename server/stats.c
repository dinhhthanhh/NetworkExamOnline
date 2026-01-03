#include "stats.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

void get_leaderboard(int socket_fd, int limit)
{
  char query[500];
  sqlite3_stmt *stmt;

  // Chỉ lấy user có role = 'user' (không lấy admin)
  snprintf(query, sizeof(query),
           "SELECT u.id, u.username, COALESCE(SUM(r.score), 0) as total_score, COUNT(r.id) as tests_completed "
           "FROM users u LEFT JOIN results r ON u.id = r.user_id "
           "WHERE u.role = 'user' "
           "GROUP BY u.id ORDER BY total_score DESC LIMIT %d;",
           limit);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    char response[4096];
    strcpy(response, "LEADERBOARD|");

    int rank = 1;
    int display_rank = 1;
    int prev_score = -1;
    
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      // int user_id = sqlite3_column_int(stmt, 0);  // Unused - commented out
      const char *username = (const char *)sqlite3_column_text(stmt, 1);
      int total_score = sqlite3_column_int(stmt, 2);
      int tests_completed = sqlite3_column_int(stmt, 3);

      // If score is same as previous, use same display_rank
      if (total_score != prev_score) {
        display_rank = rank;
        prev_score = total_score;
      }

      char entry[300];
      snprintf(entry, sizeof(entry), "%d|%s|Score:%d|Tests:%d|",
               display_rank, username, total_score, tests_completed);
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

// Get detailed statistics for a specific room (for admin)
void get_room_statistics(int socket_fd, int room_id, int requesting_user_id)
{
  pthread_mutex_lock(&server_data.lock);

  // Verify user is admin or room owner
  sqlite3_stmt *stmt;
  const char *check_sql = "SELECT host_id FROM rooms WHERE id = ?;";
  
  if (sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL) != SQLITE_OK) {
    char response[] = "ROOM_STATS_ERROR|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    char response[] = "ROOM_STATS_ERROR|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int host_id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  // Check if requesting user is the host
  if (host_id != requesting_user_id) {
    char response[] = "ROOM_STATS_ERROR|Permission denied\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  char response[16384];
  int offset = 0;

  // Room header info
  offset += snprintf(response + offset, sizeof(response) - offset, "ROOM_STATS|%d|", room_id);

  // Get participants and their status
  // Status: 0=Not started, 1=Taking exam (in participants), 2=Submitted
  const char *participants_sql = 
    "SELECT "
    "  u.id, "
    "  u.username, "
    "  p.start_time, "
    "  r.score, "
    "  r.total_questions, "
    "  r.completed_at, "
    "  CASE "
    "    WHEN r.id IS NOT NULL THEN 'Submitted' "
    "    WHEN p.start_time > 0 THEN 'Taking' "
    "    ELSE 'Waiting' "
    "  END as status "
    "FROM users u "
    "LEFT JOIN participants p ON u.id = p.user_id AND p.room_id = ? "
    "LEFT JOIN results r ON u.id = r.user_id AND r.room_id = ? "
    "WHERE u.id IN ("
    "  SELECT user_id FROM participants WHERE room_id = ? "
    "  UNION "
    "  SELECT user_id FROM results WHERE room_id = ?"
    ") "
    "ORDER BY "
    "  CASE "
    "    WHEN r.id IS NOT NULL THEN 0 "
    "    WHEN p.start_time > 0 THEN 1 "
    "    ELSE 2 "
    "  END, "
    "  u.username;";

  if (sqlite3_prepare_v2(db, participants_sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, room_id);
    sqlite3_bind_int(stmt, 3, room_id);
    sqlite3_bind_int(stmt, 4, room_id);

    int total_participants = 0;
    int currently_taking = 0;
    int submitted = 0;
    double total_score = 0.0;
    int scored_count = 0;

    // Participants data
    offset += snprintf(response + offset, sizeof(response) - offset, "PARTICIPANTS|");

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int user_id = sqlite3_column_int(stmt, 0);
      const char *username = (const char *)sqlite3_column_text(stmt, 1);
      int score = sqlite3_column_int(stmt, 3);
      int total_questions = sqlite3_column_int(stmt, 4);
      const char *completed_at = (const char *)sqlite3_column_text(stmt, 5);
      const char *status = (const char *)sqlite3_column_text(stmt, 6);

      total_participants++;
      
      if (strcmp(status, "Taking") == 0) {
        currently_taking++;
      } else if (strcmp(status, "Submitted") == 0) {
        submitted++;
        if (total_questions > 0) {
          total_score += (score * 100.0 / total_questions);
          scored_count++;
        }
      }

      // Format: user_id:username:status:score:total_questions:completed_at|
      offset += snprintf(response + offset, sizeof(response) - offset,
                        "%d:%s:%s:%d:%d:%s|",
                        user_id, username, status, 
                        score, total_questions,
                        completed_at ? completed_at : "N/A");
    }

    sqlite3_finalize(stmt);

    // Calculate average score
    double avg_score = (scored_count > 0) ? (total_score / scored_count) : 0.0;

    // Add summary statistics
    offset += snprintf(response + offset, sizeof(response) - offset,
                      "SUMMARY|Total:%d|Taking:%d|Submitted:%d|AvgScore:%.2f\n",
                      total_participants, currently_taking, submitted, avg_score);

    send(socket_fd, response, offset, 0);
  } else {
    char error_response[] = "ROOM_STATS_ERROR|Failed to query participants\n";
    send(socket_fd, error_response, strlen(error_response), 0);
  }

  pthread_mutex_unlock(&server_data.lock);
}
