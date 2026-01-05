#include "rooms.h"
#include "db.h"
#include "results.h"
#include "network.h"
#include <sys/socket.h>
#include <unistd.h>  // for usleep

extern ServerData server_data;
extern sqlite3 *db;

// Forward declaration
static void load_room_answers_internal(int room_id, int user_id);

void create_test_room(int socket_fd, int creator_id, char *room_name, int num_q, int time_limit, int easy_count, int medium_count, int hard_count) {
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra role - chỉ admin mới được tạo room
  char role_query[256];
  snprintf(role_query, sizeof(role_query),
           "SELECT role FROM users WHERE id = %d", creator_id);
  
  sqlite3_stmt *stmt_role;
  if (sqlite3_prepare_v2(db, role_query, -1, &stmt_role, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt_role) == SQLITE_ROW) {
      const char *role = (const char *)sqlite3_column_text(stmt_role, 0);
      if (role && strcmp(role, "admin") != 0) {
        char response[] = "CREATE_ROOM_FAIL|Permission denied: Only admin can create rooms\n";
        send(socket_fd, response, strlen(response), 0);
        sqlite3_finalize(stmt_role);
        pthread_mutex_unlock(&server_data.lock);
        return;
      }
    }
    sqlite3_finalize(stmt_role);
  }

  // Kiểm tra đã đạt max rooms
  if (server_data.room_count >= MAX_ROOMS) {
    char response[] = "CREATE_ROOM_FAIL|Max rooms reached\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  // Kiểm tra room_name hợp lệ
  if (room_name == NULL || strlen(room_name) == 0) {
    char response[] = "CREATE_ROOM_FAIL|Room name cannot be empty\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // time_limit validation
  if (time_limit <= 0 || time_limit > 180) {
    char response[] = "CREATE_ROOM_FAIL|Invalid time limit (1-180 minutes)\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Validate question counts
  int total_questions = easy_count + medium_count + hard_count;
  if (total_questions <= 0) {
    char response[] = "CREATE_ROOM_FAIL|Must select at least 1 question\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  if (total_questions > 50) {
    char response[] = "CREATE_ROOM_FAIL|Too many questions (max 50)\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Kiểm tra room_name đã tồn tại chưa
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT id FROM rooms WHERE name = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    char response[] = "CREATE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_ROW) {
    char response[] = "CREATE_ROOM_FAIL|Room name already exists\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // ===== INSERT VÀO DATABASE =====
  
  const char *sql_insert = 
    "INSERT INTO rooms (name, host_id, duration, room_status, exam_start_time) "
    "VALUES (?, ?, ?, 0, 0);";
  
  rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Failed to prepare insert: %s\n", sqlite3_errmsg(db));
    char response[] = "CREATE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, creator_id);
  sqlite3_bind_int(stmt, 3, time_limit);

  rc = sqlite3_step(stmt);
  
  if (rc != SQLITE_DONE) {
    fprintf(stderr, "Failed to insert room: %s\n", sqlite3_errmsg(db));
    char response[] = "CREATE_ROOM_FAIL|Failed to create room\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Lấy room_id vừa tạo (auto-increment)
  int room_id = sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  // ===== RANDOM QUESTION SELECTION =====
  
  int selected_count = 0;
  
  // Select easy questions
  if (easy_count > 0) {
    const char *sql_easy = "SELECT id FROM exam_questions WHERE room_id = 0 AND difficulty = 'easy' ORDER BY RANDOM() LIMIT ?;";
    rc = sqlite3_prepare_v2(db, sql_easy, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, easy_count);
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        int question_id = sqlite3_column_int(stmt, 0);
        // Update question to assign it to this room
        sqlite3_stmt *stmt_update;
        const char *sql_update = "UPDATE exam_questions SET room_id = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db, sql_update, -1, &stmt_update, 0) == SQLITE_OK) {
          sqlite3_bind_int(stmt_update, 1, room_id);
          sqlite3_bind_int(stmt_update, 2, question_id);
          sqlite3_step(stmt_update);
          sqlite3_finalize(stmt_update);
          selected_count++;
        }
      }
      sqlite3_finalize(stmt);
    }
  }
  
  // Select medium questions
  if (medium_count > 0) {
    const char *sql_medium = "SELECT id FROM exam_questions WHERE room_id = 0 AND difficulty = 'medium' ORDER BY RANDOM() LIMIT ?;";
    rc = sqlite3_prepare_v2(db, sql_medium, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, medium_count);
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        int question_id = sqlite3_column_int(stmt, 0);
        sqlite3_stmt *stmt_update;
        const char *sql_update = "UPDATE exam_questions SET room_id = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db, sql_update, -1, &stmt_update, 0) == SQLITE_OK) {
          sqlite3_bind_int(stmt_update, 1, room_id);
          sqlite3_bind_int(stmt_update, 2, question_id);
          sqlite3_step(stmt_update);
          sqlite3_finalize(stmt_update);
          selected_count++;
        }
      }
      sqlite3_finalize(stmt);
    }
  }
  
  // Select hard questions
  if (hard_count > 0) {
    const char *sql_hard = "SELECT id FROM exam_questions WHERE room_id = 0 AND difficulty = 'hard' ORDER BY RANDOM() LIMIT ?;";
    rc = sqlite3_prepare_v2(db, sql_hard, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, hard_count);
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        int question_id = sqlite3_column_int(stmt, 0);
        sqlite3_stmt *stmt_update;
        const char *sql_update = "UPDATE exam_questions SET room_id = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db, sql_update, -1, &stmt_update, 0) == SQLITE_OK) {
          sqlite3_bind_int(stmt_update, 1, room_id);
          sqlite3_bind_int(stmt_update, 2, question_id);
          sqlite3_step(stmt_update);
          sqlite3_finalize(stmt_update);
          selected_count++;
        }
      }
      sqlite3_finalize(stmt);
    }
  }
  
  if (selected_count < total_questions) {
    char warning[256];
    snprintf(warning, sizeof(warning), 
             "Warning: Only %d out of %d requested questions were available in the question bank", 
             selected_count, total_questions);
    printf("[CREATE_ROOM] %s\n", warning);
  }

  // ===== THÊM VÀO IN-MEMORY =====
  
  if (server_data.room_count < MAX_ROOMS) {
      int idx = server_data.room_count;
      server_data.rooms[idx].room_id = room_id;
      strncpy(server_data.rooms[idx].room_name, room_name, sizeof(server_data.rooms[idx].room_name) - 1);
      server_data.rooms[idx].creator_id = creator_id;
      server_data.rooms[idx].time_limit = time_limit;
      server_data.rooms[idx].room_status = 0;  // WAITING - chưa bắt đầu
      server_data.rooms[idx].exam_start_time = 0;
      server_data.rooms[idx].num_questions = selected_count;  // số câu hỏi đã chọn
      server_data.rooms[idx].participant_count = 0;
      
      // Init arrays
      memset(server_data.rooms[idx].participants, 0, sizeof(server_data.rooms[idx].participants));
      memset(server_data.rooms[idx].answers, -1, sizeof(server_data.rooms[idx].answers));
      
      server_data.room_count++;
      printf("[CREATE_ROOM] Added room '%s' (ID=%d) to in-memory at idx=%d with %d questions\n", 
             room_name, room_id, idx, selected_count);
  }

  // ===== GỬI RESPONSE =====
  
  char response[512];
  snprintf(response, sizeof(response), 
           "CREATE_ROOM_OK|%d|%s|%d|%d|Easy:%d Medium:%d Hard:%d\n", 
           room_id, room_name, time_limit, selected_count, easy_count, medium_count, hard_count);
  send(socket_fd, response, strlen(response), 0);

  // ===== LOGGING =====
  
  char log_details[512];
  snprintf(log_details, sizeof(log_details), 
           "Room: %s (ID=%d, Time=%dm, Questions=%d [E:%d M:%d H:%d])", 
           room_name, room_id, time_limit, selected_count, easy_count, medium_count, hard_count);
  log_activity(creator_id, "CREATE_ROOM", log_details);

  printf("[INFO] User %d created room '%s' (ID=%d)\n", 
         creator_id, room_name, room_id);
  
  pthread_mutex_unlock(&server_data.lock);
  
  // ===== BROADCAST ROOM CREATED =====
  broadcast_room_created(room_id, room_name, time_limit);
}

