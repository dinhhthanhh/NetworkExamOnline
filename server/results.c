#include "results.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

/*
 * Helper nội bộ: tìm index của room trong mảng server_data.rooms
 * theo room_id thật lấy từ DB.
 */
static int find_room_index(int room_id) {
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            return i;
        }
    }
    return -1;
}

/*
 * Helper nội bộ: tìm vị trí của một user trong danh sách participants
 * của một TestRoom.
 */
static int find_participant_index(TestRoom *room, int user_id) {
    for (int i = 0; i < room->participant_count; i++) {
        if (room->participants[i] == user_id) {
            return i;
        }
    }
    return -1;
}

/*
 * Helper: ghi toàn bộ đáp án đang lưu in-memory của một user trong room
 * xuống bảng exam_answers theo dạng batch (xóa cũ, thêm mới trong transaction).
 */
static void flush_answers_to_db(int user_id, int room_id, TestRoom *room, int user_idx) {
    // Xóa các đáp án cũ trong DB
    char delete_query[256];
    snprintf(delete_query, sizeof(delete_query),
             "DELETE FROM exam_answers WHERE user_id = %d AND room_id = %d",
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
                     "SELECT id FROM exam_questions WHERE room_id = %d AND is_selected = 1 LIMIT 1 OFFSET %d",
                     room_id, q);
            
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(db, get_qid_query, -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    int question_id = sqlite3_column_int(stmt, 0);
                    
                    char insert_query[512];
                    snprintf(insert_query, sizeof(insert_query),
                             "INSERT INTO exam_answers (user_id, room_id, question_id, selected_answer, answered_at) "
                             "VALUES (%d, %d, %d, %d, %ld)",
                             user_id, room_id, question_id, ans->answer, ans->submit_time);
                    sqlite3_exec(db, insert_query, NULL, NULL, NULL);
                }
                sqlite3_finalize(stmt);
            }
        }
    }
    
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
}

/*
 * Lưu tạm thời đáp án vào bộ nhớ RAM cho một câu hỏi:
 *  - Validate answer và quyền tham gia phòng
 *  - Map question_id thực sang index trong mảng
 *  - Tự động flush xuống DB mỗi 5 câu hoặc khi làm xong toàn bộ.
 */
