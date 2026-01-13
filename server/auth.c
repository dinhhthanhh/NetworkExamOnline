#include "auth.h"
#include "db.h"
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <openssl/sha.h>

extern ServerData server_data;
extern sqlite3 *db;

/*
 * Sinh token phiên đăng nhập ngẫu nhiên cho user,
 * dùng để xác thực các request sau khi LOGIN thành công.
 */
void generate_session_token(char *token, size_t len)
{
  static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  srand(time(NULL) + rand());
  for (size_t i = 0; i < len - 1; i++)
  {
    token[i] = charset[rand() % (sizeof(charset) - 1)];
  }
  token[len - 1] = '\0';
}

/*
 * Băm mật khẩu bằng SHA-256 và lưu dưới dạng hex string 64 ký tự.
 * Mọi mật khẩu lưu trong DB đều phải đi qua hàm này.
 */
void hash_password(const char *password, char *hashed_output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)password, strlen(password), hash);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hashed_output + (i * 2), "%02x", hash[i]);
    }
    hashed_output[64] = '\0';
}

/*
 * Xử lý đăng ký tài khoản mới:
 *  - Băm mật khẩu
 *  - Lưu user vào bảng users
 *  - Cập nhật cấu trúc in-memory server_data.users
 * Trả về REGISTER_OK/REGISTER_FAIL cho client.
 */
void register_user(int socket_fd, char *username, char *password)
{
  char response[200];
  char hashed_password[65];

  hash_password(password, hashed_password);
  if (!username || !password)
  {
    snprintf(response, sizeof(response), "REGISTER_FAIL|Missing username or password\n");
    server_send(socket_fd, response);
    return;
  }

  char query[300];
  char *err_msg = 0;

  snprintf(query, sizeof(query),
           "INSERT INTO users (username, password) VALUES ('%s', '%s');",
           username, hashed_password);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_exec(db, query, 0, 0, &err_msg) != SQLITE_OK)
  {
    snprintf(response, sizeof(response), "REGISTER_FAIL|Username already exists\n");
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }
  else
  {
    snprintf(response, sizeof(response), "REGISTER_OK|Account created successfully\n");

    int idx = server_data.user_count;
    server_data.users[idx].user_id = (int)sqlite3_last_insert_rowid(db);
    strncpy(server_data.users[idx].username, username, sizeof(server_data.users[idx].username) - 1);
    strncpy(server_data.users[idx].password, password, sizeof(server_data.users[idx].password) - 1);
    server_data.users[idx].is_online = 0;
    server_data.users[idx].socket_fd = -1;
    server_data.user_count++;
  }

  pthread_mutex_unlock(&server_data.lock);
  server_send(socket_fd, response);
}

/*
 * Xử lý đăng nhập:
 *  - Kiểm tra username/password (đã băm) trong DB
 *  - Sinh session token, set trạng thái is_online, socket_fd
 *  - Lưu role của user (user/admin) để phía client phân quyền
 *  - Đồng bộ cờ is_online vào DB và log hoạt động.
 */
void login_user(int socket_fd, char *username, char *password, int *user_id)
{
  char query[300];
  sqlite3_stmt *stmt;
  char response[300];
  char hashed_password[65];
  char user_role[20] = "user";
  hash_password(password, hashed_password);

  snprintf(query, sizeof(query),
           "SELECT id, password, role FROM users WHERE username='%s';", username);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      const char *db_password = (const char *)sqlite3_column_text(stmt, 1);
      
      if (strcmp(db_password, hashed_password) != 0)
      {
        snprintf(response, sizeof(response), "LOGIN_FAIL|WRONG_PASSWORD\n");
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&server_data.lock);
        server_send(socket_fd, response);
        return;
      }

      *user_id = sqlite3_column_int(stmt, 0);
      
      // Get role (column 2)
      const char *role = (const char *)sqlite3_column_text(stmt, 2);
      if (role) {
        strncpy(user_role, role, sizeof(user_role) - 1);
      }

      // Kiểm tra user đã online chưa (chống đăng nhập đồng thời)
      int already_online = 0;
      int duplicate_count = 0;
      for (int i = 0; i < server_data.user_count; i++)
      {
        if (server_data.users[i].user_id == *user_id)
        {
          duplicate_count++;
          
          if (server_data.users[i].is_online == 1)
          {
            already_online = 1;
          }
        }
      }

      if (duplicate_count > 1) {
      }

      if (already_online)
      {
        snprintf(response, sizeof(response), "LOGIN_FAIL|User is already logged in from another device\n");
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&server_data.lock);
        server_send(socket_fd, response);
        return;
      }

      // Generate session token
      char token[64];
      generate_session_token(token, sizeof(token));

      // Update user data with token and last activity
      // QUAN TRỌNG: Update TẤT CẢ instances của user (nếu có duplicate)
      int user_found = 0;
      for (int i = 0; i < server_data.user_count; i++)
      {
        if (server_data.users[i].user_id == *user_id)
        {
          strncpy(server_data.users[i].session_token, token, sizeof(server_data.users[i].session_token) - 1);
          server_data.users[i].last_activity = time(NULL);
          server_data.users[i].is_online = 1;
          server_data.users[i].socket_fd = socket_fd;
          user_found = 1;
          // KHÔNG BREAK - tiếp tục update tất cả instances
        }
      }

      // Nếu user chưa có trong in-memory, thêm vào
      if (!user_found && server_data.user_count < MAX_CLIENTS)
      {
        int idx = server_data.user_count;
        server_data.users[idx].user_id = *user_id;
        strncpy(server_data.users[idx].username, username, sizeof(server_data.users[idx].username) - 1);
        strncpy(server_data.users[idx].session_token, token, sizeof(server_data.users[idx].session_token) - 1);
        server_data.users[idx].last_activity = time(NULL);
        server_data.users[idx].is_online = 1;
        server_data.users[idx].socket_fd = socket_fd;
        server_data.user_count++;
      }

      // Đồng bộ trạng thái online vào database
      char update_query[200];
      char *err_msg = 0;
      snprintf(update_query, sizeof(update_query),
               "UPDATE users SET is_online = 1 WHERE id = %d;", *user_id);
      sqlite3_exec(db, update_query, 0, 0, &err_msg);
      if (err_msg) {
        sqlite3_free(err_msg);
      }

      snprintf(response, sizeof(response), "LOGIN_OK|%d|%s|%s\n", *user_id, token, user_role);
      log_activity(*user_id, "LOGIN", "User logged in");
    }
    else
    {
      snprintf(response, sizeof(response), "LOGIN_FAIL|USER_NOT_FOUND\n");
    }
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);
  server_send(socket_fd, response);
}

