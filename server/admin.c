#include "admin.h"
#include "db.h"
#include <sys/socket.h>
#include <time.h>

extern ServerData server_data;
extern sqlite3 *db;

void get_admin_dashboard(int socket_fd, int admin_id)
{
  char query[500];
  sqlite3_stmt *stmt;

  // Get total users, rooms, questions, and total tests
  strcpy(query, "SELECT COUNT(*) FROM users;");

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    int total_users = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      total_users = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    // Get total tests completed
    strcpy(query, "SELECT COUNT(*) FROM results;");
    if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
    {
      int total_tests = 0;
      if (sqlite3_step(stmt) == SQLITE_ROW)
      {
        total_tests = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);

      // Build response
      char response[500];
      snprintf(response, sizeof(response),
               "ADMIN_DASHBOARD|Users:%d|Rooms:%d|Questions:%d|TotalTests:%d|OnlineUsers:%d\n",
               total_users, server_data.room_count, server_data.question_count,
               total_tests, server_data.user_count);
      send(socket_fd, response, strlen(response), 0);
    }
  }

  pthread_mutex_unlock(&server_data.lock);
}

void manage_users(int socket_fd, int admin_id)
{
  pthread_mutex_lock(&server_data.lock);

  char response[4096];
  strcpy(response, "USER_LIST|");

  for (int i = 0; i < server_data.user_count; i++)
  {
    char user_info[200];
    snprintf(user_info, sizeof(user_info), "ID:%d|%s|Online:%d|",
             server_data.users[i].user_id,
             server_data.users[i].username,
             server_data.users[i].is_online);
    strcat(response, user_info);
  }

  strcat(response, "\n");
  send(socket_fd, response, strlen(response), 0);

  pthread_mutex_unlock(&server_data.lock);
}

void manage_questions(int socket_fd, int admin_id)
{
  pthread_mutex_lock(&server_data.lock);

  char response[8192];
  strcpy(response, "QUESTION_LIST|");

  for (int i = 0; i < server_data.question_count && i < 100; i++)
  {
    Question *q = &server_data.questions[i];
    char q_info[300];
    snprintf(q_info, sizeof(q_info), "ID:%d|Q:%s|Diff:%s|Cat:%s|",
             q->id, q->text, q->difficulty, q->category);
    strcat(response, q_info);
  }

  strcat(response, "\n");
  send(socket_fd, response, strlen(response), 0);

  pthread_mutex_unlock(&server_data.lock);
}

void get_system_stats(int socket_fd, int admin_id)
{
  char query[800];
  sqlite3_stmt *stmt;

  // Get average score
  strcpy(query, "SELECT AVG(CAST(score AS FLOAT)/total_questions) FROM results;");

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    double avg_score = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      avg_score = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);

    // Get most popular category
    strcpy(query, "SELECT category, COUNT(*) as cnt FROM questions GROUP BY category ORDER BY cnt DESC LIMIT 1;");
    if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
    {
      const char *popular_cat = "N/A";
      if (sqlite3_step(stmt) == SQLITE_ROW)
      {
        popular_cat = (const char *)sqlite3_column_text(stmt, 0);
      }
      sqlite3_finalize(stmt);

      char response[400];
      snprintf(response, sizeof(response),
               "SYSTEM_STATS|AvgScore:%.2f%%|PopularCategory:%s|TotalQuestions:%d\n",
               avg_score * 100, popular_cat, server_data.question_count);
      send(socket_fd, response, strlen(response), 0);
    }
  }

  pthread_mutex_unlock(&server_data.lock);
}

void ban_user(int socket_fd, int admin_id, int target_user_id)
{
  pthread_mutex_lock(&server_data.lock);

  // Mark user as banned in database
  char query[300];
  snprintf(query, sizeof(query), "DELETE FROM users WHERE id = %d;", target_user_id);

  char *err_msg = 0;
  if (sqlite3_exec(db, query, 0, 0, &err_msg) == SQLITE_OK)
  {
    // Remove from in-memory
    for (int i = 0; i < server_data.user_count; i++)
    {
      if (server_data.users[i].user_id == target_user_id)
      {
        // Shift array
        for (int j = i; j < server_data.user_count - 1; j++)
        {
          server_data.users[j] = server_data.users[j + 1];
        }
        server_data.user_count--;
        break;
      }
    }

    char response[] = "BAN_USER_OK\n";
    send(socket_fd, response, strlen(response), 0);
    log_activity(admin_id, "BAN_USER", "Banned user");
  }
  else
  {
    char response[] = "BAN_USER_FAIL\n";
    send(socket_fd, response, strlen(response), 0);
  }

  pthread_mutex_unlock(&server_data.lock);
}

void delete_question(int socket_fd, int admin_id, int question_id)
{
  pthread_mutex_lock(&server_data.lock);

  char query[300];
  snprintf(query, sizeof(query), "DELETE FROM questions WHERE id = %d;", question_id);

  char *err_msg = 0;
  if (sqlite3_exec(db, query, 0, 0, &err_msg) == SQLITE_OK)
  {
    // Remove from in-memory
    for (int i = 0; i < server_data.question_count; i++)
    {
      if (server_data.questions[i].id == question_id)
      {
        for (int j = i; j < server_data.question_count - 1; j++)
        {
          server_data.questions[j] = server_data.questions[j + 1];
        }
        server_data.question_count--;
        break;
      }
    }

    char response[] = "DELETE_QUESTION_OK\n";
    send(socket_fd, response, strlen(response), 0);
    log_activity(admin_id, "DELETE_QUESTION", "Deleted question");
  }
  else
  {
    char response[] = "DELETE_QUESTION_FAIL\n";
    send(socket_fd, response, strlen(response), 0);
  }

  pthread_mutex_unlock(&server_data.lock);
}