void save_answer(int socket_fd, int user_id, int room_id, int question_id, int selected_answer)
{
    pthread_mutex_lock(&server_data.lock);
    
    // Validate selected_answer (0-3 = A-D)
    if (selected_answer < 0 || selected_answer > 3) {
        server_send(socket_fd, "SAVE_ANSWER_FAIL|Invalid answer\n");
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Tìm room trong in-memory structure
    int room_idx = find_room_index(room_id);
    if (room_idx == -1) {
        server_send(socket_fd, "SAVE_ANSWER_FAIL|Room not found\n");
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    TestRoom *room = &server_data.rooms[room_idx];
    
    // Tìm user trong participants
    int user_idx = find_participant_index(room, user_id);
    if (user_idx == -1) {
        server_send(socket_fd, "SAVE_ANSWER_FAIL|Not a participant\n");
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Tìm question index (question_id → index trong mảng)
    char get_q_index[256];
    snprintf(get_q_index, sizeof(get_q_index),
             "SELECT COUNT(*) - 1 FROM exam_questions WHERE room_id = %d AND is_selected = 1 AND id <= %d",
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
        server_send(socket_fd, "SAVE_ANSWER_FAIL|Invalid question\n");
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // **LƯU VÀO IN-MEMORY**
    room->answers[user_idx][question_idx].user_id = user_id;
    room->answers[user_idx][question_idx].answer = selected_answer;
    room->answers[user_idx][question_idx].submit_time = time(NULL);
    
    server_send(socket_fd, "SAVE_ANSWER_OK\n");
    
    // **LUÔN FLUSH VÀO DB NGAY (để tránh mất data khi disconnect)**
    flush_answers_to_db(user_id, room_id, room, user_idx);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Submit đáp án theo số thứ tự câu hỏi (logic cũ, vẫn giữ để tương thích):
 *  - Ghi trực tiếp vào mảng room->answers[room_id][question_num].
 */
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
    server_send(socket_fd, response);
  }

  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Người dùng nộp bài thi:
 *  - Flush toàn bộ đáp án in-memory xuống DB
 *  - Kiểm tra đã bắt đầu thi, kiểm tra hết giờ
 *  - Tính điểm từ exam_answers vs exam_questions và lưu vào results
 *  - Đánh dấu has_taken_exam để không được thi lại.
 */
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
    server_send(socket_fd, "SUBMIT_TEST_FAIL|Database error\n");
      pthread_mutex_unlock(&server_data.lock);
      return;
  }
  
  long start_time = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
      start_time = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  
  if (start_time == 0) {
    server_send(socket_fd, "SUBMIT_TEST_FAIL|Not started yet\n");
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
  
  // Tính điểm: JOIN exam_answers với exam_questions để check correct_answer
  char score_query[512];
  snprintf(score_query, sizeof(score_query),
           "SELECT COUNT(*) FROM exam_answers ua "
           "JOIN exam_questions q ON ua.question_id = q.id "
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
                     "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d AND is_selected = 1", room_id);
  
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

  // ===== CẬP NHẬT HAS_TAKEN_EXAM = 1 (LOGIC MỚI) =====
  char update_taken_query[256];
  snprintf(update_taken_query, sizeof(update_taken_query),
           "UPDATE participants SET has_taken_exam = 1 "
           "WHERE user_id = %d AND room_id = %d",
           user_id, room_id);
  sqlite3_exec(db, update_taken_query, NULL, NULL, NULL);

  // Đồng bộ thêm với bảng room_participants (nếu tồn tại) để chặn JOIN_ROOM thi lại
  snprintf(update_taken_query, sizeof(update_taken_query),
           "UPDATE room_participants SET has_taken_exam = 1 "
           "WHERE user_id = %d AND room_id = %d",
           user_id, room_id);
  sqlite3_exec(db, update_taken_query, NULL, NULL, NULL);

  char response[200];
  snprintf(response, sizeof(response), "SUBMIT_TEST_OK|%d|%d|%ld\n", 
           score, total_questions, elapsed/60);
    server_send(socket_fd, response);

  log_activity(user_id, "SUBMIT_TEST", "Test submitted");
  
  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Trả về kết quả tổng quan của phòng thi từ mảng in-memory TestRoom:
 *  - Liệt kê từng user, điểm đạt được và tổng số câu hỏi.
 */
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
    server_send(socket_fd, response);
  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Auto-submit khi user disconnect hoặc hết thời gian (dùng trong rooms/timer):
 *  - Giả định lock đã được giữ bởi caller
 *  - Nếu chưa submit thì tính điểm từ exam_answers và lưu vào results
 *  - Đánh dấu has_taken_exam ở cả participants và room_participants.
 */
void auto_submit_on_disconnect(int user_id, int room_id)
{
  
  // Kiểm tra user đã bắt đầu thi chưa
  char check_query[256];
  snprintf(check_query, sizeof(check_query),
           "SELECT start_time FROM participants WHERE room_id = %d AND user_id = %d",
           room_id, user_id);
  
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, check_query, -1, &stmt, NULL) != SQLITE_OK) {
      return;
  }
  
  long start_time = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
      start_time = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  
  if (start_time == 0) {
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
          return; // Đã submit rồi
      }
      sqlite3_finalize(stmt);
  }
  
  // Tính điểm từ câu trả lời đã lưu
  time_t now = time(NULL);
  long elapsed = now - start_time;
  

  // Tính điểm
  char score_query[512];
  snprintf(score_query, sizeof(score_query),
           "SELECT COUNT(*) FROM exam_answers ua "
           "JOIN exam_questions q ON ua.question_id = q.id "
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
                     "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d AND is_selected = 1", room_id);
  
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
      fprintf(stderr, "[AUTO_SUBMIT] Error inserting result: %s\n", err_msg);
      sqlite3_free(err_msg);
  } else {
      // Mark user as taken exam
      char mark_taken[256];
      snprintf(mark_taken, sizeof(mark_taken),
               "UPDATE room_participants SET has_taken_exam = 1 "
               "WHERE user_id = %d AND room_id = %d",
               user_id, room_id);
      sqlite3_exec(db, mark_taken, NULL, NULL, NULL);
      
      printf("[AUTO_SUBMIT] User %d auto-submitted - Score: %d/%d\n", 
             user_id, score, total_questions);
  }

    // Đánh dấu đã thi trong cả participants và room_participants để chặn JOIN_ROOM thi lại
    char update_taken_query[256];
    snprintf(update_taken_query, sizeof(update_taken_query),
                     "UPDATE participants SET has_taken_exam = 1 "
                     "WHERE user_id = %d AND room_id = %d",
                     user_id, room_id);
    sqlite3_exec(db, update_taken_query, NULL, NULL, NULL);

    snprintf(update_taken_query, sizeof(update_taken_query),
                     "UPDATE room_participants SET has_taken_exam = 1 "
                     "WHERE user_id = %d AND room_id = %d",
                     user_id, room_id);
    sqlite3_exec(db, update_taken_query, NULL, NULL, NULL);
  
<<<<<<< HEAD
  log_activity(user_id, "AUTO_SUBMIT", "Test auto-submitted on timeout");
  
  pthread_mutex_unlock(&server_data.lock);
=======
    log_activity(user_id, "AUTO_SUBMIT", "Test auto-submitted on disconnect");
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
}

/*
 * Hàm public dùng cho module khác (ví dụ network) để ép flush toàn bộ
 * đáp án của một user trong một phòng xuống DB exam_answers.
 */
void flush_user_answers(int user_id, int room_id) {
    pthread_mutex_lock(&server_data.lock);
<<<<<<< HEAD
    
    printf("[FLUSH] flush_user_answers called: user=%d, room=%d\n", user_id, room_id);
    
    // QUAN TRỌNG: Flush TRỰC TIẾP từ DB, không cần in-memory
    // Vì khi disconnect, room có thể đã bị xóa khỏi in-memory
    // hoặc user không còn trong participants list
    
    // Không làm gì cả - đáp án đã được lưu vào DB realtime khi SAVE_ANSWER
    printf("[FLUSH] Answers already saved to DB in realtime - no action needed\n");
=======
    int room_idx = find_room_index(room_id);
    if (room_idx != -1) {
        TestRoom *room = &server_data.rooms[room_idx];
        
        int user_idx = find_participant_index(room, user_id);
        if (user_idx != -1) {
            flush_answers_to_db(user_id, room_id, room, user_idx);
        }
    }
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
    
    pthread_mutex_unlock(&server_data.lock);
}