void list_test_rooms(int socket_fd) {
  pthread_mutex_lock(&server_data.lock);

  sqlite3_stmt *stmt;
  
  const char *sql = 
    "SELECT "
    "  r.id, "
    "  r.name, "
    "  r.duration, "
    "  r.is_active, "
    "  u.username, "
    "  COUNT(q.id) as question_count "
    "FROM rooms r "
    "JOIN users u ON r.host_id = u.id "
    "LEFT JOIN exam_questions q ON r.id = q.room_id "
    "WHERE r.is_active = 1 "
    "GROUP BY r.id "
    "ORDER BY r.created_at DESC;";
  
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "LIST_ROOMS_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  char response[8192];
  int offset = 0;
  int room_count = 0;

  // Đếm số rooms trước
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    room_count++;
  }
  sqlite3_reset(stmt);

  // Header với count
  offset += snprintf(response + offset, sizeof(response) - offset, 
                     "LIST_ROOMS_OK|%d\n", room_count);

  // List từng room
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (offset >= sizeof(response) - 1000) break;

    int room_id = sqlite3_column_int(stmt, 0);
    const char *room_name = (const char*)sqlite3_column_text(stmt, 1);
    int duration = sqlite3_column_int(stmt, 2);
    // int is_active = sqlite3_column_int(stmt, 3);  // Unused
    const char *host = (const char*)sqlite3_column_text(stmt, 4);
    int question_count = sqlite3_column_int(stmt, 5);

    const char *status_str = "Open";

    char question_info[50];
    if (question_count == 0) {
      strcpy(question_info, "Chưa có question");
    } else {
      snprintf(question_info, sizeof(question_info), "%d questions", question_count);
    }

    offset += snprintf(response + offset, sizeof(response) - offset,
                       "ROOM|%d|%s|%d|%s|%s|%s\n",
                       room_id, room_name, duration, status_str, question_info, host);
  }

  sqlite3_finalize(stmt);

  send(socket_fd, response, offset, 0);
  pthread_mutex_unlock(&server_data.lock);
}

