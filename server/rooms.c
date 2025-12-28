#include "rooms.h"
#include "db.h"
#include "results.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

void create_test_room(int socket_fd, int creator_id, char *room_name, int num_q, int time_limit)
{
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra role - chỉ admin mới được tạo room
  char role_query[256];
  snprintf(role_query, sizeof(role_query),
           "SELECT role FROM users WHERE id = ?");
  
  sqlite3_stmt *stmt_role;
  if (sqlite3_prepare_v2(db, role_query, -1, &stmt_role, NULL) == SQLITE_OK)
  {
    sqlite3_bind_int(stmt_role, 1, creator_id);
    
    if (sqlite3_step(stmt_role) == SQLITE_ROW)
    {
      const char *role = (const char *)sqlite3_column_text(stmt_role, 0);
      if (role && strcmp(role, "admin") != 0)
      {
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
  if (server_data.room_count >= MAX_ROOMS)
  {
    char response[] = "CREATE_ROOM_FAIL|Max rooms reached\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Validate room_name
  if (room_name == NULL || strlen(room_name) == 0)
  {
    char response[] = "CREATE_ROOM_FAIL|Room name cannot be empty\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Validate time_limit
  if (time_limit <= 0 || time_limit > 180)
  {
    char response[] = "CREATE_ROOM_FAIL|Invalid time limit (1-180 minutes)\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Kiểm tra room_name đã tồn tại chưa
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT id FROM rooms WHERE name = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "CREATE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_ROW)
  {
    char response[] = "CREATE_ROOM_FAIL|Room name already exists\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Insert vào database
  const char *sql_insert = 
    "INSERT INTO rooms (name, host_id, duration, is_active) "
    "VALUES (?, ?, ?, 1);";
  
  rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "CREATE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, creator_id);
  sqlite3_bind_int(stmt, 3, time_limit);

  rc = sqlite3_step(stmt);
  
  if (rc != SQLITE_DONE)
  {
    char response[] = "CREATE_ROOM_FAIL|Failed to create room\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int room_id = (int)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  // Gửi response
  char response[256];
  snprintf(response, sizeof(response), 
           "CREATE_ROOM_OK|%d|%s|%d\n", 
           room_id, room_name, time_limit);
  send(socket_fd, response, strlen(response), 0);

  // Logging
  char log_details[256];
  snprintf(log_details, sizeof(log_details), 
           "Room: %s (ID=%d, Time=%dm)", 
           room_name, room_id, time_limit);
  log_activity(creator_id, "CREATE_ROOM", log_details);

    LOG_INFO("User %d created room '%s' (ID=%d)", creator_id, room_name, room_id);
  
  server_data.room_count++;
  pthread_mutex_unlock(&server_data.lock);
}

void set_room_max_attempts(int socket_fd, int user_id, int room_id, int max_attempts)
{
  pthread_mutex_lock(&server_data.lock);
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id FROM rooms WHERE id = ?;";
  if (sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0) != SQLITE_OK) {
    send(socket_fd, "ERROR|Database error\n", 21, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  sqlite3_bind_int(stmt, 1, room_id);
  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    send(socket_fd, "ERROR|Room not found\n", 21, 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  int host_id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  if (host_id != user_id) {
    send(socket_fd, "ERROR|Only room host can set max attempts\n", 43, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  const char *update_query = "UPDATE rooms SET max_attempts = ? WHERE id = ?";
  if (sqlite3_prepare_v2(db, update_query, -1, &stmt, NULL) != SQLITE_OK) {
    send(socket_fd, "ERROR|Database error\n", 21, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  sqlite3_bind_int(stmt, 1, max_attempts);
  sqlite3_bind_int(stmt, 2, room_id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    send(socket_fd, "ERROR|Failed to update\n", 23, 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  sqlite3_finalize(stmt);

  char response[128];
  snprintf(response, sizeof(response), "SET_MAX_ATTEMPTS_OK|%d|%d\n", room_id, max_attempts);
  send(socket_fd, response, strlen(response), 0);
  char log_details[128];
  snprintf(log_details, sizeof(log_details), "Max attempts set to %d for room %d", max_attempts, room_id);
  log_activity(user_id, "SET_MAX_ATTEMPTS", log_details);
  LOG_INFO("Room %d max_attempts set to %d by user %d", room_id, max_attempts, user_id);
  pthread_mutex_unlock(&server_data.lock);
}

void list_test_rooms(int socket_fd)
{
  pthread_mutex_lock(&server_data.lock);

  sqlite3_stmt *stmt;
  
  const char *sql = 
    "SELECT "
    "  r.id, "
    "  r.name, "
    "  r.duration, "
    "  r.is_active, "
    "  u.username, "
    "  COUNT(q.id) as question_count, "
    "  r.max_attempts "
    "FROM rooms r "
    "JOIN users u ON r.host_id = u.id "
    "LEFT JOIN questions q ON r.id = q.room_id "
    "WHERE r.is_active = 1 "
    "GROUP BY r.id "
    "ORDER BY r.created_at DESC;";
  
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "LIST_ROOMS_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  char response[8192];
  int offset = 0;
  int room_count = 0;

  // Đếm số rooms
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    room_count++;
  }
  sqlite3_reset(stmt);

  // Header
  offset += snprintf(response + offset, sizeof(response) - offset, 
                     "LIST_ROOMS_OK|%d\n", room_count);

  // List từng room
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    if (offset >= sizeof(response) - 1000) break;

    int room_id = sqlite3_column_int(stmt, 0);
    const char *room_name = (const char*)sqlite3_column_text(stmt, 1);
    int duration = sqlite3_column_int(stmt, 2);
    const char *host = (const char*)sqlite3_column_text(stmt, 4);
    int question_count = sqlite3_column_int(stmt, 5);
    int max_attempts = sqlite3_column_int(stmt, 6);

    char question_info[50];
    if (question_count == 0)
    {
      strcpy(question_info, "No questions");
    }
    else
    {
      snprintf(question_info, sizeof(question_info), "%d questions", question_count);
    }
    
    char attempts_info[50];
    if (max_attempts > 0)
    {
      snprintf(attempts_info, sizeof(attempts_info), "%d attempts max", max_attempts);
    }
    else
    {
      strcpy(attempts_info, "Unlimited");
    }

    offset += snprintf(response + offset, sizeof(response) - offset,
                       "ROOM|%d|%s|%d|Open|%s|%s|%s\n",
                       room_id, room_name, duration, question_info, host, attempts_info);
  }

  sqlite3_finalize(stmt);
  send(socket_fd, response, offset, 0);
  pthread_mutex_unlock(&server_data.lock);
}

void join_test_room(int socket_fd, int user_id, int room_id)
{
  pthread_mutex_lock(&server_data.lock);

  // Lấy thông tin room
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT is_active, max_attempts FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "JOIN_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW)
  {
    char response[] = "JOIN_ROOM_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int is_active = sqlite3_column_int(stmt, 0);
  int max_attempts = sqlite3_column_int(stmt, 1);
  sqlite3_finalize(stmt);

  // Kiểm tra room có OPEN không
  if (is_active != 1)
  {
    char response[] = "JOIN_ROOM_FAIL|Room is closed\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Kiểm tra max_attempts
  if (max_attempts > 0)
  {
    char count_query[256];
    snprintf(count_query, sizeof(count_query),
             "SELECT COUNT(*) FROM results WHERE room_id = ? AND user_id = ?");
    
    if (sqlite3_prepare_v2(db, count_query, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int(stmt, 1, room_id);
      sqlite3_bind_int(stmt, 2, user_id);
      
      if (sqlite3_step(stmt) == SQLITE_ROW)
      {
        int attempt_count = sqlite3_column_int(stmt, 0);
        
        if (attempt_count >= max_attempts)
        {
          char response[128];
          snprintf(response, sizeof(response),
                   "JOIN_ROOM_FAIL|Maximum attempts reached (%d/%d)\n",
                   attempt_count, max_attempts);
          send(socket_fd, response, strlen(response), 0);
          sqlite3_finalize(stmt);
          pthread_mutex_unlock(&server_data.lock);
          return;
        }
      }
      sqlite3_finalize(stmt);
    }
  }

  // Thêm user vào participants
  const char *sql_insert = 
    "INSERT OR IGNORE INTO participants (room_id, user_id, start_time) "
    "VALUES (?, ?, 0);";
  
  rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
  if (rc != SQLITE_OK)
  {
    char response[] = "JOIN_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  sqlite3_bind_int(stmt, 2, user_id);
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE)
  {
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
  
  LOG_INFO("User %d joined room %d", user_id, room_id);
  // Persist start_time/end_time and notify participants when starting the exam
  {
    // Determine duration for this room
    int duration_minutes = 60;
    const char *q = "SELECT duration FROM rooms WHERE id = ?";
    if (sqlite3_prepare_v2(db, q, -1, &stmt, NULL) == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, room_id);
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        duration_minutes = sqlite3_column_int(stmt, 0);
      }
      sqlite3_finalize(stmt);
    }

    time_t now = time(NULL);
    time_t end_time = now + (duration_minutes * 60);

    // Update rooms table with start/end times
    const char *update_times_sql = "UPDATE rooms SET start_time = ?, end_time = ?, is_active = 1 WHERE id = ?";
    if (sqlite3_prepare_v2(db, update_times_sql, -1, &stmt, NULL) == SQLITE_OK) {
      sqlite3_bind_int64(stmt, 1, now);
      sqlite3_bind_int64(stmt, 2, end_time);
      sqlite3_bind_int(stmt, 3, room_id);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }

    // Update all participants start_time
    const char *update_part_start = "UPDATE participants SET start_time = ? WHERE room_id = ?";
    if (sqlite3_prepare_v2(db, update_part_start, -1, &stmt, NULL) == SQLITE_OK) {
      sqlite3_bind_int64(stmt, 1, now);
      sqlite3_bind_int(stmt, 2, room_id);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }

    // Create sessions row
    const char *ins_session = "INSERT INTO sessions (room_id, start_time, end_time, is_active) VALUES (?, ?, ?, 1)";
    if (sqlite3_prepare_v2(db, ins_session, -1, &stmt, NULL) == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, room_id);
      sqlite3_bind_int64(stmt, 2, now);
      sqlite3_bind_int64(stmt, 3, end_time);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      long session_id = sqlite3_last_insert_rowid(db);

      // Update participants.session_id for this room
      const char *upd_part_session = "UPDATE participants SET session_id = ? WHERE room_id = ?";
      sqlite3_stmt *ustmt;
      if (sqlite3_prepare_v2(db, upd_part_session, -1, &ustmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(ustmt, 1, session_id);
        sqlite3_bind_int(ustmt, 2, room_id);
        sqlite3_step(ustmt);
        sqlite3_finalize(ustmt);
      }
    }

    // Notify connected participants: send START_EXAM|<end_time>\n
    const char *select_participants = "SELECT user_id FROM participants WHERE room_id = ?";
    if (sqlite3_prepare_v2(db, select_participants, -1, &stmt, NULL) == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, room_id);
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        int p_uid = sqlite3_column_int(stmt, 0);
        // find socket for user
        for (int i = 0; i < server_data.user_count; i++) {
          if (server_data.users[i].user_id == p_uid && server_data.users[i].is_online && server_data.users[i].socket_fd > 0) {
            char notify[128];
            snprintf(notify, sizeof(notify), "START_EXAM|%lld\n", (long long)end_time);
            send(server_data.users[i].socket_fd, notify, strlen(notify), 0);
          }
        }
      }
      sqlite3_finalize(stmt);
    }
  }

  pthread_mutex_unlock(&server_data.lock);
  return;
}

void close_room(int socket_fd, int user_id, int room_id)
{
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra room và quyền sở hữu
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id, is_active FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "CLOSE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW)
  {
    char response[] = "CLOSE_ROOM_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int host_id = sqlite3_column_int(stmt, 0);
  int is_active = sqlite3_column_int(stmt, 1);
  sqlite3_finalize(stmt);

  if (host_id != user_id)
  {
    char response[] = "CLOSE_ROOM_FAIL|Only creator can close\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  if (is_active == 0)
  {
    char response[] = "CLOSE_ROOM_FAIL|Room already closed\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Close room
  const char *sql_update = "UPDATE rooms SET is_active = 0 WHERE id = ?;";
  rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "CLOSE_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE)
  {
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
  
  LOG_INFO("User %d closed room %d", user_id, room_id);
  pthread_mutex_unlock(&server_data.lock);
}

void list_my_rooms(int socket_fd, int user_id)
{
  pthread_mutex_lock(&server_data.lock);

  sqlite3_stmt *stmt;
  
  const char *sql = 
    "SELECT "
    "  r.id, "
    "  r.name, "
    "  r.duration, "
    "  r.is_active, "
    "  COUNT(q.id) as question_count "
    "FROM rooms r "
    "LEFT JOIN questions q ON r.id = q.room_id "
    "WHERE r.host_id = ? "
    "GROUP BY r.id "
    "ORDER BY r.created_at DESC;";
  
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "LIST_MY_ROOMS_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, user_id);

  char response[8192];
  int offset = 0;
  int room_count = 0;

  // Đếm số rooms
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    room_count++;
  }
  sqlite3_reset(stmt);

  // Header
  offset += snprintf(response + offset, sizeof(response) - offset, 
                     "LIST_MY_ROOMS_OK|%d\n", room_count);

  // List từng room
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    if (offset >= sizeof(response) - 1000) break;

    int room_id = sqlite3_column_int(stmt, 0);
    const char *room_name = (const char*)sqlite3_column_text(stmt, 1);
    int duration = sqlite3_column_int(stmt, 2);
    int is_active = sqlite3_column_int(stmt, 3);
    int question_count = sqlite3_column_int(stmt, 4);

    const char *status_str = (is_active == 1) ? "Open" : "Closed";

    char question_info[50];
    if (question_count == 0)
    {
      strcpy(question_info, "No questions");
    }
    else
    {
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

void start_test(int socket_fd, int user_id, int room_id)
{
  pthread_mutex_lock(&server_data.lock);

  // Kiểm tra room
  sqlite3_stmt *stmt;
  const char *sql_check = "SELECT host_id, is_active FROM rooms WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "START_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);

  if (rc != SQLITE_ROW)
  {
    char response[] = "START_ROOM_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int host_id = sqlite3_column_int(stmt, 0);
  int is_active = sqlite3_column_int(stmt, 1);
  sqlite3_finalize(stmt);

  if (host_id != user_id)
  {
    char response[] = "START_ROOM_FAIL|Only creator can start\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  if (is_active == 1)
  {
    char response[] = "START_ROOM_FAIL|Room already open\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Open room
  const char *sql_update = "UPDATE rooms SET is_active = 1 WHERE id = ?;";
  rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt, 0);
  
  if (rc != SQLITE_OK)
  {
    char response[] = "START_ROOM_FAIL|Database error\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  sqlite3_bind_int(stmt, 1, room_id);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE)
  {
    char response[] = "START_ROOM_FAIL|Failed to start room\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  char response[128];
  snprintf(response, sizeof(response), "START_ROOM_OK|Room %d opened\n", room_id);
  send(socket_fd, response, strlen(response), 0);

  char log_details[128];
  snprintf(log_details, sizeof(log_details), "Opened room ID=%d", room_id);
  log_activity(user_id, "START_ROOM", log_details);
  
  LOG_INFO("User %d opened room %d", user_id, room_id);
  pthread_mutex_unlock(&server_data.lock);
}

void handle_begin_exam(int socket_fd, int user_id, int room_id)
{
  pthread_mutex_lock(&server_data.lock);
  
  // Lấy thông tin room
  const char *room_query = "SELECT duration, host_id, is_active FROM rooms WHERE id = ?";
  
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, room_query, -1, &stmt, NULL) != SQLITE_OK)
  {
    send(socket_fd, "ERROR|Database error\n", 21, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  sqlite3_bind_int(stmt, 1, room_id);
  
  int duration_minutes = 60;
  int host_id = -1;
  int is_active = 0;
  
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    duration_minutes = sqlite3_column_int(stmt, 0);
    host_id = sqlite3_column_int(stmt, 1);
    is_active = sqlite3_column_int(stmt, 2);
  }
  else
  {
    send(socket_fd, "ERROR|Room not found\n", 21, 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  sqlite3_finalize(stmt);
  
  // Validate
  if (is_active != 1)
  {
    send(socket_fd, "ERROR|Room is closed\n", 22, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  if (user_id == host_id)
  {
    send(socket_fd, "ERROR|Host cannot take exam\n", 29, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Thêm user vào participants
  const char *insert_query = 
    "INSERT OR IGNORE INTO participants (room_id, user_id, start_time) VALUES (?, ?, 0)";
  
  if (sqlite3_prepare_v2(db, insert_query, -1, &stmt, NULL) == SQLITE_OK)
  {
    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  
  // Lấy danh sách câu hỏi
  const char *question_query = 
    "SELECT id, question_text, option_a, option_b, option_c, option_d, difficulty "
    "FROM questions WHERE room_id = ?";
  
  if (sqlite3_prepare_v2(db, question_query, -1, &stmt, NULL) != SQLITE_OK)
  {
    send(socket_fd, "ERROR|Cannot load questions\n", 28, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  sqlite3_bind_int(stmt, 1, room_id);
  
  // Build response
  char response[8192];
  snprintf(response, sizeof(response), "BEGIN_EXAM_OK|%d", duration_minutes);
  
  int question_count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    int q_id = sqlite3_column_int(stmt, 0);
    const char *q_text = (const char *)sqlite3_column_text(stmt, 1);
    const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
    const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
    const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
    const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);
    const char *difficulty = (const char *)sqlite3_column_text(stmt, 6);
    
    char q_entry[1024];
    snprintf(q_entry, sizeof(q_entry), "|%d:%s:%s:%s:%s:%s:%s",
         q_id, q_text, opt_a, opt_b, opt_c, opt_d,
         difficulty ? difficulty : "");
    strcat(response, q_entry);
    question_count++;
  }
  
  sqlite3_finalize(stmt);
  
  if (question_count == 0)
  {
    send(socket_fd, "ERROR|No questions in room\n", 27, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Lưu start_time
  time_t start_time = time(NULL);
  const char *update_query = 
    "UPDATE participants SET start_time = ? WHERE room_id = ? AND user_id = ?";
  
  if (sqlite3_prepare_v2(db, update_query, -1, &stmt, NULL) == SQLITE_OK)
  {
    sqlite3_bind_int64(stmt, 1, start_time);
    sqlite3_bind_int(stmt, 2, room_id);
    sqlite3_bind_int(stmt, 3, user_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  
  strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
        LOG_INFO("User %d started exam in room %d (%d questions, %d min)", user_id, room_id, question_count, duration_minutes);
    
    pthread_mutex_unlock(&server_data.lock);
}

void handle_resume_exam(int socket_fd, int user_id, int room_id)
{
    pthread_mutex_lock(&server_data.lock);
  // Check for active session for this room
  sqlite3_stmt *stmt;
  const char *sel_session = "SELECT id, start_time, end_time FROM sessions WHERE room_id = ? AND is_active = 1 ORDER BY id DESC LIMIT 1";
  int session_id = 0;
  long start_time = 0;
  long end_time = 0;
  if (sqlite3_prepare_v2(db, sel_session, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, room_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      session_id = sqlite3_column_int(stmt, 0);
      start_time = sqlite3_column_int64(stmt, 1);
      end_time = sqlite3_column_int64(stmt, 2);
    }
    sqlite3_finalize(stmt);
  }

  if (session_id == 0) {
    // No active session
    send(socket_fd, "RESUME_NOT_STARTED\n", 19, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Check already submitted
  const char *check_result = "SELECT id FROM results WHERE room_id = ? AND user_id = ?";
  if (sqlite3_prepare_v2(db, check_result, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, user_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      send(socket_fd, "RESUME_ALREADY_SUBMITTED\n", 25, 0);
      sqlite3_finalize(stmt);
      pthread_mutex_unlock(&server_data.lock);
      return;
    }
    sqlite3_finalize(stmt);
  }

  // Calculate remaining based on session end_time
  time_t now = time(NULL);
  long remaining = end_time - now;
  if (remaining <= 0) {
    // auto-submit all for this user and inform
    auto_submit_on_timeout(user_id, room_id);
    send(socket_fd, "RESUME_TIME_EXPIRED\n", 20, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  // Fetch questions and any saved answers for this session
  const char *question_query =
    "SELECT q.id, q.question_text, q.option_a, q.option_b, q.option_c, q.option_d, ua.selected_answer FROM questions q "
    "LEFT JOIN user_answers ua ON q.id = ua.question_id AND ua.user_id = ? AND ua.session_id = ? "
    "WHERE q.room_id = ?";
    
    if (sqlite3_prepare_v2(db, question_query, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "ERROR|Cannot load questions\n", 28, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, session_id);
    sqlite3_bind_int(stmt, 3, room_id);
    
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
    
        LOG_INFO("User %d resumed exam in room %d (%ld seconds remaining)", user_id, room_id, remaining);
    
    pthread_mutex_unlock(&server_data.lock);
}
