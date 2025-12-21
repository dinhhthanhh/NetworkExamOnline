#include "results.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

// Lưu đáp án realtime (INSERT OR REPLACE để update nếu đã tồn tại)
void save_answer(int socket_fd, int user_id, int room_id, int question_id, int selected_answer)
{
    pthread_mutex_lock(&server_data.lock);
    
    // Validate selected_answer (0-3 = A-D)
    if (selected_answer < 0 || selected_answer > 3) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Invalid answer\n", 33, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // INSERT OR REPLACE để update nếu user đổi đáp án
    char *query = sqlite3_mprintf(
        "INSERT OR REPLACE INTO user_answers (user_id, room_id, question_id, selected_answer, answered_at) "
        "VALUES (%d, %d, %d, %d, CURRENT_TIMESTAMP)",
        user_id, room_id, question_id, selected_answer);
    
    if (!query) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Memory error\n", 31, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, query, NULL, NULL, &err_msg);
    sqlite3_free(query);
    
    if (rc == SQLITE_OK) {
        send(socket_fd, "SAVE_ANSWER_OK\n", 15, 0);
        printf("[DEBUG] User %d answered Q%d = %d in room %d\n", 
               user_id, question_id, selected_answer, room_id);
    } else {
        char error[128];
        snprintf(error, sizeof(error), "SAVE_ANSWER_FAIL|%s\n", 
                 err_msg ? err_msg : "Database error");
        send(socket_fd, error, strlen(error), 0);
        if (err_msg) sqlite3_free(err_msg);
    }
    
    pthread_mutex_unlock(&server_data.lock);
}

void submit_answer(int socket_fd, int user_id, int room_id, int question_num, int answer)
{
  pthread_mutex_lock(&server_data.lock);

  TestRoom *room = &server_data.rooms[room_id];

  int user_idx = -1;
  for (int i = 0; i < room->participant_count; i++)
  {
    if (room->participants[i] == user_id)
    {
      user_idx = i;
      break;
    }
  }

  if (user_idx != -1)
  {
    room->answers[user_idx][question_num].user_id = user_id;
    room->answers[user_idx][question_num].answer = answer;
    room->answers[user_idx][question_num].submit_time = time(NULL);

    char response[] = "SUBMIT_ANSWER_OK\n";
    send(socket_fd, response, strlen(response), 0);
  }

  pthread_mutex_unlock(&server_data.lock);
}

