#include "timer.h"
#include "db.h"
#include <time.h>
#include <pthread.h>

extern ServerData server_data;
extern sqlite3 *db;

/*
 * Gửi thông tin thời gian còn lại của một phòng thi tới tất cả
 * thí sinh trong phòng (hiện tại mới xây dựng message, chưa gửi ra client).
 */
void broadcast_time_update(int room_id, int time_remaining)
{
  pthread_mutex_lock(&server_data.lock);

  TestRoom *room = &server_data.rooms[room_id];

  // Send time update to all participants
  char update[100];
  snprintf(update, sizeof(update), "TIME_UPDATE|%d|%d\n", room_id, time_remaining);

  for (int i = 0; i < room->participant_count; i++)
  {
    // In a real app, we'd need to track client sockets for each participant
    // Currently, this function only updates server-side timer state.
  }

  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Quét toàn bộ phòng thi đang STARTED để kiểm tra hết giờ:
 *  - Gửi TIME_UPDATE định kỳ (thông qua broadcast_time_update)
 *  - Khi hết thời gian, auto-submit cho các user đang online và
 *    ghi kết quả vào bảng results.
 */
void check_room_timeouts(void)
{
  pthread_mutex_lock(&server_data.lock);

  time_t now = time(NULL);

  for (int i = 0; i < server_data.room_count; i++)
  {
    TestRoom *room = &server_data.rooms[i];

    if (room->room_status == 1)  // STARTED
    { // Ongoing
      int elapsed = (int)(now - room->exam_start_time);
      int remaining = room->time_limit - elapsed;

      if (remaining > 0)
      {
        broadcast_time_update(i, remaining);
      }
      else if (room->room_status == 1)
      {
        // Time's up - auto-submit CHỈ users đang ONLINE
        room->room_status = 0; // Reset về WAITING để có thể mở lại kỳ thi

        // Query danh sách participants từ DB để lấy users đã bắt đầu thi
        char participant_query[512];
        snprintf(participant_query, sizeof(participant_query),
                 "SELECT DISTINCT p.user_id FROM participants p "
                 "WHERE p.room_id = %d AND p.start_time > 0",
                 i);

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
                       i, user_id);

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
                         user_id, i);

                int score = 0;
                sqlite3_stmt *score_stmt;
                if (sqlite3_prepare_v2(db, score_query, -1, &score_stmt, NULL) == SQLITE_OK) {
                  if (sqlite3_step(score_stmt) == SQLITE_ROW) {
                    score = sqlite3_column_int(score_stmt, 0);
                  }
                  sqlite3_finalize(score_stmt);
                }

                // Đếm tổng số câu hỏi
                char count_query[256];
                snprintf(count_query, sizeof(count_query),
                         "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d", i);

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
                         user_id, i, score, total_questions, room->time_limit * 60);

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
    }
  }

  pthread_mutex_unlock(&server_data.lock);
}