void join_test_room(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra room có tồn tại
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT is_active FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "JOIN_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW) {
    char response[] = "JOIN_ROOM_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int is_active = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  // Kiểm tra room có OPEN không
  if (is_active != 1) {
    char response[] = "JOIN_ROOM_FAIL|Room is closed\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // ===== KIỂM TRA ĐÃ THI CHƯA (LOGIC MỚI) =====
  // Kiểm tra trong bảng room_participants xem user đã thi room này chưa
  char check_taken_query[256];
  snprintf(check_taken_query, sizeof(check_taken_query),
           "SELECT has_taken_exam FROM room_participants WHERE room_id = %d AND user_id = %d",
           room_id, user_id);
  
  if (sqlite3_prepare_v2(db, check_taken_query, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      int has_taken = sqlite3_column_int(stmt, 0);
      sqlite3_finalize(stmt);
      
      if (has_taken == 1) {
        char response[] = "JOIN_ROOM_FAIL|You have already taken this exam\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
      }
    } else {
      // Chưa có record -> tạo mới với has_taken_exam = 0
      sqlite3_finalize(stmt);
      
      const char *sql_insert_participant = 
        "INSERT OR IGNORE INTO room_participants (room_id, user_id, has_taken_exam) "
        "VALUES (?, ?, 0);";
      
      if (sqlite3_prepare_v2(db, sql_insert_participant, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, room_id);
        sqlite3_bind_int(stmt, 2, user_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
      }
    }
  }

  // Thêm user vào participants table (cho tracking thời gian bắt đầu)
  const char *sql_insert = 
    "INSERT OR IGNORE INTO participants (room_id, user_id, start_time) "
    "VALUES (?, ?, 0);";
  
  rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    char response[] = "JOIN_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  sqlite3_bind_int(stmt, 2, user_id);
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    char response[] = "JOIN_ROOM_FAIL|Failed to join\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  char response[] = "JOIN_ROOM_OK\n";
  send(socket_fd, response, strlen(response), 0);

  char log_details[128];
  snprintf(log_details, sizeof(log_details), "Joined room ID=%d", room_id);
  log_activity(user_id, "JOIN_ROOM", log_details);
  
  printf("[INFO] User %d joined room %d\n", user_id, room_id);
  pthread_mutex_unlock(&server_data.lock);
}

/*
// REMOVED: set_room_max_attempts - not needed anymore
void set_room_max_attempts(int socket_fd, int user_id, int room_id, int max_attempts) {
  pthread_mutex_lock(&server_data.lock);
  
  // Kiểm tra user có phải host của room không
  char check_query[256];
  snprintf(check_query, sizeof(check_query),
           "SELECT host_id FROM rooms WHERE id = %d", room_id);
  
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, check_query, -1, &stmt, NULL) != SQLITE_OK) {
    send(socket_fd, "ERROR|Database error\n", 21, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  int host_id = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    host_id = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  
  if (host_id != user_id) {
    send(socket_fd, "ERROR|Only room host can set max attempts\n", 43, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Update max_attempts
  char update_query[256];
  snprintf(update_query, sizeof(update_query),
           "UPDATE rooms SET max_attempts = %d WHERE id = %d",
           max_attempts, room_id);
  
  char *err_msg = NULL;
  if (sqlite3_exec(db, update_query, NULL, NULL, &err_msg) != SQLITE_OK) {
    char response[128];
    snprintf(response, sizeof(response), "ERROR|%s\n", 
             err_msg ? err_msg : "Failed to update");
    send(socket_fd, response, strlen(response), 0);
    if (err_msg) sqlite3_free(err_msg);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  char response[128];
  snprintf(response, sizeof(response), 
           "SET_MAX_ATTEMPTS_OK|%d|%d\n", room_id, max_attempts);
  send(socket_fd, response, strlen(response), 0);
  
  printf("[INFO] Room %d max_attempts set to %d by user %d\n", 
         room_id, max_attempts, user_id);
  
  pthread_mutex_unlock(&server_data.lock);
}
*/

void close_room(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra room có tồn tại và quyền sở hữu
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id, is_active FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "CLOSE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW) {
    char response[] = "CLOSE_ROOM_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int host_id = sqlite3_column_int(stmt, 0);
  int is_active = sqlite3_column_int(stmt, 1);
  sqlite3_finalize(stmt);

  // Kiểm tra quyền sở hữu
  if (host_id != user_id) {
    char response[] = "CLOSE_ROOM_FAIL|Only creator can close\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Kiểm tra room đã close chưa
  if (is_active == 0) {
    char response[] = "CLOSE_ROOM_FAIL|Room already closed\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Close room (set is_active = 0)
  const char *sql_update = "UPDATE rooms SET is_active = 0 WHERE id = ?;";
  rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "CLOSE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    char response[] = "CLOSE_ROOM_FAIL|Failed to close room\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  char response[128];
  snprintf(response, sizeof(response), "CLOSE_ROOM_OK|Room %d closed\n", room_id);
  send(socket_fd, response, strlen(response), 0);

  char log_details[128];
  snprintf(log_details, sizeof(log_details), "Closed room ID=%d", room_id);
  log_activity(user_id, "CLOSE_ROOM", log_details);
  
  printf("[INFO] User %d closed room %d\n", user_id, room_id);
  pthread_mutex_unlock(&server_data.lock);
}

void list_my_rooms(int socket_fd, int user_id) {
  pthread_mutex_lock(&server_data.lock);

  sqlite3_stmt *stmt;
  
  const char *sql = 
    "SELECT "
    "  r.id, "
    "  r.name, "
    "  r.duration, "
    "  r.room_status, "
    "  COUNT(q.id) as question_count "
    "FROM rooms r "
    "LEFT JOIN exam_questions q ON r.id = q.room_id "
    "WHERE r.host_id = ? "
    "GROUP BY r.id "
    "ORDER BY r.created_at DESC;";
  
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "LIST_MY_ROOMS_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, user_id);

  char response[8192];
  int offset = 0;
  int room_count = 0;

  // Đếm số rooms trước
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    room_count++;
  }
  sqlite3_reset(stmt);

  // Header với count
  offset += snprintf(response + offset, sizeof(response) - offset, 
                     "LIST_MY_ROOMS_OK|%d\n", room_count);

  // List từng room
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (offset >= sizeof(response) - 1000) break;

    int room_id = sqlite3_column_int(stmt, 0);
    const char *room_name = (const char*)sqlite3_column_text(stmt, 1);
    int duration = sqlite3_column_int(stmt, 2);
    int room_status = sqlite3_column_int(stmt, 3);
    int question_count = sqlite3_column_int(stmt, 4);

    const char *status_str;
    if (room_status == 0) {
      status_str = "Waiting";
    } else if (room_status == 1) {
      status_str = "Started";
    } else {
      status_str = "Ended";
    }

    char question_info[50];
    if (question_count == 0) {
      strcpy(question_info, "No questions");
    } else {
      snprintf(question_info, sizeof(question_info), "%d questions", question_count);
    }

    offset += snprintf(response + offset, sizeof(response) - offset,
                       "ROOM|%d|%s|%d|%s|%s\n",
                       room_id, room_name, duration, status_str, question_info);
  }

  sqlite3_finalize(stmt);

  send(socket_fd, response, offset, 0);
  pthread_mutex_unlock(&server_data.lock);
}

// Delete a room (admin only)
void delete_room(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);
  
  // Check if user is admin and room owner
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "DELETE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);
  
  if (rc != SQLITE_ROW) {
    char response[] = "DELETE_ROOM_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  int host_id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  
  if (host_id != user_id) {
    char response[] = "DELETE_ROOM_FAIL|You are not the owner of this room\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // ===== BROADCAST TO ALL PARTICIPANTS FIRST =====
  char broadcast_msg[256];
  snprintf(broadcast_msg, sizeof(broadcast_msg), "ROOM_DELETED|%d\n", room_id);
  broadcast_to_room_participants(room_id, broadcast_msg);  // GỬI CHO TẤT CẢ
  printf("[INFO] Broadcasting ROOM_DELETED for room %d to all participants\n", room_id);
  
  // Small delay để broadcast được xử lý
  usleep(100000); // 100ms
  
  // ===== DELETE FROM DATABASE (HARD DELETE) =====
  // Delete associated data: exam_answers, participants, questions
  const char *sqls[] = {
    "DELETE FROM exam_answers WHERE room_id = ?;",
    "DELETE FROM participants WHERE room_id = ?;",
    "DELETE FROM exam_questions WHERE room_id = ?;",  // Permanently delete questions
    "DELETE FROM results WHERE room_id = ?;",
    "DELETE FROM rooms WHERE id = ?;"  // Hard delete the room
  };
  
  const char *sql_names[] = {
    "exam_answers", "participants", "exam_questions", "results", "rooms"
  };
  
  for (int i = 0; i < 5; i++) {
    rc = sqlite3_prepare_v2(db, sqls[i], -1, &stmt, 0);
    if (rc == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, room_id);
      rc = sqlite3_step(stmt);
      if (rc == SQLITE_DONE) {
        int deleted = sqlite3_changes(db);
        printf("[DELETE_ROOM] Deleted %d rows from %s for room %d\n", deleted, sql_names[i], room_id);
      } else {
        printf("[DELETE_ROOM] ERROR deleting from %s: %s\n", sql_names[i], sqlite3_errmsg(db));
      }
      sqlite3_finalize(stmt);
    } else {
      printf("[DELETE_ROOM] ERROR preparing DELETE for %s: %s\n", sql_names[i], sqlite3_errmsg(db));
    }
  }
  
  // Remove from in-memory
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      // Shift remaining rooms
      for (int j = i; j < server_data.room_count - 1; j++) {
        server_data.rooms[j] = server_data.rooms[j + 1];
      }
      server_data.room_count--;
      break;
    }
  }
  
  // ===== SEND RESPONSE TO ADMIN LAST =====
  char response[] = "DELETE_ROOM_OK\n";
  send(socket_fd, response, strlen(response), 0);
  printf("[INFO] Room %d fully deleted from memory and database\n", room_id);
  pthread_mutex_unlock(&server_data.lock);
}