void submit_test(int socket_fd, int user_id, int room_id)
{
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra user đã bắt đầu thi chưa
  char check_query[256];
  snprintf(check_query, sizeof(check_query),
           "SELECT start_time FROM participants WHERE room_id = %d AND user_id = %d",
           room_id, user_id);
  
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, check_query, -1, &stmt, NULL) != SQLITE_OK) {
      send(socket_fd, "SUBMIT_TEST_FAIL|Database error\n", 33, 0);
      pthread_mutex_unlock(&server_data.lock);
      return;
  }
  
  long start_time = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
      start_time = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  
  if (start_time == 0) {
      send(socket_fd, "SUBMIT_TEST_FAIL|Not started yet\n", 34, 0);
      pthread_mutex_unlock(&server_data.lock);
      return;
  }
  
  // Kiểm tra timeout
  time_t now = time(NULL);
  long elapsed = now - start_time;
  
  // Lấy duration của room
  char duration_query[256];
  snprintf(duration_query, sizeof(duration_query),
           "SELECT duration FROM rooms WHERE id = %d", room_id);
  
  int duration_minutes = 60;
  if (sqlite3_prepare_v2(db, duration_query, -1, &stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
          duration_minutes = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
  }
  
  long max_time = duration_minutes * 60;
  if (elapsed > max_time) {
      elapsed = max_time; // Cap ở max time
  }
  
  // Tính điểm: JOIN user_answers với questions để check correct_answer
  char score_query[512];
  snprintf(score_query, sizeof(score_query),
           "SELECT COUNT(*) FROM user_answers ua "
           "JOIN questions q ON ua.question_id = q.id "
           "WHERE ua.user_id = %d AND ua.room_id = %d "
           "AND ua.selected_answer = q.correct_answer",
           user_id, room_id);
  
  int score = 0;
  if (sqlite3_prepare_v2(db, score_query, -1, &stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
          score = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
  }
  
  // Đếm tổng số câu hỏi
  char count_query[256];
  snprintf(count_query, sizeof(count_query),
           "SELECT COUNT(*) FROM questions WHERE room_id = %d", room_id);
  
  int total_questions = 0;
  if (sqlite3_prepare_v2(db, count_query, -1, &stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
          total_questions = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
  }
  
  // Lưu kết quả vào results table
  char *insert_query = sqlite3_mprintf(
      "INSERT INTO results (user_id, room_id, score, total_questions, time_taken) "
      "VALUES (%d, %d, %d, %d, %ld)",
      user_id, room_id, score, total_questions, elapsed);
  
  char *err_msg = NULL;
  sqlite3_exec(db, insert_query, NULL, NULL, &err_msg);
  sqlite3_free(insert_query);
  
  if (err_msg) {
      sqlite3_free(err_msg);
  }

  char response[200];
  snprintf(response, sizeof(response), "SUBMIT_TEST_OK|%d|%d|%ld\n", 
           score, total_questions, elapsed/60);
  send(socket_fd, response, strlen(response), 0);

  log_activity(user_id, "SUBMIT_TEST", "Test submitted");
  printf("[INFO] User %d submitted test - Score: %d/%d\n", user_id, score, total_questions);
  
  pthread_mutex_unlock(&server_data.lock);
}

void view_results(int socket_fd, int room_id)
{
  pthread_mutex_lock(&server_data.lock);

  TestRoom *room = &server_data.rooms[room_id];
  char response[2048];
  strcpy(response, "VIEW_RESULTS|");

  for (int i = 0; i < room->participant_count; i++)
  {
    char result_info[200];
    snprintf(result_info, sizeof(result_info), "User %d: %d/%d|",
             room->participants[i], room->scores[i], room->num_questions);
    strcat(response, result_info);
  }

  strcat(response, "\n");
  send(socket_fd, response, strlen(response), 0);
  pthread_mutex_unlock(&server_data.lock);
}

// Auto-submit khi user disconnect (được gọi từ handle_client khi detect disconnect)
void auto_submit_on_disconnect(int user_id, int room_id)
{
  pthread_mutex_lock(&server_data.lock);
  
  printf("[INFO] Auto-submit for user %d in room %d (disconnect detected)\n", user_id, room_id);
  
  // Kiểm tra user đã bắt đầu thi chưa
  char check_query[256];
  snprintf(check_query, sizeof(check_query),
           "SELECT start_time FROM participants WHERE room_id = %d AND user_id = %d",
           room_id, user_id);
  
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, check_query, -1, &stmt, NULL) != SQLITE_OK) {
      pthread_mutex_unlock(&server_data.lock);
      return;
  }
  
  long start_time = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
      start_time = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  
  if (start_time == 0) {
      pthread_mutex_unlock(&server_data.lock);
      return; // Chưa bắt đầu thi
  }
  
  // Kiểm tra đã submit chưa
  char check_result[256];
  snprintf(check_result, sizeof(check_result),
           "SELECT id FROM results WHERE room_id = %d AND user_id = %d",
           room_id, user_id);
  
  if (sqlite3_prepare_v2(db, check_result, -1, &stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
          sqlite3_finalize(stmt);
          pthread_mutex_unlock(&server_data.lock);
          return; // Đã submit rồi
      }
      sqlite3_finalize(stmt);
  }
  
  // Tính điểm từ câu trả lời đã lưu
  time_t now = time(NULL);
  long elapsed = now - start_time;
  
  // Lấy duration
  char duration_query[256];
  snprintf(duration_query, sizeof(duration_query),
           "SELECT duration FROM rooms WHERE id = %d", room_id);
  
  int duration_minutes = 60;
  if (sqlite3_prepare_v2(db, duration_query, -1, &stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
          duration_minutes = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
  }
  
  // Tính điểm
  char score_query[512];
  snprintf(score_query, sizeof(score_query),
           "SELECT COUNT(*) FROM user_answers ua "
           "JOIN questions q ON ua.question_id = q.id "
           "WHERE ua.user_id = %d AND ua.room_id = %d "
           "AND ua.selected_answer = q.correct_answer",
           user_id, room_id);
  
  int score = 0;
  if (sqlite3_prepare_v2(db, score_query, -1, &stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
          score = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
  }
  
  // Đếm tổng câu hỏi
  char count_query[256];
  snprintf(count_query, sizeof(count_query),
           "SELECT COUNT(*) FROM questions WHERE room_id = %d", room_id);
  
  int total_questions = 0;
  if (sqlite3_prepare_v2(db, count_query, -1, &stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
          total_questions = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
  }
  
  // Lưu kết quả
  char *insert_query = sqlite3_mprintf(
      "INSERT INTO results (user_id, room_id, score, total_questions, time_taken) "
      "VALUES (%d, %d, %d, %d, %ld)",
      user_id, room_id, score, total_questions, elapsed);
  
  char *err_msg = NULL;
  sqlite3_exec(db, insert_query, NULL, NULL, &err_msg);
  sqlite3_free(insert_query);
  
  if (err_msg) {
      sqlite3_free(err_msg);
  }
  
  log_activity(user_id, "AUTO_SUBMIT", "Test auto-submitted on disconnect");
  printf("[INFO] Auto-submitted for user %d - Score: %d/%d\n", user_id, score, total_questions);
  
  pthread_mutex_unlock(&server_data.lock);
}
