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

/*
 * Tạo một phòng thi mới:
 *  - Chỉ cho phép admin (kiểm tra role trong bảng users)
 *  - Validate tên phòng, thời gian, số lượng câu hỏi theo độ khó
 *  - Lưu cấu hình easy/medium/hard vào bảng rooms
 *  - Câu hỏi sẽ được chọn ngẫu nhiên theo cấu hình này khi admin START_ROOM
 */
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
        server_send(socket_fd, response);
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
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  // Kiểm tra room_name hợp lệ
  if (room_name == NULL || strlen(room_name) == 0) {
    char response[] = "CREATE_ROOM_FAIL|Room name cannot be empty\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // time_limit validation
  if (time_limit <= 0 || time_limit > 180) {
    char response[] = "CREATE_ROOM_FAIL|Invalid time limit (1-180 minutes)\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Validate question counts (only check max, min is now allowed to be 0)
  // Question counts can be 0 - admin will configure in Question Manager
  int total_questions = easy_count + medium_count + hard_count;
  if (total_questions > 50) {
    char response[] = "CREATE_ROOM_FAIL|Too many questions (max 50)\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Kiểm tra room_name đã tồn tại chưa
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT id FROM rooms WHERE name = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    char response[] = "CREATE_ROOM_FAIL|Database error\n";
    server_send(socket_fd, response);
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
  // Lưu thêm easy_count, medium_count, hard_count để dùng khi START_ROOM chọn câu hỏi
  const char *sql_insert = 
    "INSERT INTO rooms (name, host_id, duration, room_status, exam_start_time, easy_count, medium_count, hard_count) "
    "VALUES (?, ?, ?, 0, 0, ?, ?, ?);";
  
  rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Failed to prepare insert: %s\n", sqlite3_errmsg(db));
    char response[] = "CREATE_ROOM_FAIL|Database error\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, creator_id);
  sqlite3_bind_int(stmt, 3, time_limit);
  sqlite3_bind_int(stmt, 4, easy_count);
  sqlite3_bind_int(stmt, 5, medium_count);
  sqlite3_bind_int(stmt, 6, hard_count);

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
      // Ở thời điểm tạo room chưa chọn câu hỏi cụ thể, dùng tổng cấu hình
      server_data.rooms[idx].num_questions = total_questions;  // số câu hỏi dự kiến
      server_data.rooms[idx].participant_count = 0;
      
      // Init arrays
      memset(server_data.rooms[idx].participants, 0, sizeof(server_data.rooms[idx].participants));
      memset(server_data.rooms[idx].answers, -1, sizeof(server_data.rooms[idx].answers));
      
      server_data.room_count++;
  }

  // ===== GỬI RESPONSE =====
  
  char response[512];
  snprintf(response, sizeof(response), 
           "CREATE_ROOM_OK|%d|%s|%d|%d|Easy:%d Medium:%d Hard:%d\n", 
           room_id, room_name, time_limit, total_questions, easy_count, medium_count, hard_count);
  server_send(socket_fd, response);

  // ===== LOGGING =====
  
  char log_details[512];
  snprintf(log_details, sizeof(log_details), 
           "Room: %s (ID=%d, Time=%dm, Questions=%d [E:%d M:%d H:%d])", 
           room_name, room_id, time_limit, total_questions, easy_count, medium_count, hard_count);
  log_activity(creator_id, "CREATE_ROOM", log_details);

  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Liệt kê danh sách các phòng thi đang mở:
 *  - Join với users để lấy tên host
 *  - Đếm số câu hỏi của từng phòng và trả về cho client.
 */
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
    server_send(socket_fd, response);
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

  response[offset] = '\0';
  server_send(socket_fd, response);
  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Cho phép user tham gia một phòng thi:
 *  - Kiểm tra room tồn tại và đang mở
 *  - Kiểm tra user đã thi phòng này chưa (has_taken_exam)
 *  - Thêm bản ghi participants để theo dõi thời gian bắt đầu bài thi.
 */
void join_test_room(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra room có tồn tại
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT is_active FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "JOIN_ROOM_FAIL|Database error\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW) {
    char response[] = "JOIN_ROOM_FAIL|Room not found\n";
    server_send(socket_fd, response);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int is_active = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  // Kiểm tra room có OPEN không
  if (is_active != 1) {
    char response[] = "JOIN_ROOM_FAIL|Room is closed\n";
    server_send(socket_fd, response);
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
        server_send(socket_fd, response);
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
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  sqlite3_bind_int(stmt, 2, user_id);
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    char response[] = "JOIN_ROOM_FAIL|Failed to join\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  char response[] = "JOIN_ROOM_OK\n";
  server_send(socket_fd, response);

  char log_details[128];
  snprintf(log_details, sizeof(log_details), "Joined room ID=%d", room_id);
  log_activity(user_id, "JOIN_ROOM", log_details);
  
  pthread_mutex_unlock(&server_data.lock);
}

<<<<<<< HEAD

=======
/*
 * Đóng một phòng thi (host):
 *  - Chỉ host của phòng được đóng room
 *  - Cập nhật is_active trong DB, dùng để chặn JOIN_ROOM tiếp.
 */
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
void close_room(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra room có tồn tại và quyền sở hữu
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id, is_active FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "CLOSE_ROOM_FAIL|Database error\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW) {
    char response[] = "CLOSE_ROOM_FAIL|Room not found\n";
    server_send(socket_fd, response);
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
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Kiểm tra room đã close chưa
  if (is_active == 0) {
    char response[] = "CLOSE_ROOM_FAIL|Room already closed\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Close room (set is_active = 0)
  const char *sql_update = "UPDATE rooms SET is_active = 0 WHERE id = ?;";
  rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "CLOSE_ROOM_FAIL|Database error\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    char response[] = "CLOSE_ROOM_FAIL|Failed to close room\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  char response[128];
  snprintf(response, sizeof(response), "CLOSE_ROOM_OK|Room %d closed\n", room_id);
  server_send(socket_fd, response);

  char log_details[128];
  snprintf(log_details, sizeof(log_details), "Closed room ID=%d", room_id);
  log_activity(user_id, "CLOSE_ROOM", log_details);
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
    "  r.exam_start_time, "
    "  COUNT(q.id) as question_count "
    "FROM rooms r "
    "LEFT JOIN exam_questions q ON r.id = q.room_id "
    "WHERE r.host_id = ? "
    "GROUP BY r.id "
    "ORDER BY r.created_at DESC;";
  
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "LIST_MY_ROOMS_FAIL|Database error\n";
    server_send(socket_fd, response);
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
    long exam_start_time = sqlite3_column_int64(stmt, 4);
    int question_count = sqlite3_column_int(stmt, 5);

    const char *status_str;
    if (room_status == 0) {
      status_str = "Waiting";
    } else if (room_status == 1) {
      time_t now = time(NULL);
      if (now - exam_start_time > (long)duration * 60) {
          status_str = "Ended";
      } else {
          status_str = "Started";
      }
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

  response[offset] = '\0';
  server_send(socket_fd, response);
  pthread_mutex_unlock(&server_data.lock);
}

// Delete a room (admin only)
void delete_room(int socket_fd, int user_id, int room_id) {
  // Danh sách socket của các participant để broadcast sau khi nhả lock
  int participant_sockets[MAX_CLIENTS];
  int participant_count = 0;

  pthread_mutex_lock(&server_data.lock);
  
  // Check if user is admin and room owner, and check room status
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id, room_status, duration, exam_start_time FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "DELETE_ROOM_FAIL|Database error\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);
  
  if (rc != SQLITE_ROW) {
    char response[] = "DELETE_ROOM_FAIL|Room not found\n";
    server_send(socket_fd, response);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  int host_id = sqlite3_column_int(stmt, 0);
  int room_status = sqlite3_column_int(stmt, 1);
  int duration = sqlite3_column_int(stmt, 2);
  long exam_start_time = sqlite3_column_int64(stmt, 3);
  sqlite3_finalize(stmt);
  
  if (host_id != user_id) {
    char response[] = "DELETE_ROOM_FAIL|You are not the owner of this room\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Check if room is deletable: Waiting or Ended or (Started but time's up)
  if (room_status == 1) {
    time_t now = time(NULL);
    if (now - exam_start_time <= (long)duration * 60) {
        char response[] = "DELETE_ROOM_FAIL|Cannot delete room while exam is in progress\n";
        server_send(socket_fd, response);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
  }

  // Tìm room trong in-memory và gom socket của tất cả participants
  int room_idx = -1;
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      room_idx = i;
      break;
    }
  }

  if (room_idx != -1) {
    TestRoom *room = &server_data.rooms[room_idx];
    for (int i = 0; i < room->participant_count && participant_count < MAX_CLIENTS; i++) {
      int pid = room->participants[i];
      // Tìm socket tương ứng trong danh sách user online
      for (int j = 0; j < server_data.user_count && participant_count < MAX_CLIENTS; j++) {
        if (server_data.users[j].user_id == pid && server_data.users[j].is_online == 1) {
          participant_sockets[participant_count++] = server_data.users[j].socket_fd;
          break;
        }
      }
    }

    // Xoá room khỏi in-memory
    for (int j = room_idx; j < server_data.room_count - 1; j++) {
      server_data.rooms[j] = server_data.rooms[j + 1];
    }
    server_data.room_count--;
  }

  pthread_mutex_unlock(&server_data.lock);

  // ===== BROADCAST RA NGOÀI LOCK =====
  char broadcast_msg[256];
  snprintf(broadcast_msg, sizeof(broadcast_msg), "ROOM_DELETED|%d\n", room_id);
  for (int i = 0; i < participant_count; i++) {
    server_send(participant_sockets[i], broadcast_msg);
  }

  // ===== DELETE FROM DATABASE (HARD DELETE) =====
  // Delete associated data: exam_answers, participants, questions
  const char *sqls[] = {
    "DELETE FROM exam_answers WHERE room_id = ?;",
    "DELETE FROM participants WHERE room_id = ?;",
    "DELETE FROM exam_questions WHERE room_id = ?;",  // Permanently delete questions
    "DELETE FROM results WHERE room_id = ?;",
    "DELETE FROM rooms WHERE id = ?;"  // Hard delete the room
  };
  
  for (int i = 0; i < 5; i++) {
    rc = sqlite3_prepare_v2(db, sqls[i], -1, &stmt, 0);
    if (rc == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, room_id);
      rc = sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }
  }
  
  // ===== SEND RESPONSE TO ADMIN CUỐI CÙNG =====
  char response[] = "DELETE_ROOM_OK\n";
  server_send(socket_fd, response);
}

void start_test(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra room có tồn tại trong database không
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id, is_active, easy_count, medium_count, hard_count, selection_mode FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    char response[] = "START_ROOM_FAIL|Database error\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW) {
    char response[] = "START_ROOM_FAIL|Room not found\n";
    server_send(socket_fd, response);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int host_id = sqlite3_column_int(stmt, 0);
  // int is_active = sqlite3_column_int(stmt, 1);  // Không cần check nữa
  int easy_count = sqlite3_column_int(stmt, 2);
  int medium_count = sqlite3_column_int(stmt, 3);
  int hard_count = sqlite3_column_int(stmt, 4);
  int selection_mode = sqlite3_column_int(stmt, 5);  // 0=random, 1=manual
  sqlite3_finalize(stmt);

  // Kiểm tra quyền sở hữu
  if (host_id != user_id) {
    char response[] = "START_ROOM_FAIL|Only creator can start\n";
    server_send(socket_fd, response);
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
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Kiểm tra room đã bắt đầu chưa
  if (server_data.rooms[room_idx].room_status == 1) {  // STARTED
    time_t now = time(NULL);
    if (now - server_data.rooms[room_idx].exam_start_time <= (long)server_data.rooms[room_idx].time_limit * 60) {
        char response[] = "START_ROOM_FAIL|Room already started and in progress\n";
        server_send(socket_fd, response);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
  }

  int selected_total = 0;
  char *err_msg = NULL;

  // ===== CHỌN CÂU HỎI THEO SELECTION MODE =====
  if (selection_mode == 0) {
    // ===== RANDOM SELECTION MODE =====
    // Kiểm tra cấu hình số câu hỏi hợp lệ
    int total_questions = easy_count + medium_count + hard_count;
    if (total_questions <= 0) {
      char response[] = "START_ROOM_FAIL|Invalid question configuration. Please set difficulty counts in Question Manager.\n";
      server_send(socket_fd, response);
      pthread_mutex_unlock(&server_data.lock);
      return;
    }

    // ===== VALIDATE QUESTION AVAILABILITY FOR EACH DIFFICULTY =====
    const char *difficulties[] = {"easy", "medium", "hard"};
    const char *difficulty_names[] = {"Easy", "Medium", "Hard"};
    int required_counts[] = {easy_count, medium_count, hard_count};
    
    for (int d = 0; d < 3; d++) {
      if (required_counts[d] > 0) {
        // Count available questions for this difficulty
        char count_sql[256];
        snprintf(count_sql, sizeof(count_sql),
                 "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d AND TRIM(LOWER(difficulty)) = '%s'",
                 room_id, difficulties[d]);
        
        sqlite3_stmt *count_stmt;
        int available = 0;
        if (sqlite3_prepare_v2(db, count_sql, -1, &count_stmt, NULL) == SQLITE_OK) {
          if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            available = sqlite3_column_int(count_stmt, 0);
          }
          sqlite3_finalize(count_stmt);
        }
        
        if (available < required_counts[d]) {
          char response[256];
          snprintf(response, sizeof(response), 
                   "START_ROOM_FAIL|Not enough %s questions: need %d but have %d\n",
                   difficulty_names[d], required_counts[d], available);
          server_send(socket_fd, response);
          pthread_mutex_unlock(&server_data.lock);
          return;
        }
      }
    }

    // Reset tất cả câu hỏi của room về is_selected = 0
    char reset_sql[256];
    snprintf(reset_sql, sizeof(reset_sql),
             "UPDATE exam_questions SET is_selected = 0 WHERE room_id = %d", room_id);
    if (sqlite3_exec(db, reset_sql, 0, 0, &err_msg) != SQLITE_OK) {
      sqlite3_free(err_msg);
      err_msg = NULL;
    }

    // Với mỗi độ khó, nếu có cấu hình > 0 thì chọn ngẫu nhiên trong ngân hàng exam_questions của room
    // Sử dụng TRIM(LOWER()) để normalize difficulty values và tránh whitespace issues
    int easy_selected = 0;
    int medium_selected = 0;
    int hard_selected = 0;

    // Helper function để chọn câu hỏi theo difficulty
    int counts[] = {easy_count, medium_count, hard_count};
    int *selected_counts[] = {&easy_selected, &medium_selected, &hard_selected};
    
    for (int d = 0; d < 3; d++) {
      if (counts[d] > 0) {
        char diff_query[512];
        snprintf(diff_query, sizeof(diff_query),
                 "SELECT id FROM exam_questions WHERE room_id = %d AND TRIM(LOWER(difficulty)) = '%s' ORDER BY RANDOM() LIMIT %d",
                 room_id, difficulties[d], counts[d]);
        
        sqlite3_stmt *s;
        if (sqlite3_prepare_v2(db, diff_query, -1, &s, NULL) == SQLITE_OK) {
          while (sqlite3_step(s) == SQLITE_ROW) {
            int qid = sqlite3_column_int(s, 0);
            char mark_sql[256];
            snprintf(mark_sql, sizeof(mark_sql),
                     "UPDATE exam_questions SET is_selected = 1 WHERE id = %d", qid);
            if (sqlite3_exec(db, mark_sql, NULL, NULL, NULL) == SQLITE_OK) {
              selected_total++;
              (*selected_counts[d])++;
            }
          }
          sqlite3_finalize(s);
        } else {
        }
      }
    }

    // Log tổng kết số câu đã chọn theo từng difficulty
    printf("[DEBUG] start_test (RANDOM): room=%d, easy=%d/%d, medium=%d/%d, hard=%d/%d, total=%d\n",
           room_id, easy_selected, easy_count, medium_selected, medium_count, 
           hard_selected, hard_count, selected_total);
  } else {
    // ===== MANUAL SELECTION MODE =====
    // Đếm số câu hỏi đã được admin chọn sẵn (is_selected = 1)
    char count_query[256];
    snprintf(count_query, sizeof(count_query),
             "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d AND is_selected = 1", room_id);
    
    sqlite3_stmt *count_stmt;
    if (sqlite3_prepare_v2(db, count_query, -1, &count_stmt, NULL) == SQLITE_OK) {
      if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        selected_total = sqlite3_column_int(count_stmt, 0);
      }
      sqlite3_finalize(count_stmt);
    }
    
    printf("[DEBUG] start_test (MANUAL): room=%d, total=%d\n",
           room_id, selected_total);
  }

  if (selected_total == 0) {
    char response[] = "START_ROOM_FAIL|No questions selected. Please add/select questions before starting.\n";
    server_send(socket_fd, response);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Đồng bộ lại num_questions in-memory theo số câu thực sự chọn được
  if (room_idx != -1) {
    server_data.rooms[room_idx].num_questions = selected_total;
  }

  // Update room status: WAITING -> STARTED (both in-memory and DB)
  time_t start_time = time(NULL);
  server_data.rooms[room_idx].room_status = 1;  // STARTED
  server_data.rooms[room_idx].exam_start_time = start_time;

  // Update database status AND exam_start_time
  char update_sql[256];
  snprintf(update_sql, sizeof(update_sql),
<<<<<<< HEAD
           "UPDATE rooms SET room_status = 1, exam_start_time = %ld WHERE id = %d", 
           start_time, room_id);
  char *err_msg = NULL;
=======
           "UPDATE rooms SET room_status = 1, exam_start_time = %ld WHERE id = %d", (long)start_time, room_id);
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
  if (sqlite3_exec(db, update_sql, 0, 0, &err_msg) != SQLITE_OK) {
    sqlite3_free(err_msg);
  }

  char response[128];
  snprintf(response, sizeof(response), "START_ROOM_OK|Room %d started\n", room_id);
  server_send(socket_fd, response);

  char log_details[128];
  snprintf(log_details, sizeof(log_details), "Started exam in room ID=%d", room_id);
  log_activity(user_id, "START_ROOM", log_details);

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
        
        // Load info từ DB (including room_status and exam_start_time for late joiners)
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
<<<<<<< HEAD
                server_data.rooms[room_idx].room_status = sqlite3_column_int(room_stmt, 3);  // Load từ DB
                server_data.rooms[room_idx].exam_start_time = sqlite3_column_int64(room_stmt, 4);  // Load từ DB
=======
                // ===== FIX: Load actual room_status and exam_start_time from DB =====
                server_data.rooms[room_idx].room_status = sqlite3_column_int(room_stmt, 3);
                server_data.rooms[room_idx].exam_start_time = sqlite3_column_int64(room_stmt, 4);
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
                
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
    }
    
    // Lưu start_time cho user này trong DB
    char update_query[512];
    snprintf(update_query, sizeof(update_query),
             "INSERT OR REPLACE INTO participants (room_id, user_id, start_time) "
             "VALUES (%d, %d, %ld)",
             room_id, user_id, now);
    sqlite3_exec(db, update_query, NULL, NULL, NULL);
    
    // Lấy danh sách câu hỏi (THÊM difficulty vào query)
    char question_query[256];
    snprintf(question_query, sizeof(question_query),
         "SELECT id, question_text, option_a, option_b, option_c, option_d, difficulty "
         "FROM exam_questions WHERE room_id = %d AND is_selected = 1", room_id);
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, question_query, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "ERROR|Cannot load questions\n", 28, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Count questions first for dynamic allocation
    char count_query[256];
    snprintf(count_query, sizeof(count_query),
             "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d AND is_selected = 1", room_id);
    int question_total = 0;
    sqlite3_stmt *count_stmt;
    if (sqlite3_prepare_v2(db, count_query, -1, &count_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            question_total = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }
    
    // Dynamic allocation: 1KB per question
    size_t buf_size = (size_t)question_total * 1024 + 4096;
    char *response = malloc(buf_size);
    if (!response) {
        sqlite3_finalize(stmt);
        send(socket_fd, "ERROR|Memory allocation error\n", 30, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Format: BEGIN_EXAM_OK|remaining_seconds|q1_id:q1_text:optA:optB:optC:optD:difficulty|q2_id:...
    int offset = snprintf(response, buf_size, "BEGIN_EXAM_OK|%ld", remaining);
    
    int question_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int q_id = sqlite3_column_int(stmt, 0);
        const char *q_text = (const char *)sqlite3_column_text(stmt, 1);
        const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
        const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
        const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
        const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);
        const char *difficulty = (const char *)sqlite3_column_text(stmt, 6);
        
        // Normalize difficulty: convert to proper case (easy -> Easy, medium -> Medium, hard -> Hard)
        char normalized_diff[20] = "Medium";  // default
        if (difficulty && strlen(difficulty) > 0) {
            if (strcasecmp(difficulty, "easy") == 0) {
                strcpy(normalized_diff, "Easy");
            } else if (strcasecmp(difficulty, "medium") == 0) {
                strcpy(normalized_diff, "Medium");
            } else if (strcasecmp(difficulty, "hard") == 0) {
                strcpy(normalized_diff, "Hard");
            } else {
                // Keep original but capitalize first letter
                strncpy(normalized_diff, difficulty, sizeof(normalized_diff) - 1);
                if (normalized_diff[0] >= 'a' && normalized_diff[0] <= 'z') {
                    normalized_diff[0] -= 32;
                }
            }
        }
        
        offset += snprintf(response + offset, buf_size - offset, "|%d:%s:%s:%s:%s:%s:%s",
                 q_id, q_text, opt_a, opt_b, opt_c, opt_d, normalized_diff);
        question_count++;
    }
    
    sqlite3_finalize(stmt);
    
    if (question_count == 0) {
        free(response);
        send(socket_fd, "ERROR|No questions in room\n", 27, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    free(response);
    
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
    
<<<<<<< HEAD
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
=======
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
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
    }
    
    // Lấy danh sách câu hỏi và câu trả lời đã lưu (THÊM difficulty)
    char question_query[512];
    snprintf(question_query, sizeof(question_query),
         "SELECT q.id, q.question_text, q.option_a, q.option_b, q.option_c, q.option_d, "
         "q.difficulty, ua.selected_answer FROM exam_questions q "
             "LEFT JOIN exam_answers ua ON q.id = ua.question_id "
             "AND ua.user_id = %d AND ua.room_id = %d "
         "WHERE q.room_id = %d AND q.is_selected = 1",
             user_id, room_id, room_id);
    
    if (sqlite3_prepare_v2(db, question_query, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "ERROR|Cannot load questions\n", 28, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
<<<<<<< HEAD
    // Format: RESUME_EXAM_OK|remaining_seconds|duration_minutes|q1_id:q1_text:optA:optB:optC:optD:saved_answer|...
    // THÊM duration_minutes để client tính toán exam_start_time chính xác
    char response[8192];
    snprintf(response, sizeof(response), "RESUME_EXAM_OK|%ld|%d", remaining, duration_minutes);
=======
    // Count questions for dynamic allocation
    char count_query[256];
    snprintf(count_query, sizeof(count_query),
             "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d AND is_selected = 1", room_id);
    int question_total = 0;
    sqlite3_stmt *count_stmt;
    if (sqlite3_prepare_v2(db, count_query, -1, &count_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            question_total = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }
    
    // Dynamic allocation: 1KB per question
    size_t buf_size = (size_t)question_total * 1024 + 4096;
    char *response = malloc(buf_size);
    if (!response) {
        sqlite3_finalize(stmt);
        send(socket_fd, "ERROR|Memory allocation error\n", 30, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Format: RESUME_EXAM_OK|remaining_seconds|q1_id:q1_text:optA:optB:optC:optD:difficulty:saved_answer|...
    int offset = snprintf(response, buf_size, "RESUME_EXAM_OK|%ld", remaining);
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
    
    int question_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int q_id = sqlite3_column_int(stmt, 0);
        const char *q_text = (const char *)sqlite3_column_text(stmt, 1);
        const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
        const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
        const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
        const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);
        const char *difficulty = (const char *)sqlite3_column_text(stmt, 6);
        
        // Normalize difficulty
        char normalized_diff[20] = "Medium";
        if (difficulty && strlen(difficulty) > 0) {
            if (strcasecmp(difficulty, "easy") == 0) {
                strcpy(normalized_diff, "Easy");
            } else if (strcasecmp(difficulty, "medium") == 0) {
                strcpy(normalized_diff, "Medium");
            } else if (strcasecmp(difficulty, "hard") == 0) {
                strcpy(normalized_diff, "Hard");
            } else {
                strncpy(normalized_diff, difficulty, sizeof(normalized_diff) - 1);
                if (normalized_diff[0] >= 'a' && normalized_diff[0] <= 'z') {
                    normalized_diff[0] -= 32;
                }
            }
        }
        
        // saved_answer có thể NULL nếu chưa trả lời
        int saved_answer = -1;
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
            saved_answer = sqlite3_column_int(stmt, 7);
        }
        
        offset += snprintf(response + offset, buf_size - offset, "|%d:%s:%s:%s:%s:%s:%s:%d",
                 q_id, q_text, opt_a, opt_b, opt_c, opt_d, normalized_diff, saved_answer);
        question_count++;
    }
    
    sqlite3_finalize(stmt);
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
<<<<<<< HEAD
    
    printf("[INFO] User %d resumed exam in room %d (%ld seconds remaining, %d min duration)\n", 
           user_id, room_id, remaining, duration_minutes);
=======
    free(response);
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
    
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
            
            // Đếm số câu hỏi đã được chọn (is_selected = 1) cho đề thi
            char count_query[256];
            snprintf(count_query, sizeof(count_query),
                     "SELECT COUNT(*) FROM exam_questions WHERE room_id = %d AND is_selected = 1",
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
         "JOIN exam_questions q ON ua.question_id = q.id "
         "WHERE ua.user_id = %d AND ua.room_id = %d AND q.is_selected = 1",
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
                     "SELECT COUNT(*) - 1 FROM exam_questions WHERE room_id = %d AND is_selected = 1 AND id <= %d",
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

// Get all questions in an exam room (includes is_selected status and selection_mode)
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
    
    // Get selection_mode and difficulty counts from database
    int selection_mode = 0;
    int easy_count = 0, medium_count = 0, hard_count = 0;
    char mode_query[256];
    snprintf(mode_query, sizeof(mode_query), 
             "SELECT selection_mode, easy_count, medium_count, hard_count FROM rooms WHERE id=%d", room_id);
    sqlite3_stmt *mode_stmt;
    if (sqlite3_prepare_v2(db, mode_query, -1, &mode_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(mode_stmt) == SQLITE_ROW) {
            selection_mode = sqlite3_column_int(mode_stmt, 0);
            easy_count = sqlite3_column_int(mode_stmt, 1);
            medium_count = sqlite3_column_int(mode_stmt, 2);
            hard_count = sqlite3_column_int(mode_stmt, 3);
        }
        sqlite3_finalize(mode_stmt);
    }
    
    // Get questions from database (now includes is_selected)
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id, question_text, option_a, option_b, option_c, option_d, "
             "correct_answer, difficulty, category, is_selected FROM exam_questions WHERE room_id=%d ORDER BY id",
             room_id);
    
    sqlite3_stmt *stmt;
    // Format: ROOM_QUESTIONS_LIST|room_id|selection_mode|easy_count|medium_count|hard_count|question1|question2|...
    int offset = snprintf(response, sizeof(response), "ROOM_QUESTIONS_LIST|%d|%d|%d|%d|%d", 
                          room_id, selection_mode, easy_count, medium_count, hard_count);
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
            int is_selected = sqlite3_column_int(stmt, 9);
            
            // Format: id:text:optA:optB:optC:optD:correct:difficulty:category:is_selected
            offset += snprintf(response + offset, sizeof(response) - offset,
                             "|%d:%s:%s:%s:%s:%s:%d:%s:%s:%d",
                             q_id, q_text ? q_text : "",
                             opt_a ? opt_a : "", opt_b ? opt_b : "",
                             opt_c ? opt_c : "", opt_d ? opt_d : "",
                             correct, difficulty ? difficulty : "", category ? category : "",
                             is_selected);
            question_count++;
        }
        sqlite3_finalize(stmt);
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
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
              printf("[DEBUG] update_exam_question: qid=%d, room=%d, user=%d\n",
           question_id, room_id, user_id);
    } else {
        send(socket_fd, "UPDATE_QUESTION_FAIL|Database error\n", 37, 0);
        if (err_msg) {
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
    
          printf("[DEBUG] update_room_question: qid=%d, room=%d, user=%d\n",
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

// Set question selection status (admin toggle for manual mode)
// Format: SET_QUESTION_SELECTED|room_id|question_id|is_selected (0 or 1)
void set_question_selected(int socket_fd, int user_id, int room_id, int question_id, int is_selected) {
  pthread_mutex_lock(&server_data.lock);
  
  // Verify room ownership
  TestRoom *room = NULL;
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      room = &server_data.rooms[i];
      break;
    }
  }
  
  if (room == NULL) {
    server_send(socket_fd, "SET_QUESTION_SELECTED_FAIL|Room not found\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  if (room->creator_id != user_id) {
    server_send(socket_fd, "SET_QUESTION_SELECTED_FAIL|Permission denied\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Check room not started
  if (room->room_status == 1) {
    server_send(socket_fd, "SET_QUESTION_SELECTED_FAIL|Cannot modify while exam is running\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Update question selection status
  char query[256];
  snprintf(query, sizeof(query),
           "UPDATE exam_questions SET is_selected = %d WHERE id = %d AND room_id = %d",
           is_selected ? 1 : 0, question_id, room_id);
  
  char *err_msg = NULL;
  if (sqlite3_exec(db, query, NULL, NULL, &err_msg) == SQLITE_OK) {
    char response[128];
    snprintf(response, sizeof(response), "SET_QUESTION_SELECTED_OK|%d|%d|%d\n",
             room_id, question_id, is_selected);
    server_send(socket_fd, response);
          printf("[DEBUG] set_question_selected: qid=%d, room=%d, val=%d, user=%d\n",
           question_id, room_id, is_selected, user_id);
  } else {
    char response[256];
    snprintf(response, sizeof(response), "SET_QUESTION_SELECTED_FAIL|%s\n", err_msg ? err_msg : "Unknown error");
    server_send(socket_fd, response);
    if (err_msg) sqlite3_free(err_msg);
  }
  
  pthread_mutex_unlock(&server_data.lock);
}

// Set room selection mode (0=random, 1=manual)
// Format: SET_ROOM_SELECTION_MODE|room_id|selection_mode
void set_room_selection_mode(int socket_fd, int user_id, int room_id, int selection_mode) {
  pthread_mutex_lock(&server_data.lock);
  
  // Verify room ownership
  TestRoom *room = NULL;
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      room = &server_data.rooms[i];
      break;
    }
  }
  
  if (room == NULL) {
    server_send(socket_fd, "SET_SELECTION_MODE_FAIL|Room not found\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  if (room->creator_id != user_id) {
    server_send(socket_fd, "SET_SELECTION_MODE_FAIL|Permission denied\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Check room not started (Allow if Ended)
  if (room->room_status == 1) {
    time_t now = time(NULL);
    if (now - room->exam_start_time <= (long)room->time_limit * 60) {
      server_send(socket_fd, "SET_SELECTION_MODE_FAIL|Cannot modify while exam is in progress\n");
      pthread_mutex_unlock(&server_data.lock);
      return;
    }
  }
  
  // Update selection mode in database
  char query[256];
  snprintf(query, sizeof(query),
           "UPDATE rooms SET selection_mode = %d WHERE id = %d",
           selection_mode ? 1 : 0, room_id);
  
  char *err_msg = NULL;
  if (sqlite3_exec(db, query, NULL, NULL, &err_msg) == SQLITE_OK) {
    // If switching to manual mode, set all questions as selected by default
    if (selection_mode == 1) {
      char select_all_query[256];
      snprintf(select_all_query, sizeof(select_all_query),
               "UPDATE exam_questions SET is_selected = 1 WHERE room_id = %d", room_id);
      sqlite3_exec(db, select_all_query, NULL, NULL, NULL);
    }
    
    char response[128];
    snprintf(response, sizeof(response), "SET_SELECTION_MODE_OK|%d|%d\n",
             room_id, selection_mode);
    server_send(socket_fd, response);
          printf("[DEBUG] set_room_selection_mode: room=%d, mode=%d, user=%d\n",
           room_id, selection_mode, user_id);
  } else {
    char response[256];
    snprintf(response, sizeof(response), "SET_SELECTION_MODE_FAIL|%s\n", err_msg ? err_msg : "Unknown error");
    server_send(socket_fd, response);
    if (err_msg) sqlite3_free(err_msg);
  }
  
  pthread_mutex_unlock(&server_data.lock);
}

// Update room difficulty counts (for Random selection mode)
// Format: UPDATE_ROOM_DIFFICULTY|room_id|easy_count|medium_count|hard_count
void update_room_difficulty(int socket_fd, int user_id, int room_id, int easy_count, int medium_count, int hard_count) {
  pthread_mutex_lock(&server_data.lock);
  
  // Verify room ownership
  TestRoom *room = NULL;
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      room = &server_data.rooms[i];
      break;
    }
  }
  
  if (room == NULL) {
    server_send(socket_fd, "UPDATE_DIFFICULTY_FAIL|Room not found\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  if (room->creator_id != user_id) {
    server_send(socket_fd, "UPDATE_DIFFICULTY_FAIL|Permission denied\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Check room not started (Allow if Ended)
  if (room->room_status == 1) {
    time_t now = time(NULL);
    if (now - room->exam_start_time <= (long)room->time_limit * 60) {
      server_send(socket_fd, "UPDATE_DIFFICULTY_FAIL|Cannot modify while exam is in progress\n");
      pthread_mutex_unlock(&server_data.lock);
      return;
    }
  }
  
  // Validate counts
  if (easy_count < 0 || medium_count < 0 || hard_count < 0) {
    server_send(socket_fd, "UPDATE_DIFFICULTY_FAIL|Invalid counts\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  int total = easy_count + medium_count + hard_count;
  if (total <= 0) {
    server_send(socket_fd, "UPDATE_DIFFICULTY_FAIL|Must have at least 1 question\n");
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Update database
  char query[256];
  snprintf(query, sizeof(query),
           "UPDATE rooms SET easy_count = %d, medium_count = %d, hard_count = %d WHERE id = %d",
           easy_count, medium_count, hard_count, room_id);
  
  char *err_msg = NULL;
  if (sqlite3_exec(db, query, NULL, NULL, &err_msg) == SQLITE_OK) {
    char response[128];
    snprintf(response, sizeof(response), "UPDATE_DIFFICULTY_OK|%d|%d|%d|%d\n",
             room_id, easy_count, medium_count, hard_count);
    server_send(socket_fd, response);
          printf("[DEBUG] update_room_difficulty: room=%d, E:%d, M:%d, H:%d, user=%d\n",
           room_id, easy_count, medium_count, hard_count, user_id);
  } else {
    char response[256];
    snprintf(response, sizeof(response), "UPDATE_DIFFICULTY_FAIL|%s\n", err_msg ? err_msg : "Unknown error");
    server_send(socket_fd, response);
    if (err_msg) sqlite3_free(err_msg);
  }
  
  pthread_mutex_unlock(&server_data.lock);
}