void start_test(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra room có tồn tại trong database không
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id, is_active FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "START_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW) {
    char response[] = "START_ROOM_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int host_id = sqlite3_column_int(stmt, 0);
  // int is_active = sqlite3_column_int(stmt, 1);  // Không cần check nữa
  sqlite3_finalize(stmt);

  // Kiểm tra quyền sở hữu
  if (host_id != user_id) {
    char response[] = "START_ROOM_FAIL|Only creator can start\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Tìm room trong in-memory để update status
  int room_idx = -1;
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      room_idx = i;
      break;
    }
  }
  
  if (room_idx == -1) {
    char response[] = "START_ROOM_FAIL|Room not loaded in memory\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Kiểm tra room đã bắt đầu chưa
  if (server_data.rooms[room_idx].room_status == 1) {  // STARTED
    char response[] = "START_ROOM_FAIL|Room already started\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Kiểm tra room có câu hỏi chưa
  char check_questions[256];
  snprintf(check_questions, sizeof(check_questions),
           "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d", room_id);
  sqlite3_stmt *check_stmt;
  int question_count = 0;
  
  if (sqlite3_prepare_v2(db, check_questions, -1, &check_stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(check_stmt) == SQLITE_ROW) {
      question_count = sqlite3_column_int(check_stmt, 0);
    }
    sqlite3_finalize(check_stmt);
  }
  
  if (question_count == 0) {
    char response[] = "START_ROOM_FAIL|Room has no questions. Please add questions first.\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Update room status: WAITING -> STARTED (both in-memory and DB)
  time_t start_time = time(NULL);
  server_data.rooms[room_idx].room_status = 1;  // STARTED
  server_data.rooms[room_idx].exam_start_time = start_time;

  // Update database status
  char update_sql[256];
  snprintf(update_sql, sizeof(update_sql),
           "UPDATE rooms SET room_status = 1 WHERE id = %d", room_id);
  char *err_msg = NULL;
  if (sqlite3_exec(db, update_sql, 0, 0, &err_msg) != SQLITE_OK) {
    printf("[ERROR] Failed to update room status in DB: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  char response[128];
  snprintf(response, sizeof(response), "START_ROOM_OK|Room %d started\n", room_id);
  send(socket_fd, response, strlen(response), 0);

  char log_details[128];
  snprintf(log_details, sizeof(log_details), "Started exam in room ID=%d", room_id);
  log_activity(user_id, "START_ROOM", log_details);
  
  printf("[INFO] User %d started exam in room %d\n", user_id, room_id);
  
  pthread_mutex_unlock(&server_data.lock);
  
  // ===== BROADCAST ROOM STARTED =====
  char broadcast_msg[256];
  snprintf(broadcast_msg, sizeof(broadcast_msg), 
           "ROOM_STARTED|%d|%ld\n", 
           room_id, start_time);
  broadcast_to_room_participants(room_id, broadcast_msg);
}

// User bắt đầu làm bài thi - Load questions và kiểm tra room status
void handle_begin_exam(int socket_fd, int user_id, int room_id)
{
    pthread_mutex_lock(&server_data.lock);
    
    // Tìm room trong in-memory
    int room_idx = -1;
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            room_idx = i;
            break;
        }
    }
    
    // Nếu room chưa có trong in-memory, load nó vào
    if (room_idx == -1 && server_data.room_count < MAX_ROOMS) {
        room_idx = server_data.room_count;
        server_data.rooms[room_idx].room_id = room_id;
        
        // Load info từ DB
        char room_info_query[256];
        snprintf(room_info_query, sizeof(room_info_query),
                 "SELECT name, host_id, duration FROM rooms WHERE id = %d", room_id);
        sqlite3_stmt *room_stmt;
        if (sqlite3_prepare_v2(db, room_info_query, -1, &room_stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(room_stmt) == SQLITE_ROW) {
                const char *name = (const char *)sqlite3_column_text(room_stmt, 0);
                if (name) {
                    strncpy(server_data.rooms[room_idx].room_name, name, 
                           sizeof(server_data.rooms[room_idx].room_name) - 1);
                }
                server_data.rooms[room_idx].creator_id = sqlite3_column_int(room_stmt, 1);
                server_data.rooms[room_idx].time_limit = sqlite3_column_int(room_stmt, 2);
                server_data.rooms[room_idx].room_status = 0;  // WAITING by default
                server_data.rooms[room_idx].exam_start_time = 0;
                
                // Đếm số câu hỏi
                char count_query[256];
                snprintf(count_query, sizeof(count_query),
                         "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d", room_id);
                sqlite3_stmt *count_stmt;
                if (sqlite3_prepare_v2(db, count_query, -1, &count_stmt, NULL) == SQLITE_OK) {
                    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
                        server_data.rooms[room_idx].num_questions = sqlite3_column_int(count_stmt, 0);
                    }
                    sqlite3_finalize(count_stmt);
                }
                
                server_data.rooms[room_idx].participant_count = 0;
                memset(server_data.rooms[room_idx].participants, 0, 
                       sizeof(server_data.rooms[room_idx].participants));
                memset(server_data.rooms[room_idx].answers, -1, 
                       sizeof(server_data.rooms[room_idx].answers));
                
                server_data.room_count++;
                printf("[BEGIN_EXAM] Loaded room %d into in-memory (idx=%d)\n", room_id, room_idx);
            }
            sqlite3_finalize(room_stmt);
        }
    }
    
    if (room_idx == -1) {
        send(socket_fd, "ERROR|Room not found\n", 21, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    TestRoom *room = &server_data.rooms[room_idx];
    
    // Kiểm tra user không phải host
    if (user_id == room->creator_id) {
        send(socket_fd, "ERROR|Host cannot take exam\n", 29, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // ===== LOGIC MỚI: KIỂM TRA ROOM STATUS =====
    
    // Case 1: WAITING - Chưa bắt đầu
    if (room->room_status == 0) {  // WAITING
        send(socket_fd, "EXAM_WAITING|Waiting for host to start exam\n", 45, 0);
        
        // Thêm user vào participants để sẵn sàng
        int already_participant = 0;
        for (int i = 0; i < room->participant_count; i++) {
            if (room->participants[i] == user_id) {
                already_participant = 1;
                break;
            }
        }
        
        if (!already_participant && room->participant_count < MAX_CLIENTS) {
            room->participants[room->participant_count] = user_id;
            room->participant_count++;
            printf("[BEGIN_EXAM] User %d joined waiting room %d\n", user_id, room_id);
        }
        
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Case 2: ENDED - Đã kết thúc
    if (room->room_status == 2) {  // ENDED
        send(socket_fd, "ERROR|Exam has ended\n", 21, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Case 3: STARTED - Đang thi
    if (room->room_status != 1) {
        send(socket_fd, "ERROR|Invalid room status\n", 26, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Tính thời gian còn lại
    time_t now = time(NULL);
    long elapsed = now - room->exam_start_time;
    long duration_seconds = room->time_limit * 60;
    long remaining = duration_seconds - elapsed;
    
    if (remaining <= 0) {
        send(socket_fd, "ERROR|Exam time expired\n", 24, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Thêm user vào participants nếu chưa có
    int already_participant = 0;
    for (int i = 0; i < room->participant_count; i++) {
        if (room->participants[i] == user_id) {
            already_participant = 1;
            break;
        }
    }
    
    if (!already_participant && room->participant_count < MAX_CLIENTS) {
        room->participants[room->participant_count] = user_id;
        room->participant_count++;
        
        // Init answers cho user này thành -1
        int user_idx = room->participant_count - 1;
        for (int q = 0; q < MAX_QUESTIONS; q++) {
            room->answers[user_idx][q].answer = -1;
        }
        printf("[BEGIN_EXAM] User %d joined started exam in room %d (late join, %ld sec remaining)\n", 
               user_id, room_id, remaining);
    }
    
    // Lưu start_time cho user này trong DB
    char update_query[512];
    snprintf(update_query, sizeof(update_query),
             "INSERT OR REPLACE INTO participants (room_id, user_id, start_time) "
             "VALUES (%d, %d, %ld)",
             room_id, user_id, now);
    sqlite3_exec(db, update_query, NULL, NULL, NULL);
    
    // Lấy danh sách câu hỏi
    char question_query[256];
    snprintf(question_query, sizeof(question_query),
             "SELECT id, question_text, option_a, option_b, option_c, option_d "
             "FROM exam_questions WHERE room_id = %d", room_id);
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, question_query, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "ERROR|Cannot load questions\n", 28, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Format: BEGIN_EXAM_OK|remaining_seconds|q1_id:q1_text:optA:optB:optC:optD|q2_id:...
    char response[8192];
    snprintf(response, sizeof(response), "BEGIN_EXAM_OK|%ld", remaining);
    
    int question_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int q_id = sqlite3_column_int(stmt, 0);
        const char *q_text = (const char *)sqlite3_column_text(stmt, 1);
        const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
        const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
        const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
        const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);
        
        char q_entry[1024];
        snprintf(q_entry, sizeof(q_entry), "|%d:%s:%s:%s:%s:%s",
                 q_id, q_text, opt_a, opt_b, opt_c, opt_d);
        strcat(response, q_entry);
        question_count++;
    }
    
    sqlite3_finalize(stmt);
    
    if (question_count == 0) {
        send(socket_fd, "ERROR|No questions in room\n", 27, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
    printf("[INFO] User %d began exam in room %d (%d questions, %ld sec remaining)\n", 
           user_id, room_id, question_count, remaining);
    
    pthread_mutex_unlock(&server_data.lock);
}

void handle_resume_exam(int socket_fd, int user_id, int room_id)
{
    pthread_mutex_lock(&server_data.lock);
    
    // Kiểm tra user đã bắt đầu thi trong room này chưa
    char check_query[256];
    snprintf(check_query, sizeof(check_query),
             "SELECT start_time FROM participants WHERE room_id = %d AND user_id = %d",
             room_id, user_id);
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, check_query, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "ERROR|Database error\n", 21, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    long start_time = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        start_time = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    // Nếu chưa bắt đầu hoặc start_time = 0
    if (start_time == 0) {
        send(socket_fd, "RESUME_NOT_STARTED\n", 19, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // **LOAD đáp án từ DB vào in-memory khi RESUME** (dùng internal version, đã lock rồi)
    load_room_answers_internal(room_id, user_id);
    
    // Kiểm tra đã submit chưa
    char check_result[256];
    snprintf(check_result, sizeof(check_result),
             "SELECT id FROM results WHERE room_id = %d AND user_id = %d",
             room_id, user_id);
    
    if (sqlite3_prepare_v2(db, check_result, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            send(socket_fd, "RESUME_ALREADY_SUBMITTED\n", 25, 0);
            sqlite3_finalize(stmt);
            pthread_mutex_unlock(&server_data.lock);
            return;
        }
        sqlite3_finalize(stmt);
    }
    
    // Lấy duration và tính thời gian còn lại
    char room_query[256];
    snprintf(room_query, sizeof(room_query),
             "SELECT duration FROM rooms WHERE id = %d", room_id);
    
    int duration_minutes = 60;
    if (sqlite3_prepare_v2(db, room_query, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            duration_minutes = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    // Tính thời gian đã trôi qua
    time_t now = time(NULL);
    long elapsed = now - start_time;
    long max_time = duration_minutes * 60;
    long remaining = max_time - elapsed;
    
    // Nếu hết thời gian, auto submit và trả luôn điểm
    if (remaining <= 0) {
      // Tự động chấm và lưu kết quả nếu chưa có
      auto_submit_on_disconnect(user_id, room_id);

      // Lấy điểm vừa lưu trong bảng results
      int score = 0, total_questions = 0, time_minutes = 0;
      char result_query[256];
      snprintf(result_query, sizeof(result_query),
           "SELECT score, total_questions, time_taken FROM results "
           "WHERE room_id = %d AND user_id = %d ORDER BY id DESC LIMIT 1",
           room_id, user_id);

      if (sqlite3_prepare_v2(db, result_query, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
          score = sqlite3_column_int(stmt, 0);
          total_questions = sqlite3_column_int(stmt, 1);
          long time_taken = sqlite3_column_int64(stmt, 2);
          time_minutes = (int)(time_taken / 60);
        }
        sqlite3_finalize(stmt);
      }

      char response[256];
      if (total_questions > 0) {
        snprintf(response, sizeof(response),
             "RESUME_TIME_EXPIRED|%d|%d|%d\n",
             score, total_questions, time_minutes);
      } else {
        // Fallback nếu không lấy được kết quả
        snprintf(response, sizeof(response), "RESUME_TIME_EXPIRED\n");
      }

      send(socket_fd, response, strlen(response), 0);
      pthread_mutex_unlock(&server_data.lock);
      return;
    }
    
    // Lấy danh sách câu hỏi và câu trả lời đã lưu
    char question_query[512];
    snprintf(question_query, sizeof(question_query),
             "SELECT q.id, q.question_text, q.option_a, q.option_b, q.option_c, q.option_d, "
             "ua.selected_answer FROM exam_questions q "
             "LEFT JOIN exam_answers ua ON q.id = ua.question_id "
             "AND ua.user_id = %d AND ua.room_id = %d "
             "WHERE q.room_id = %d",
             user_id, room_id, room_id);
    
    if (sqlite3_prepare_v2(db, question_query, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "ERROR|Cannot load questions\n", 28, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Format: RESUME_EXAM_OK|remaining_seconds|q1_id:q1_text:optA:optB:optC:optD:saved_answer|...
    char response[8192];
    snprintf(response, sizeof(response), "RESUME_EXAM_OK|%ld", remaining);
    
    int question_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int q_id = sqlite3_column_int(stmt, 0);
        const char *q_text = (const char *)sqlite3_column_text(stmt, 1);
        const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
        const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
        const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
        const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);
        
        // saved_answer có thể NULL nếu chưa trả lời
        int saved_answer = -1;
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
            saved_answer = sqlite3_column_int(stmt, 6);
        }
        
        char q_entry[1024];
        snprintf(q_entry, sizeof(q_entry), "|%d:%s:%s:%s:%s:%s:%d",
                 q_id, q_text, opt_a, opt_b, opt_c, opt_d, saved_answer);
        strcat(response, q_entry);
        question_count++;
    }
    
    sqlite3_finalize(stmt);
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
    printf("[INFO] User %d resumed exam in room %d (%ld seconds remaining)\n", 
           user_id, room_id, remaining);
    
    pthread_mutex_unlock(&server_data.lock);
}

// Load tất cả rooms vào in-memory structure
void load_rooms_from_db(void) {
    pthread_mutex_lock(&server_data.lock);
    
    server_data.room_count = 0;
    memset(server_data.rooms, 0, sizeof(server_data.rooms));
    
    char query[256];
    snprintf(query, sizeof(query),
             "SELECT id, name, host_id, duration, is_active FROM rooms WHERE is_active = 1");
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW && server_data.room_count < MAX_ROOMS) {
            int idx = server_data.room_count;
            
            server_data.rooms[idx].room_id = sqlite3_column_int(stmt, 0);
            const char *name = (const char *)sqlite3_column_text(stmt, 1);
            if (name) {
                strncpy(server_data.rooms[idx].room_name, name, sizeof(server_data.rooms[idx].room_name) - 1);
            }
            server_data.rooms[idx].creator_id = sqlite3_column_int(stmt, 2);
            server_data.rooms[idx].time_limit = sqlite3_column_int(stmt, 3);
            server_data.rooms[idx].room_status = 0;  // Reset về WAITING
            server_data.rooms[idx].exam_start_time = 0;
            
            // Đếm số câu hỏi
            char count_query[256];
            snprintf(count_query, sizeof(count_query),
                     "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d",
                     server_data.rooms[idx].room_id);
            
            sqlite3_stmt *count_stmt;
            if (sqlite3_prepare_v2(db, count_query, -1, &count_stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(count_stmt) == SQLITE_ROW) {
                    server_data.rooms[idx].num_questions = sqlite3_column_int(count_stmt, 0);
                }
                sqlite3_finalize(count_stmt);
            }
            
            // Init các array
            server_data.rooms[idx].participant_count = 0;
            memset(server_data.rooms[idx].participants, 0, sizeof(server_data.rooms[idx].participants));
            memset(server_data.rooms[idx].answers, -1, sizeof(server_data.rooms[idx].answers));  // -1 = chưa trả lời
            
            server_data.room_count++;
        }
        
        printf("[DB_LOAD] Loaded %d rooms into memory\n", server_data.room_count);
    } else {
        fprintf(stderr, "[DB_LOAD] Failed to load rooms from database\n");
    }
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
}

// Load đáp án của user từ DB vào in-memory (INTERNAL - không lock, assume đã lock)
static void load_room_answers_internal(int room_id, int user_id) {
    // Tìm room index
    int room_idx = -1;
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            room_idx = i;
            break;
        }
    }
    
    if (room_idx == -1) {
        return;
    }
    
    TestRoom *room = &server_data.rooms[room_idx];
    
    // Tìm user index
    int user_idx = -1;
    for (int i = 0; i < room->participant_count; i++) {
        if (room->participants[i] == user_id) {
            user_idx = i;
            break;
        }
    }
    
    if (user_idx == -1) {
        return;
    }
    
    // Load answers từ DB
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT ua.question_id, ua.selected_answer, ua.answered_at "
             "FROM exam_answers ua "
             "WHERE ua.user_id = %d AND ua.room_id = %d",
             user_id, room_id);
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int question_id = sqlite3_column_int(stmt, 0);
            int selected_answer = sqlite3_column_int(stmt, 1);
            long answered_at = sqlite3_column_int64(stmt, 2);
            
            // Tìm question index
            char idx_query[256];
            snprintf(idx_query, sizeof(idx_query),
                     "SELECT COUNT(*) - 1 FROM exam_questions WHERE room_id = %d AND id <= %d",
                     room_id, question_id);
            
            sqlite3_stmt *idx_stmt;
            int question_idx = -1;
            if (sqlite3_prepare_v2(db, idx_query, -1, &idx_stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(idx_stmt) == SQLITE_ROW) {
                    question_idx = sqlite3_column_int(idx_stmt, 0);
                }
                sqlite3_finalize(idx_stmt);
            }
            
            if (question_idx >= 0 && question_idx < MAX_QUESTIONS) {
                room->answers[user_idx][question_idx].user_id = user_id;
                room->answers[user_idx][question_idx].answer = selected_answer;
                room->answers[user_idx][question_idx].submit_time = answered_at;
            }
        }
        
        printf("[DB_LOAD] Loaded answers for user %d in room %d\n", user_id, room_id);
    }
    
    sqlite3_finalize(stmt);
}

// Load đáp án của user từ DB vào in-memory (PUBLIC - có lock)
void load_room_answers(int room_id, int user_id) {
    pthread_mutex_lock(&server_data.lock);
    load_room_answers_internal(room_id, user_id);
    pthread_mutex_unlock(&server_data.lock);
}

// Handle GET_USER_ROOMS request
void handle_get_user_rooms(int socket_fd, int user_id) {
    pthread_mutex_lock(&server_data.lock);
    
    char response[BUFFER_SIZE * 2] = "ROOMS_LIST";
    int has_rooms = 0;
    
    for (int i = 0; i < server_data.room_count; i++) {
        TestRoom *room = &server_data.rooms[i];
        
        if (room->creator_id == user_id) {
            has_rooms = 1;
            char room_entry[256];
            snprintf(room_entry, sizeof(room_entry), "|%d:%s", 
                    room->room_id, room->room_name);
            strcat(response, room_entry);
        }
    }
    
    pthread_mutex_unlock(&server_data.lock);
    
    if (!has_rooms) {
        send(socket_fd, "NO_ROOMS\n", 9, 0);
    } else {
        strcat(response, "\n");
        send(socket_fd, response, strlen(response), 0);
    }
}

// Get exam students status (ACTIVE, SUBMITTED, DISCONNECTED, NOT_STARTED)
void get_exam_students_status(int socket_fd, int user_id, int room_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find room and verify permission
    TestRoom *room = NULL;
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            room = &server_data.rooms[i];
            break;
        }
    }
    
    char response[BUFFER_SIZE * 2];
    
    if (room == NULL) {
        snprintf(response, sizeof(response), "EXAM_STUDENTS_FAIL|Room not found\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->creator_id != user_id) {
        snprintf(response, sizeof(response), "EXAM_STUDENTS_FAIL|Permission denied\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Build response: EXAM_STUDENTS_LIST|room_id|user_id:username:status;...
    int offset = snprintf(response, sizeof(response), "EXAM_STUDENTS_LIST|%d", room_id);
    
    // Get all participants
    for (int i = 0; i < room->participant_count; i++) {
        int participant_id = room->participants[i];
        char username[50] = "Unknown";
        char status[20] = "NOT_STARTED";
        
        // Get username
        for (int j = 0; j < server_data.user_count; j++) {
            if (server_data.users[j].user_id == participant_id) {
                strncpy(username, server_data.users[j].username, sizeof(username) - 1);
                break;
            }
        }
        
        // Determine status
        // Check if user has submitted (by checking results table in DB)
        char check_query[256];
        snprintf(check_query, sizeof(check_query),
                 "SELECT COUNT(*) FROM test_results WHERE user_id=%d AND room_id=%d",
                 participant_id, room_id);
        
        sqlite3_stmt *check_stmt;
        int has_submitted = 0;
        if (sqlite3_prepare_v2(db, check_query, -1, &check_stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                has_submitted = sqlite3_column_int(check_stmt, 0) > 0;
            }
            sqlite3_finalize(check_stmt);
        }
        
        if (has_submitted) {
            strcpy(status, "SUBMITTED");
        } else {
            // Check if user has any answers (started but not submitted)
            char answer_query[256];
            snprintf(answer_query, sizeof(answer_query),
                     "SELECT COUNT(*) FROM exam_answers WHERE user_id=%d AND room_id=%d",
                     participant_id, room_id);
            
            sqlite3_stmt *ans_stmt;
            int has_answers = 0;
            if (sqlite3_prepare_v2(db, answer_query, -1, &ans_stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(ans_stmt) == SQLITE_ROW) {
                    has_answers = sqlite3_column_int(ans_stmt, 0) > 0;
                }
                sqlite3_finalize(ans_stmt);
            }
            
            if (has_answers) {
                // Has started, check if online
                int is_online = 0;
                for (int j = 0; j < server_data.user_count; j++) {
                    if (server_data.users[j].user_id == participant_id && 
                        server_data.users[j].is_online == 1) {
                        is_online = 1;
                        break;
                    }
                }
                strcpy(status, is_online ? "ACTIVE" : "DISCONNECTED");
            } else {
                strcpy(status, "NOT_STARTED");
            }
        }
        
        offset += snprintf(response + offset, sizeof(response) - offset,
                         "|%d:%s:%s", participant_id, username, status);
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
    printf("[GET_EXAM_STUDENTS] Success: Sent status of %d students in room %d to user %d\n",
           room->participant_count, room_id, user_id);
    
    pthread_mutex_unlock(&server_data.lock);
}

// Get all questions in an exam room
void get_room_questions(int socket_fd, int user_id, int room_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find room and verify permission
    TestRoom *room = NULL;
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            room = &server_data.rooms[i];
            break;
        }
    }
    
    char response[BUFFER_SIZE * 3];
    
    if (room == NULL) {
        snprintf(response, sizeof(response), "ROOM_QUESTIONS_FAIL|Room not found\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->creator_id != user_id) {
        snprintf(response, sizeof(response), "ROOM_QUESTIONS_FAIL|Permission denied\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Get questions from database
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id, question_text, option_a, option_b, option_c, option_d, "
             "correct_answer, difficulty, category FROM exam_questions WHERE room_id=%d ORDER BY id",
             room_id);
    
    sqlite3_stmt *stmt;
    int offset = snprintf(response, sizeof(response), "ROOM_QUESTIONS_LIST|%d", room_id);
    int question_count = 0;
    
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int q_id = sqlite3_column_int(stmt, 0);
            const char *q_text = (const char *)sqlite3_column_text(stmt, 1);
            const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
            const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
            const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
            const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);
            int correct = sqlite3_column_int(stmt, 6);
            const char *difficulty = (const char *)sqlite3_column_text(stmt, 7);
            const char *category = (const char *)sqlite3_column_text(stmt, 8);
            
            offset += snprintf(response + offset, sizeof(response) - offset,
                             "|%d:%s:%s:%s:%s:%s:%d:%s:%s",
                             q_id, q_text ? q_text : "",
                             opt_a ? opt_a : "", opt_b ? opt_b : "",
                             opt_c ? opt_c : "", opt_d ? opt_d : "",
                             correct, difficulty ? difficulty : "", category ? category : "");
            question_count++;
        }
        sqlite3_finalize(stmt);
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
    printf("[GET_ROOM_QUESTIONS] Success: Sent %d questions from room %d to user %d\n",
           question_count, room_id, user_id);
    
    pthread_mutex_unlock(&server_data.lock);
}

// Get single question detail for editing
void get_question_detail(int socket_fd, int user_id, int room_id, int question_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Verify room ownership
    TestRoom *room = NULL;
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            room = &server_data.rooms[i];
            break;
        }
    }
    
    if (room == NULL || room->creator_id != user_id) {
        send(socket_fd, "QUESTION_DETAIL_FAIL|Permission denied\n", 40, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Query question from database
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id, question_text, option_a, option_b, option_c, option_d, "
             "correct_answer, difficulty, category FROM exam_questions WHERE id=%d AND room_id=%d",
             question_id, room_id);
    
    sqlite3_stmt *stmt;
    char response[BUFFER_SIZE];
    
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int q_id = sqlite3_column_int(stmt, 0);
            const char *q_text = (const char *)sqlite3_column_text(stmt, 1);
            const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
            const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
            const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
            const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);
            int correct = sqlite3_column_int(stmt, 6);
            const char *difficulty = (const char *)sqlite3_column_text(stmt, 7);
            const char *category = (const char *)sqlite3_column_text(stmt, 8);
            
            snprintf(response, sizeof(response),
                    "QUESTION_DETAIL|%d|%s|%s|%s|%s|%s|%d|%s|%s\n",
                    q_id, q_text ? q_text : "",
                    opt_a ? opt_a : "", opt_b ? opt_b : "",
                    opt_c ? opt_c : "", opt_d ? opt_d : "",
                    correct, difficulty ? difficulty : "", category ? category : "");
            send(socket_fd, response, strlen(response), 0);
        } else {
            send(socket_fd, "QUESTION_DETAIL_FAIL|Question not found\n", 41, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        send(socket_fd, "QUESTION_DETAIL_FAIL|Database error\n", 37, 0);
    }
    
    pthread_mutex_unlock(&server_data.lock);
}

// Update exam question
void update_exam_question(int socket_fd, int user_id, int room_id, int question_id, char *new_data) {
    pthread_mutex_lock(&server_data.lock);
    
    // Verify room ownership
    TestRoom *room = NULL;
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            room = &server_data.rooms[i];
            break;
        }
    }
    
    if (room == NULL || room->creator_id != user_id) {
        send(socket_fd, "UPDATE_QUESTION_FAIL|Permission denied\n", 40, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Parse new_data: question_text|opt1|opt2|opt3|opt4|correct|difficulty|category
    char data_copy[2048];
    strncpy(data_copy, new_data, sizeof(data_copy) - 1);
    data_copy[sizeof(data_copy) - 1] = '\0';
    
    char *q_text = strtok(data_copy, "|");
    char *opt1 = strtok(NULL, "|");
    char *opt2 = strtok(NULL, "|");
    char *opt3 = strtok(NULL, "|");
    char *opt4 = strtok(NULL, "|");
    char *correct_str = strtok(NULL, "|");
    char *difficulty = strtok(NULL, "|");
    char *category = strtok(NULL, "|");
    
    if (!q_text || !opt1 || !opt2 || !opt3 || !opt4 || !correct_str || !difficulty || !category) {
        send(socket_fd, "UPDATE_QUESTION_FAIL|Invalid data format\n", 42, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int correct_answer = atoi(correct_str);
    
    // Update in database using sqlite3_mprintf for safety
    char *query = sqlite3_mprintf(
        "UPDATE exam_questions SET question_text=%Q, option_a=%Q, option_b=%Q, "
        "option_c=%Q, option_d=%Q, correct_answer=%d, difficulty=%Q, category=%Q "
        "WHERE id=%d AND room_id=%d",
        q_text, opt1, opt2, opt3, opt4, correct_answer, difficulty, category,
        question_id, room_id);
    
    if (!query) {
        send(socket_fd, "UPDATE_QUESTION_FAIL|Memory error\n", 35, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, query, NULL, NULL, &err_msg);
    sqlite3_free(query);
    
    if (rc == SQLITE_OK) {
        send(socket_fd, "UPDATE_QUESTION_OK\n", 19, 0);
        printf("[UPDATE_QUESTION] Question %d updated in room %d by user %d\n",
               question_id, room_id, user_id);
    } else {
        send(socket_fd, "UPDATE_QUESTION_FAIL|Database error\n", 37, 0);
        if (err_msg) {
            fprintf(stderr, "[UPDATE_QUESTION] Error: %s\n", err_msg);
            sqlite3_free(err_msg);
        }
    }
    
    pthread_mutex_unlock(&server_data.lock);
}


// Update a room question
void update_room_question(int socket_fd, int user_id, int room_id, int question_id, char *new_data) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find room and verify permission
    TestRoom *room = NULL;
    for (int i = 0; i < server_data.room_count; i++) {
        if (server_data.rooms[i].room_id == room_id) {
            room = &server_data.rooms[i];
            break;
        }
    }
    
    char response[256];
    
    if (room == NULL) {
        snprintf(response, sizeof(response), "UPDATE_ROOM_QUESTION_FAIL|Room not found\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->creator_id != user_id) {
        snprintf(response, sizeof(response), "UPDATE_ROOM_QUESTION_FAIL|Permission denied\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Parse new_data: question_text|opt1|opt2|opt3|opt4|correct|difficulty|category
    char data_copy[2048];
    strncpy(data_copy, new_data, sizeof(data_copy) - 1);
    
    char *q_text = strtok(data_copy, "|");
    char *opt1 = strtok(NULL, "|");
    char *opt2 = strtok(NULL, "|");
    char *opt3 = strtok(NULL, "|");
    char *opt4 = strtok(NULL, "|");
    char *correct_str = strtok(NULL, "|");
    char *difficulty = strtok(NULL, "|");
    char *category = strtok(NULL, "|");
    
    if (!q_text || !opt1 || !opt2 || !opt3 || !opt4 || !correct_str || !difficulty || !category) {
        snprintf(response, sizeof(response), "UPDATE_ROOM_QUESTION_FAIL|Invalid data format\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int correct_answer = atoi(correct_str);
    
    // Update in database
    char query[2048];
    char *err_msg = 0;
    snprintf(query, sizeof(query),
             "UPDATE exam_questions SET question_text='%s', option_a='%s', option_b='%s', "
             "option_c='%s', option_d='%s', correct_answer=%d, difficulty='%s', category='%s' "
             "WHERE id=%d AND room_id=%d;",
             q_text, opt1, opt2, opt3, opt4, correct_answer, difficulty, category,
             question_id, room_id);
    
    if (sqlite3_exec(db, query, 0, 0, &err_msg) != SQLITE_OK) {
        snprintf(response, sizeof(response), "UPDATE_ROOM_QUESTION_FAIL|Database error\n");
        fprintf(stderr, "[UPDATE_ROOM_QUESTION] DB error: %s\n", err_msg);
        sqlite3_free(err_msg);
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Update in-memory
    for (int i = 0; i < server_data.question_count; i++) {
        if (server_data.questions[i].id == question_id) {
            strncpy(server_data.questions[i].text, q_text, sizeof(server_data.questions[i].text) - 1);
            strncpy(server_data.questions[i].options[0], opt1, sizeof(server_data.questions[i].options[0]) - 1);
            strncpy(server_data.questions[i].options[1], opt2, sizeof(server_data.questions[i].options[1]) - 1);
            strncpy(server_data.questions[i].options[2], opt3, sizeof(server_data.questions[i].options[2]) - 1);
            strncpy(server_data.questions[i].options[3], opt4, sizeof(server_data.questions[i].options[3]) - 1);
            server_data.questions[i].correct_answer = correct_answer;
            strncpy(server_data.questions[i].difficulty, difficulty, sizeof(server_data.questions[i].difficulty) - 1);
            strncpy(server_data.questions[i].category, category, sizeof(server_data.questions[i].category) - 1);
            break;
        }
    }
    
    snprintf(response, sizeof(response), "UPDATE_ROOM_QUESTION_OK|Question updated successfully\n");
    send(socket_fd, response, strlen(response), 0);
    
    printf("[UPDATE_ROOM_QUESTION] Success: Question %d updated in room %d by user %d\n",
           question_id, room_id, user_id);
    log_activity(user_id, "UPDATE_ROOM_QUESTION", "Updated exam question");
    
    pthread_mutex_unlock(&server_data.lock);
}

// Get room members/participants
void get_room_members(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);
  
  // Check if user is the room creator
  TestRoom *room = NULL;
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      room = &server_data.rooms[i];
      break;
    }
  }
  
  if (room == NULL || room->creator_id != user_id) {
    char response[] = "ROOM_MEMBERS_FAIL|Permission denied\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Query database for room participants from participants table
  // Show all participants and their best score (if they have completed the exam)
  const char *sql = "SELECT DISTINCT u.id, u.username, "
                    "COALESCE(MAX(r.score), -1) as best_score "
                    "FROM participants p "
                    "JOIN users u ON p.user_id = u.id "
                    "LEFT JOIN results r ON u.id = r.user_id AND r.room_id = p.room_id "
                    "WHERE p.room_id = ? "
                    "GROUP BY u.id, u.username "
                    "ORDER BY best_score DESC;";
  
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    char response[] = "ROOM_MEMBERS_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  sqlite3_bind_int(stmt, 1, room_id);
  
  char response[BUFFER_SIZE];
  int offset = snprintf(response, sizeof(response), "ROOM_MEMBERS|%d|", room_id);
  
  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int uid = sqlite3_column_int(stmt, 0);
    const char *username = (const char*)sqlite3_column_text(stmt, 1);
    int score = sqlite3_column_int(stmt, 2);
    
    offset += snprintf(response + offset, sizeof(response) - offset,
                       "%d:%s:%d;", uid, username, score);
    count++;
  }
  
  sqlite3_finalize(stmt);
  
  // Add count after room_id
  char final_response[BUFFER_SIZE];
  snprintf(final_response, sizeof(final_response), "ROOM_MEMBERS|%d|%d|", room_id, count);
  
  if (count == 0) {
    strcat(final_response, "NONE");
  } else {
    // Copy member data
    strcat(final_response, strchr(response + 14, '|') + 1);
  }
  
  strcat(final_response, "\n");
  send(socket_fd, final_response, strlen(final_response), 0);
  
  pthread_mutex_unlock(&server_data.lock);
}
