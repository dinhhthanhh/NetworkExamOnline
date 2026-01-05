#include "timer.h"
#include "db.h"
#include <time.h>
#include <pthread.h>

extern ServerData server_data;
extern sqlite3 *db;

RoomTimer room_timers[MAX_ROOMS];

void start_room_timer(int room_id, int time_limit)
{
  pthread_mutex_lock(&server_data.lock);

  if (room_id < MAX_ROOMS)
  {
    room_timers[room_id].room_id = room_id;
    room_timers[room_id].time_remaining = time_limit;
    room_timers[room_id].start_time = time(NULL);

    printf("Timer started for room %d: %d seconds\n", room_id, time_limit);
  }

  pthread_mutex_unlock(&server_data.lock);
}

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
    // For now, just log the broadcast
    printf("Broadcast time update: Room %d has %d seconds remaining\n", room_id, time_remaining);
  }

  pthread_mutex_unlock(&server_data.lock);
}

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
    if (room->room_status == 1) {  // STARTED
      rooms_to_check[room_check_count].room_id = room->room_id;
      rooms_to_check[room_check_count].time_limit = room->time_limit;
      rooms_to_check[room_check_count].exam_start_time = room->exam_start_time;
      rooms_to_check[room_check_count].room_status = room->room_status;
      room_check_count++;
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

void cleanup_expired_rooms(void)
{
  pthread_mutex_lock(&server_data.lock);

  time_t now = time(NULL);
  const int ROOM_EXPIRE_TIME = 3600; // 1 hour

  for (int i = 0; i < server_data.room_count; i++)
  {
    TestRoom *room = &server_data.rooms[i];

    if (room->room_status == 2)  // ENDED
    { // Finished
      int age = (int)(now - room->end_time);

      if (age > ROOM_EXPIRE_TIME)
      {
        // Mark as expired (can be archived)
        printf("Room %d expired and can be archived\n", i);
      }
    }
  }

  pthread_mutex_unlock(&server_data.lock);
}