/*
 * Xử lý đăng xuất:
 *  - Tắt cờ is_online và xóa session_token trong server_data
 *  - Đồng bộ trạng thái offline vào DB
 *  - Hỗ trợ cả trường hợp chỉ biết socket_fd (disconnect khi chưa lưu user_id).
 */
void logout_user(int user_id, int socket_fd)
{
  pthread_mutex_lock(&server_data.lock);

  int logged_out_user_id = user_id;
  int user_found = 0;

  // Cập nhật trạng thái user trong in-memory structure
  // QUAN TRỌNG: Không break để đảm bảo logout TẤT CẢ instances (tránh duplicate)
  for (int i = 0; i < server_data.user_count; i++)
  {
    if (server_data.users[i].user_id == user_id || 
        (user_id == -1 && server_data.users[i].socket_fd == socket_fd))
    {
      server_data.users[i].is_online = 0;
      server_data.users[i].socket_fd = -1;
      memset(server_data.users[i].session_token, 0, sizeof(server_data.users[i].session_token));
      
      logged_out_user_id = server_data.users[i].user_id;
      user_found = 1;
      
      // KHÔNG BREAK - tiếp tục logout tất cả instances
    }
  }

  // Log activity và đồng bộ DB (chỉ 1 lần sau khi logout tất cả instances)
  if (user_found && logged_out_user_id > 0) {
    log_activity(logged_out_user_id, "LOGOUT", "User logged out");
    
    // Đồng bộ trạng thái offline vào database
    char update_query[200];
    char *err_msg = 0;
    snprintf(update_query, sizeof(update_query),
             "UPDATE users SET is_online = 0 WHERE id = %d;", logged_out_user_id);
    sqlite3_exec(db, update_query, 0, 0, &err_msg);
    if (err_msg) {
      sqlite3_free(err_msg);
    }
  }

  if (!user_found) {
  }

  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Đổi mật khẩu:
 *  - Kiểm tra tham số, độ dài mật khẩu mới
 *  - Xác thực mật khẩu cũ trong DB
 *  - Cập nhật mật khẩu mới (đã băm) vào DB và log hoạt động.
 */
void change_password(int socket_fd, int user_id, char *old_password, char *new_password)
{
  char response[256];
  char old_hashed[65], new_hashed[65];
  
  if (!old_password || !new_password || user_id <= 0) {
    snprintf(response, sizeof(response), "CHANGE_PASSWORD_FAIL|Invalid parameters\n");
    server_send(socket_fd, response);
    return;
  }
  
  // Validate new password length
  if (strlen(new_password) < 3) {
    snprintf(response, sizeof(response), "CHANGE_PASSWORD_FAIL|New password too short (min 3 characters)\n");
    server_send(socket_fd, response);
    return;
  }
  
  hash_password(old_password, old_hashed);
  hash_password(new_password, new_hashed);
  
  pthread_mutex_lock(&server_data.lock);
  
  // Verify old password
  char query[512];
  sqlite3_stmt *stmt;
  snprintf(query, sizeof(query),
           "SELECT id FROM users WHERE id=%d AND password='%s';", user_id, old_hashed);
  
  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      // Old password correct, update to new password
      sqlite3_finalize(stmt);
      
      char update_query[512];
      char *err_msg = 0;
      snprintf(update_query, sizeof(update_query),
               "UPDATE users SET password='%s' WHERE id=%d;", new_hashed, user_id);
      
      if (sqlite3_exec(db, update_query, 0, 0, &err_msg) == SQLITE_OK) {
        snprintf(response, sizeof(response), "CHANGE_PASSWORD_OK|Password changed successfully\n");
        log_activity(user_id, "CHANGE_PASSWORD", "Password changed successfully");
      } else {
        snprintf(response, sizeof(response), "CHANGE_PASSWORD_FAIL|Database error\n");
        sqlite3_free(err_msg);
      }
    } else {
      // Old password incorrect
      sqlite3_finalize(stmt);
      snprintf(response, sizeof(response), "CHANGE_PASSWORD_FAIL|Old password incorrect\n");
    }
  } else {
    snprintf(response, sizeof(response), "CHANGE_PASSWORD_FAIL|Database query error\n");
  }
  
  pthread_mutex_unlock(&server_data.lock);
  server_send(socket_fd, response);
}
