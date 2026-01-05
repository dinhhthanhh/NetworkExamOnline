#include "rooms.h"
#include "db.h"
#include "results.h"
#include "network.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

// Forward declaration
static void load_room_answers_internal(int room_id, int user_id);

void create_test_room(int socket_fd, int creator_id, char *room_name, int num_q, int time_limit) {
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

  // ===== THÊM VÀO IN-MEMORY =====
  
  if (server_data.room_count < MAX_ROOMS) {
      int idx = server_data.room_count;
      server_data.rooms[idx].room_id = room_id;
      strncpy(server_data.rooms[idx].room_name, room_name, sizeof(server_data.rooms[idx].room_name) - 1);
      server_data.rooms[idx].creator_id = creator_id;
      server_data.rooms[idx].time_limit = time_limit;
      server_data.rooms[idx].room_status = 0;  // WAITING - chưa bắt đầu
      server_data.rooms[idx].exam_start_time = 0;
      server_data.rooms[idx].num_questions = 0;  // chưa có câu hỏi
      server_data.rooms[idx].participant_count = 0;
      
      // Init arrays
      memset(server_data.rooms[idx].participants, 0, sizeof(server_data.rooms[idx].participants));
      memset(server_data.rooms[idx].answers, -1, sizeof(server_data.rooms[idx].answers));
      
      server_data.room_count++;
      printf("[CREATE_ROOM] Added room '%s' (ID=%d) to in-memory at idx=%d\n", 
             room_name, room_id, idx);
  }

  // ===== GỬI RESPONSE =====
  
  char response[256];
  snprintf(response, sizeof(response), 
           "CREATE_ROOM_OK|%d|%s|%d\n", 
           room_id, room_name, time_limit);
  send(socket_fd, response, strlen(response), 0);

  // ===== LOGGING =====
  
  char log_details[256];
  snprintf(log_details, sizeof(log_details), 
           "Room: %s (ID=%d, Time=%dm)", 
           room_name, room_id, time_limit);
  log_activity(creator_id, "CREATE_ROOM", log_details);

  printf("[INFO] User %d created room '%s' (ID=%d)\n", 
         creator_id, room_name, room_id);
  
  pthread_mutex_unlock(&server_data.lock);
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
    "LEFT JOIN questions q ON r.id = q.room_id "
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
    "LEFT JOIN questions q ON r.id = q.room_id "
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
           "SELECT COUNT(*) FROM questions WHERE room_id = %d", room_id);
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

  // Update database status AND exam_start_time
  char update_sql[256];
  snprintf(update_sql, sizeof(update_sql),
           "UPDATE rooms SET room_status = 1, exam_start_time = %ld WHERE id = %d", 
           start_time, room_id);
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
                 "SELECT name, host_id, duration, room_status, exam_start_time FROM rooms WHERE id = %d", room_id);
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
                server_data.rooms[room_idx].room_status = sqlite3_column_int(room_stmt, 3);  // Load từ DB
                server_data.rooms[room_idx].exam_start_time = sqlite3_column_int64(room_stmt, 4);  // Load từ DB
                
                // Đếm số câu hỏi
                char count_query[256];
                snprintf(count_query, sizeof(count_query),
                         "SELECT COUNT(*) FROM questions WHERE room_id = %d", room_id);
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
    
    // Case 2: ENDED - Đã kết thúc hoặc hết giờ
    if (room->room_status == 2) {  // ENDED
        // Check user đã submit chưa
        char check_result[256];
        snprintf(check_result, sizeof(check_result),
                 "SELECT id FROM results WHERE room_id = %d AND user_id = %d",
                 room_id, user_id);
        
        sqlite3_stmt *check_stmt;
        int already_submitted = 0;
        if (sqlite3_prepare_v2(db, check_result, -1, &check_stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                already_submitted = 1;
            }
            sqlite3_finalize(check_stmt);
        }
        
        if (already_submitted) {
            send(socket_fd, "ERROR|Exam has ended and you have submitted\n", 44, 0);
        } else {
            send(socket_fd, "ERROR|Exam has ended\n", 21, 0);
        }
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Case 2.5: STARTED nhưng đã hết giờ (timer chưa kịp xử lý)
    if (room->room_status == 1) {  // STARTED
        time_t now = time(NULL);
        long elapsed = now - room->exam_start_time;
        long duration_seconds = room->time_limit * 60;
        
        if (elapsed >= duration_seconds) {
            // Hết giờ - update status và báo lỗi
            room->room_status = 2;  // ENDED
            
            // Update DB
            char update_query[256];
            snprintf(update_query, sizeof(update_query),
                     "UPDATE rooms SET room_status = 2 WHERE id = %d", room_id);
            sqlite3_exec(db, update_query, NULL, NULL, NULL);
            
            printf("[BEGIN_EXAM] Room %d time expired - marking as ENDED\n", room_id);
            send(socket_fd, "ERROR|Exam time expired\n", 24, 0);
            pthread_mutex_unlock(&server_data.lock);
            return;
        }
    }
    
    // Case 3: STARTED - Đang thi
    if (room->room_status != 1) {
        send(socket_fd, "ERROR|Invalid room status\n", 26, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Tính thời gian còn lại và check timeout
    time_t now = time(NULL);
    long elapsed = now - room->exam_start_time;
    long duration_seconds = room->time_limit * 60;
    long remaining = duration_seconds - elapsed;
    
    if (remaining <= 0) {
        // Hết giờ - update status
        room->room_status = 2;  // ENDED
        
        char update_query[256];
        snprintf(update_query, sizeof(update_query),
                 "UPDATE rooms SET room_status = 2 WHERE id = %d", room_id);
        sqlite3_exec(db, update_query, NULL, NULL, NULL);
        
        printf("[BEGIN_EXAM] Room %d time expired during BEGIN_EXAM\n", room_id);
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
             "FROM questions WHERE room_id = %d", room_id);
    
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
    
    // Nếu hết thời gian
    if (remaining <= 0) {
        // Check lại xem timer đã auto-submit chưa
        char recheck_result[256];
        snprintf(recheck_result, sizeof(recheck_result),
                 "SELECT id FROM results WHERE room_id = %d AND user_id = %d",
                 room_id, user_id);
        
        int was_auto_submitted = 0;
        if (sqlite3_prepare_v2(db, recheck_result, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                was_auto_submitted = 1;
            }
            sqlite3_finalize(stmt);
        }
        
        if (!was_auto_submitted) {
            // Timer chưa kịp auto-submit → submit ngay
            printf("[RESUME] User %d timeout - calling auto_submit\n", user_id);
            
            // Unlock trước khi gọi auto_submit (tránh deadlock)
            pthread_mutex_unlock(&server_data.lock);
            auto_submit_on_disconnect(user_id, room_id);
            pthread_mutex_lock(&server_data.lock);
            
            printf("[RESUME] Manual auto-submit completed for user %d\n", user_id);
        }
        
        // Update room status nếu cần
        int room_idx = -1;
        for (int i = 0; i < server_data.room_count; i++) {
            if (server_data.rooms[i].room_id == room_id) {
                room_idx = i;
                break;
            }
        }
        if (room_idx != -1 && server_data.rooms[room_idx].room_status == 1) {
            server_data.rooms[room_idx].room_status = 2;
            char update_query[256];
            snprintf(update_query, sizeof(update_query),
                     "UPDATE rooms SET room_status = 2 WHERE id = %d", room_id);
            sqlite3_exec(db, update_query, NULL, NULL, NULL);
        }
        
        send(socket_fd, "RESUME_TIME_EXPIRED\n", 20, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Lấy danh sách câu hỏi và câu trả lời đã lưu
    char question_query[512];
    snprintf(question_query, sizeof(question_query),
             "SELECT q.id, q.question_text, q.option_a, q.option_b, q.option_c, q.option_d, "
             "ua.selected_answer FROM questions q "
             "LEFT JOIN user_answers ua ON q.id = ua.question_id "
             "AND ua.user_id = %d AND ua.room_id = %d "
             "WHERE q.room_id = %d",
             user_id, room_id, room_id);
    
    if (sqlite3_prepare_v2(db, question_query, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "ERROR|Cannot load questions\n", 28, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Format: RESUME_EXAM_OK|remaining_seconds|duration_minutes|q1_id:q1_text:optA:optB:optC:optD:saved_answer|...
    // THÊM duration_minutes để client tính toán exam_start_time chính xác
    char response[8192];
    snprintf(response, sizeof(response), "RESUME_EXAM_OK|%ld|%d", remaining, duration_minutes);
    
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
    
    printf("[INFO] User %d resumed exam in room %d (%ld seconds remaining, %d min duration)\n", 
           user_id, room_id, remaining, duration_minutes);
    
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
                     "SELECT COUNT(*) FROM questions WHERE room_id = %d",
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
             "FROM user_answers ua "
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
                     "SELECT COUNT(*) - 1 FROM questions WHERE room_id = %d AND id <= %d",
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
