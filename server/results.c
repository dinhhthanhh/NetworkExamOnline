#include "results.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

// Helper: Tìm room index trong server_data.rooms[]
static int find_room_index(int room_id) {
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            return i;
        }
    }
    return -1;
}

// Helper: Tìm user index trong room->participants[]
static int find_participant_index(TestRoom *room, int user_id) {
    for (int i = 0; i < room->participant_count; i++) {
        if (room->participants[i] == user_id) {
            return i;
        }
    }
    return -1;
}

// Helper: Ghi toàn bộ đáp án của 1 user vào DB (batch write)
static void flush_answers_to_db(int user_id, int room_id, TestRoom *room, int user_idx) {
    printf("[FLUSH] Starting flush for user %d in room %d (user_idx=%d)\n", 
           user_id, room_id, user_idx);
    
    // Xóa các đáp án cũ trong DB
    char delete_query[256];
    snprintf(delete_query, sizeof(delete_query),
             "DELETE FROM user_answers WHERE user_id = %d AND room_id = %d",
             user_id, room_id);
    sqlite3_exec(db, delete_query, NULL, NULL, NULL);
    
    // Insert tất cả đáp án mới (batch)
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (int q = 0; q < MAX_QUESTIONS; q++) {
        UserAnswer *ans = &room->answers[user_idx][q];
        if (ans->answer >= 0 && ans->answer <= 3) {  // Có đáp án hợp lệ
            // Lấy question_id thực tế từ DB
            char get_qid_query[256];
            snprintf(get_qid_query, sizeof(get_qid_query),
                     "SELECT id FROM questions WHERE room_id = %d LIMIT 1 OFFSET %d",
                     room_id, q);
            
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(db, get_qid_query, -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    int question_id = sqlite3_column_int(stmt, 0);
                    
                    char insert_query[512];
                    snprintf(insert_query, sizeof(insert_query),
                             "INSERT INTO user_answers (user_id, room_id, question_id, selected_answer, answered_at) "
                             "VALUES (%d, %d, %d, %d, %ld)",
                             user_id, room_id, question_id, ans->answer, ans->submit_time);
                    sqlite3_exec(db, insert_query, NULL, NULL, NULL);
                }
                sqlite3_finalize(stmt);
            }
        }
    }
    
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    printf("[FLUSH] Saved answers for user %d in room %d to DB\n", user_id, room_id);
}

// Lưu đáp án vào IN-MEMORY (thay vì ghi trực tiếp DB)
void save_answer(int socket_fd, int user_id, int room_id, int question_id, int selected_answer)
{
    pthread_mutex_lock(&server_data.lock);
    
    // Validate selected_answer (0-3 = A-D)
    if (selected_answer < 0 || selected_answer > 3) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Invalid answer\n", 33, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Tìm room trong in-memory structure
    int room_idx = find_room_index(room_id);
    if (room_idx == -1) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Room not found\n", 34, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    TestRoom *room = &server_data.rooms[room_idx];
    
    // Tìm user trong participants
    int user_idx = find_participant_index(room, user_id);
    if (user_idx == -1) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Not a participant\n", 37, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Tìm question index (question_id → index trong mảng)
    char get_q_index[256];
    snprintf(get_q_index, sizeof(get_q_index),
             "SELECT COUNT(*) - 1 FROM questions WHERE room_id = %d AND id <= %d",
             room_id, question_id);
    
    sqlite3_stmt *stmt;
    int question_idx = -1;
    if (sqlite3_prepare_v2(db, get_q_index, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            question_idx = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    if (question_idx < 0 || question_idx >= MAX_QUESTIONS) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Invalid question\n", 36, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // **LƯU VÀO IN-MEMORY**
    room->answers[user_idx][question_idx].user_id = user_id;
    room->answers[user_idx][question_idx].answer = selected_answer;
    room->answers[user_idx][question_idx].submit_time = time(NULL);
    
    send(socket_fd, "SAVE_ANSWER_OK\n", 16, 0);
    
    printf("[MEMORY] User %d answered Q%d = %d in room %d (in-memory)\n", 
           user_id, question_idx, selected_answer, room_id);
    
    // **AUTO-SAVE mỗi 5 câu hoặc câu cuối**
    int answered_count = 0;
    for (int i = 0; i < MAX_QUESTIONS; i++) {
        if (room->answers[user_idx][i].answer >= 0) {
            answered_count++;
        }
    }
    
    if (answered_count % 5 == 0 || answered_count == room->num_questions) {
        flush_answers_to_db(user_id, room_id, room, user_idx);
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

  // **FLUSH tất cả đáp án từ in-memory vào DB trước khi tính điểm**
  int room_idx = find_room_index(room_id);
  if (room_idx != -1) {
      TestRoom *room = &server_data.rooms[room_idx];
      int user_idx = find_participant_index(room, user_id);
      if (user_idx != -1) {
          flush_answers_to_db(user_id, room_id, room, user_idx);
      }
  }

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

// Public wrapper để flush answers (dùng cho external calls)
void flush_user_answers(int user_id, int room_id) {
    pthread_mutex_lock(&server_data.lock);
    
    printf("[FLUSH] flush_user_answers called: user=%d, room=%d, room_count=%d\n", 
           user_id, room_id, server_data.room_count);
    
    int room_idx = find_room_index(room_id);
    if (room_idx != -1) {
        TestRoom *room = &server_data.rooms[room_idx];
        printf("[FLUSH] Found room at idx=%d, participant_count=%d\n", 
               room_idx, room->participant_count);
        
        int user_idx = find_participant_index(room, user_id);
        if (user_idx != -1) {
            printf("[FLUSH] Found user at idx=%d, calling flush_answers_to_db\n", user_idx);
            flush_answers_to_db(user_id, room_id, room, user_idx);
        } else {
            printf("[FLUSH] ERROR: User %d not found in room participants\n", user_id);
        }
    } else {
        printf("[FLUSH] ERROR: Room %d not found in in-memory (room_count=%d)\n", 
               room_id, server_data.room_count);
        // List all rooms for debugging
        for (int i = 0; i < server_data.room_count; i++) {
            printf("[FLUSH]   Room[%d]: id=%d, participants=%d\n", 
                   i, server_data.rooms[i].room_id, server_data.rooms[i].participant_count);
        }
    }
    
    pthread_mutex_unlock(&server_data.lock);
}
