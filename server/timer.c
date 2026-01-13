#include "timer.h"
#include "db.h"
#include <time.h>
#include <pthread.h>

extern ServerData server_data;
extern sqlite3 *db;

/*
 * Gửi thông tin thời gian còn lại của một phòng thi tới tất cả
 * thí sinh trong phòng (hiện tại mới xây dựng message, chưa gửi ra client).
 * NOTE: This function is called from check_room_timeouts which already holds the mutex
 */
void broadcast_time_update(int room_idx, int time_remaining)
{
  // NOTE: No mutex lock here - caller (check_room_timeouts) already holds the lock
  
  TestRoom *room = &server_data.rooms[room_idx];

  // Send time update to all participants
  char update[100];
  snprintf(update, sizeof(update), "TIME_UPDATE|%d|%d\n", room->room_id, time_remaining);

  for (int i = 0; i < room->participant_count; i++)
  {
    // In a real app, we'd need to track client sockets for each participant
    // Currently, this function only updates server-side timer state.
  }
}

/*
 * Quét toàn bộ phòng thi đang STARTED để kiểm tra hết giờ:
 *  - Gửi TIME_UPDATE định kỳ (thông qua broadcast_time_update)
 *  - Khi hết thời gian, auto-submit cho các user đang online và
 *    ghi kết quả vào bảng results.
 */
