#include "rooms.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

void create_test_room(int socket_fd, int creator_id, char *room_name, int num_q, int time_limit) {
  pthread_mutex_lock(&server_data.lock);

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
  // Kiểm tra số câu hỏi hợp lệ
  if (num_q <= 0 || num_q > MAX_QUESTIONS) {
    char response[128];
    snprintf(response, sizeof(response), 
             "CREATE_ROOM_FAIL|Invalid number of questions (1-%d)\n", 
             MAX_QUESTIONS);
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
    "INSERT INTO rooms (name, host_id, duration, is_active) "
    "VALUES (?, ?, ?, 1);";
  
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

  // ===== GỬI RESPONSE =====
  
  char response[256];
  snprintf(response, sizeof(response), 
           "CREATE_ROOM_OK|%d|%s|%d|%d\n", 
           room_id, room_name, num_q, time_limit);
  send(socket_fd, response, strlen(response), 0);

  // ===== LOGGING =====
  
  char log_details[256];
  snprintf(log_details, sizeof(log_details), 
           "Room: %s (ID=%d, Questions=%d, Time=%dm)", 
           room_name, room_id, num_q, time_limit);
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
    int is_active = sqlite3_column_int(stmt, 3);
    const char *host = (const char*)sqlite3_column_text(stmt, 4);
    int question_count = sqlite3_column_int(stmt, 5);

    const char *status_str = (is_active == 1) ? "Open" : "Closed";

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

  if (room_id >= server_data.room_count) {
    char response[] = "JOIN_ROOM_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  TestRoom *room = &server_data.rooms[room_id];

  if (room->status != 0) {
    char response[] = "JOIN_ROOM_FAIL|Room already started\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  if (room->participant_count >= MAX_CLIENTS) {
    char response[] = "JOIN_ROOM_FAIL|Room is full\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  room->participants[room->participant_count] = user_id;
  room->participant_count++;

  char response[] = "JOIN_ROOM_OK\n";
  send(socket_fd, response, strlen(response), 0);

  log_activity(user_id, "JOIN_ROOM", room->room_name);
  pthread_mutex_unlock(&server_data.lock);
}

void start_test(int socket_fd, int user_id, int room_id) {
  pthread_mutex_lock(&server_data.lock);

  if (room_id >= server_data.room_count) {
    char response[] = "START_TEST_FAIL|Room not found\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  TestRoom *room = &server_data.rooms[room_id];

  if (room->creator_id != user_id) {
    char response[] = "START_TEST_FAIL|Only creator can start\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  room->status = 1;
  room->start_time = time(NULL);
  room->end_time = room->start_time + (room->time_limit * 60);

  char response[] = "START_TEST_OK\n";
  send(socket_fd, response, strlen(response), 0);

  log_activity(user_id, "START_TEST", room->room_name);
  pthread_mutex_unlock(&server_data.lock);
}
