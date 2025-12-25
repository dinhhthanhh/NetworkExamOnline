#include "practice.h"
#include "db.h"
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

extern ServerData server_data;
extern sqlite3 *db;

void handle_begin_practice(int socket_fd, int user_id)
{
  char response[8192];
  char query[512];
  sqlite3_stmt *stmt;

  // Lấy 10 câu hỏi random từ database
  snprintf(query, sizeof(query),
           "SELECT id, question_text, option_a, option_b, option_c, option_d "
           "FROM questions ORDER BY RANDOM() LIMIT 10;");

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) != SQLITE_OK)
  {
    snprintf(response, sizeof(response), "ERROR|Cannot load practice questions\n");
    pthread_mutex_unlock(&server_data.lock);
    send(socket_fd, response, strlen(response), 0);
    return;
  }

  // Tạo session practice trong database
  char session_query[256];
  snprintf(session_query, sizeof(session_query),
           "INSERT INTO practice_sessions (user_id, start_time, duration_minutes) "
           "VALUES (%d, datetime('now'), 30);",
           user_id);
  
  if (sqlite3_exec(db, session_query, 0, 0, 0) != SQLITE_OK)
  {
    snprintf(response, sizeof(response), "ERROR|Cannot create practice session\n");
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    send(socket_fd, response, strlen(response), 0);
    return;
  }

  int session_id = (int)sqlite3_last_insert_rowid(db);

  // Build response: PRACTICE_OK|duration|q1_id:text:A:B:C:D|q2_id:...
  strcpy(response, "PRACTICE_OK|30");

  int question_count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && question_count < 10)
  {
    int q_id = sqlite3_column_int(stmt, 0);
    const char *text = (const char *)sqlite3_column_text(stmt, 1);
    const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
    const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
    const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
    const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);

    // Lưu question vào practice_questions
    char insert_q[512];
    snprintf(insert_q, sizeof(insert_q),
             "INSERT INTO practice_questions (session_id, question_id) VALUES (%d, %d);",
             session_id, q_id);
    sqlite3_exec(db, insert_q, 0, 0, 0);

    // Append vào response
    char q_data[1024];
    snprintf(q_data, sizeof(q_data), "|%d:%s:%s:%s:%s:%s",
             q_id, text, opt_a, opt_b, opt_c, opt_d);
    strcat(response, q_data);

    question_count++;
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);

  strcat(response, "\n");
  send(socket_fd, response, strlen(response), 0);

  printf("[PRACTICE] User %d started practice session %d with %d questions\n",
         user_id, session_id, question_count);
}

void save_practice_answer(int socket_fd, int user_id, int question_id, int answer)
{
  char query[512];
  char response[128];

  // Lấy session_id hiện tại
  snprintf(query, sizeof(query),
           "SELECT id FROM practice_sessions WHERE user_id=%d "
           "AND submitted=0 ORDER BY id DESC LIMIT 1;",
           user_id);

  sqlite3_stmt *stmt;
  pthread_mutex_lock(&server_data.lock);

  int session_id = -1;
  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      session_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  if (session_id == -1)
  {
    snprintf(response, sizeof(response), "ERROR|No active practice session\n");
    pthread_mutex_unlock(&server_data.lock);
    send(socket_fd, response, strlen(response), 0);
    return;
  }

  // Lưu/Update answer
  snprintf(query, sizeof(query),
           "INSERT OR REPLACE INTO practice_answers "
           "(session_id, question_id, user_answer) VALUES (%d, %d, %d);",
           session_id, question_id, answer);

  if (sqlite3_exec(db, query, 0, 0, 0) == SQLITE_OK)
  {
    snprintf(response, sizeof(response), "ANSWER_SAVED\n");
  }
  else
  {
    snprintf(response, sizeof(response), "ERROR|Cannot save answer\n");
  }

  pthread_mutex_unlock(&server_data.lock);
  send(socket_fd, response, strlen(response), 0);
}

void submit_practice(int socket_fd, int user_id)
{
  char query[512];
  char response[256];

  // Lấy session_id
  snprintf(query, sizeof(query),
           "SELECT id FROM practice_sessions WHERE user_id=%d "
           "AND submitted=0 ORDER BY id DESC LIMIT 1;",
           user_id);

  sqlite3_stmt *stmt;
  pthread_mutex_lock(&server_data.lock);

  int session_id = -1;
  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      session_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  if (session_id == -1)
  {
    snprintf(response, sizeof(response), "ERROR|No active practice session\n");
    pthread_mutex_unlock(&server_data.lock);
    send(socket_fd, response, strlen(response), 0);
    return;
  }

  // Tính điểm
  snprintf(query, sizeof(query),
           "SELECT COUNT(*) FROM practice_questions pq "
           "JOIN practice_answers pa ON pq.question_id = pa.question_id AND pq.session_id = pa.session_id "
           "JOIN questions q ON pq.question_id = q.id "
           "WHERE pq.session_id = %d AND pa.user_answer = q.correct_answer;",
           session_id);

  int score = 0;
  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      score = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  // Đếm tổng câu hỏi
  snprintf(query, sizeof(query),
           "SELECT COUNT(*) FROM practice_questions WHERE session_id=%d;",
           session_id);

  int total = 0;
  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  // Mark session as submitted
  snprintf(query, sizeof(query),
           "UPDATE practice_sessions SET submitted=1, score=%d, "
           "end_time=datetime('now') WHERE id=%d;",
           score, session_id);
  sqlite3_exec(db, query, 0, 0, 0);

  pthread_mutex_unlock(&server_data.lock);

  // Tính thời gian (giả sử 30 phút)
  int time_taken = 30;

  snprintf(response, sizeof(response), "PRACTICE_RESULT|%d|%d|%d\n",
           score, total, time_taken);
  send(socket_fd, response, strlen(response), 0);

  printf("[PRACTICE] User %d submitted session %d: %d/%d\n",
         user_id, session_id, score, total);
}