void check_room_timeouts(void)
{
  // Copy room data để xử lý ngoài lock (tránh deadlock)
  typedef struct {
    int room_id;
    int time_limit;
    time_t exam_start_time;
    int room_status;
  } RoomSnapshot;
  
  RoomSnapshot rooms_to_check[MAX_ROOMS];
  int room_check_count = 0;
  
  // PHASE 1: Copy data nhanh trong lock
  pthread_mutex_lock(&server_data.lock);
  time_t now = time(NULL);
  
  for (int i = 0; i < server_data.room_count; i++) {
    TestRoom *room = &server_data.rooms[i];
<<<<<<< HEAD
    if (room->room_status == 1) {  // STARTED
      rooms_to_check[room_check_count].room_id = room->room_id;
      rooms_to_check[room_check_count].time_limit = room->time_limit;
      rooms_to_check[room_check_count].exam_start_time = room->exam_start_time;
      rooms_to_check[room_check_count].room_status = room->room_status;
      room_check_count++;
=======

    if (room->room_status == 1)  // STARTED
    { // Ongoing
      int elapsed = (int)(now - room->exam_start_time);
      int remaining = (room->time_limit * 60) - elapsed;  // time_limit is in minutes, convert to seconds

      if (remaining > 0)
      {
        broadcast_time_update(i, remaining);
      }
      else if (room->room_status == 1)
      {
        // Time's up - auto-submit CHỈ users đang ONLINE
        room->room_status = 2; // Set status TO ENDED

        // ===== PERSIST ENDED STATUS TO DATABASE =====
        char update_status_sql[256];
        snprintf(update_status_sql, sizeof(update_status_sql),
                 "UPDATE rooms SET room_status = 2 WHERE id = %d", room->room_id);
        char *update_err = NULL;
        sqlite3_exec(db, update_status_sql, NULL, NULL, &update_err);
        if (update_err) {
          sqlite3_free(update_err);
        } else {
        }

        // ===== BROADCAST ROOM_ENDED TO ALL ONLINE USERS =====
        char end_broadcast[128];
        snprintf(end_broadcast, sizeof(end_broadcast), "ROOM_ENDED|%d\n", room->room_id);
        for (int u = 0; u < server_data.user_count; u++) {
          if (server_data.users[u].is_online == 1) {
            send(server_data.users[u].socket_fd, end_broadcast, strlen(end_broadcast), 0);
          }
        }

        // Query danh sách participants từ DB để lấy users đã bắt đầu thi
        char participant_query[512];
        snprintf(participant_query, sizeof(participant_query),
                 "SELECT DISTINCT p.user_id FROM participants p "
                 "WHERE p.room_id = %d AND p.start_time > 0",
                 room->room_id);

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, participant_query, -1, &stmt, NULL) == SQLITE_OK) {
          while (sqlite3_step(stmt) == SQLITE_ROW) {
            int user_id = sqlite3_column_int(stmt, 0);

            // Kiểm tra user có đang online không
            int is_online = 0;
            for (int u = 0; u < server_data.user_count; u++) {
              if (server_data.users[u].user_id == user_id) {
                is_online = server_data.users[u].is_online;
                break;
              }
            }

            // CHỈ auto-submit user đang online
            // User offline sẽ được giữ lại để có thể RESUME sau
            if (is_online) {
              // Query để check đã submit chưa
              char check_result[256];
              snprintf(check_result, sizeof(check_result),
                       "SELECT id FROM results WHERE room_id = %d AND user_id = %d",
                       room->room_id, user_id);

              sqlite3_stmt *check_stmt;
              int already_submitted = 0;
              if (sqlite3_prepare_v2(db, check_result, -1, &check_stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                  already_submitted = 1;
                }
                sqlite3_finalize(check_stmt);
              }

              if (!already_submitted) {
                // Tính điểm từ exam_answers
                char score_query[512];
                snprintf(score_query, sizeof(score_query),
                         "SELECT COUNT(*) FROM exam_answers ua "
                         "JOIN exam_questions q ON ua.question_id = q.id "
                         "WHERE ua.user_id = %d AND ua.room_id = %d "
                         "AND ua.selected_answer = q.correct_answer",
                         user_id, room->room_id);

                int score = 0;
                sqlite3_stmt *score_stmt;
                if (sqlite3_prepare_v2(db, score_query, -1, &score_stmt, NULL) == SQLITE_OK) {
                  if (sqlite3_step(score_stmt) == SQLITE_ROW) {
                    score = sqlite3_column_int(score_stmt, 0);
                  }
                  sqlite3_finalize(score_stmt);
                }

                // Đếm tổng số câu hỏi đã được chọn (is_selected = 1)
                char count_query[256];
                snprintf(count_query, sizeof(count_query),
                         "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d AND is_selected = 1", room->room_id);

                int total_questions = 0;
                sqlite3_stmt *count_stmt;
                if (sqlite3_prepare_v2(db, count_query, -1, &count_stmt, NULL) == SQLITE_OK) {
                  if (sqlite3_step(count_stmt) == SQLITE_ROW) {
                    total_questions = sqlite3_column_int(count_stmt, 0);
                  }
                  sqlite3_finalize(count_stmt);
                }

                // Insert into results
                char insert_query[512];
                snprintf(insert_query, sizeof(insert_query),
                         "INSERT INTO results (user_id, room_id, score, total_questions, time_taken) "
                         "VALUES (%d, %d, %d, %d, %d)",
                         user_id, room->room_id, score, total_questions, room->time_limit * 60);

                char *err_msg = NULL;
                sqlite3_exec(db, insert_query, NULL, NULL, &err_msg);
                if (err_msg) {
                  sqlite3_free(err_msg);
                }
              }
            }
          }
          sqlite3_finalize(stmt);
        }
      }
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
    }
  }
  pthread_mutex_unlock(&server_data.lock);
  
  // PHASE 2: Xử lý timeout NGOÀI lock (query DB an toàn)
  for (int i = 0; i < room_check_count; i++) {
    RoomSnapshot *snapshot = &rooms_to_check[i];
    
    int elapsed = (int)(now - snapshot->exam_start_time);
    int remaining = snapshot->time_limit * 60 - elapsed;
    
    if (remaining > 0) {
      // Còn thời gian - broadcast time update
      // broadcast_time_update(snapshot->room_id, remaining);  // Skip for now
      continue;
    }
    
    // Hết giờ - cần auto-submit
    printf("[TIMER] Room %d time's up - processing auto-submit\n", snapshot->room_id);
    
    // Update room status trong lock nhỏ
    pthread_mutex_lock(&server_data.lock);
    for (int j = 0; j < server_data.room_count; j++) {
      if (server_data.rooms[j].room_id == snapshot->room_id) {
        if (server_data.rooms[j].room_status == 1) {  // Double check
          server_data.rooms[j].room_status = 2;  // ENDED
          printf("[TIMER] Set room %d status to ENDED\n", snapshot->room_id);
        }
        break;
      }
    }
    pthread_mutex_unlock(&server_data.lock);
    
    // Query participants NGOÀI lock
    char participant_query[512];
    snprintf(participant_query, sizeof(participant_query),
             "SELECT DISTINCT p.user_id FROM participants p "
             "WHERE p.room_id = %d AND p.start_time > 0",
             snapshot->room_id);
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, participant_query, -1, &stmt, NULL) == SQLITE_OK) {
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        int user_id = sqlite3_column_int(stmt, 0);
        
        // Check đã submit chưa
        char check_result[256];
        snprintf(check_result, sizeof(check_result),
                 "SELECT id FROM results WHERE room_id = %d AND user_id = %d",
                 snapshot->room_id, user_id);
        
        sqlite3_stmt *check_stmt;
        int already_submitted = 0;
        if (sqlite3_prepare_v2(db, check_result, -1, &check_stmt, NULL) == SQLITE_OK) {
          if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            already_submitted = 1;
          }
          sqlite3_finalize(check_stmt);
        }
        
        if (!already_submitted) {
          // Tính điểm
          char score_query[512];
          snprintf(score_query, sizeof(score_query),
                   "SELECT COUNT(*) FROM user_answers ua "
                   "JOIN questions q ON ua.question_id = q.id "
                   "WHERE ua.user_id = %d AND ua.room_id = %d "
                   "AND ua.selected_answer = q.correct_answer",
                   user_id, snapshot->room_id);
          
          int score = 0;
          sqlite3_stmt *score_stmt;
          if (sqlite3_prepare_v2(db, score_query, -1, &score_stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(score_stmt) == SQLITE_ROW) {
              score = sqlite3_column_int(score_stmt, 0);
            }
            sqlite3_finalize(score_stmt);
          }
          
          // Đếm tổng câu hỏi
          char count_query[256];
          snprintf(count_query, sizeof(count_query),
                   "SELECT COUNT(*) FROM questions WHERE room_id = %d", snapshot->room_id);
          
          int total_questions = 0;
          sqlite3_stmt *count_stmt;
          if (sqlite3_prepare_v2(db, count_query, -1, &count_stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(count_stmt) == SQLITE_ROW) {
              total_questions = sqlite3_column_int(count_stmt, 0);
            }
            sqlite3_finalize(count_stmt);
          }
          
          // Insert results
          char insert_query[512];
          snprintf(insert_query, sizeof(insert_query),
                   "INSERT INTO results (user_id, room_id, score, total_questions, time_taken) "
                   "VALUES (%d, %d, %d, %d, %d)",
                   user_id, snapshot->room_id, score, total_questions, snapshot->time_limit * 60);
          
          char *err_msg = NULL;
          sqlite3_exec(db, insert_query, NULL, NULL, &err_msg);
          if (err_msg) {
            fprintf(stderr, "[TIMER] Failed to auto-submit user %d: %s\n", user_id, err_msg);
            sqlite3_free(err_msg);
          } else {
            printf("[TIMER] Auto-submitted user %d in room %d - Score: %d/%d\n", 
                   user_id, snapshot->room_id, score, total_questions);
          }
          
          // Mark taken
          char mark_taken[256];
          snprintf(mark_taken, sizeof(mark_taken),
                   "UPDATE room_participants SET has_taken_exam = 1 "
                   "WHERE user_id = %d AND room_id = %d",
                   user_id, snapshot->room_id);
          sqlite3_exec(db, mark_taken, NULL, NULL, NULL);
        }
      }
      sqlite3_finalize(stmt);
    }
  }
}
