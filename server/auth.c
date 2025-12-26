#include "auth.h"
#include "db.h"
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <openssl/sha.h>

extern ServerData server_data;
extern sqlite3 *db;

// Generate random session token
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

//hash password
void hash_password(const char *password, char *hashed_output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)password, strlen(password), hash);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hashed_output + (i * 2), "%02x", hash[i]);
    }
    hashed_output[64] = '\0';
}

void register_user(int socket_fd, char *username, char *password)
{
  char response[200];
  char hashed_password[65];

  hash_password(password, hashed_password);
  if (!username || !password)
  {
    snprintf(response, sizeof(response), "REGISTER_FAIL|Missing username or password\n");
    send(socket_fd, response, strlen(response), 0);
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

    printf("User registered: %s (ID: %d)\n", username, server_data.users[idx].user_id);
  }

  pthread_mutex_unlock(&server_data.lock);
  send(socket_fd, response, strlen(response), 0);
}

void login_user(int socket_fd, char *username, char *password, int *user_id)
{
  char query[300];
  sqlite3_stmt *stmt;
  char response[300];
  char hashed_password[65];
  char user_role[20] = "user";
  hash_password(password, hashed_password);

  snprintf(query, sizeof(query),
           "SELECT id, role FROM users WHERE username='%s' AND password='%s';", username, hashed_password);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      *user_id = sqlite3_column_int(stmt, 0);
      
      // Get role (column 1)
      const char *role = (const char *)sqlite3_column_text(stmt, 1);
      if (role) {
        strncpy(user_role, role, sizeof(user_role) - 1);
      }

      // Kiểm tra user đã online chưa (chống đăng nhập đồng thời)
      int already_online = 0;
      for (int i = 0; i < server_data.user_count; i++)
      {
        if (server_data.users[i].user_id == *user_id && server_data.users[i].is_online == 1)
        {
          already_online = 1;
          printf("[LOGIN_BLOCKED] User %s (ID: %d) is already online from socket %d\n", 
                 username, *user_id, server_data.users[i].socket_fd);
          break;
        }
      }

      if (already_online)
      {
        snprintf(response, sizeof(response), "LOGIN_FAIL|User is already logged in from another device\n");
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&server_data.lock);
        send(socket_fd, response, strlen(response), 0);
        return;
      }

      // Generate session token
      char token[64];
      generate_session_token(token, sizeof(token));

      // Update user data with token and last activity
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
          break;
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
        fprintf(stderr, "[LOGIN] Failed to update is_online in DB: %s\n", err_msg);
        sqlite3_free(err_msg);
      }

      snprintf(response, sizeof(response), "LOGIN_OK|%d|%s|%s\n", *user_id, token, user_role);
      log_activity(*user_id, "LOGIN", "User logged in");
      printf("[LOGIN_SUCCESS] User %s (ID: %d) logged in from socket %d\n", username, *user_id, socket_fd);
    }
    else
    {
      snprintf(response, sizeof(response), "LOGIN_FAIL|Invalid credentials\n");
    }
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);
  send(socket_fd, response, strlen(response), 0);
}

void logout_user(int user_id, int socket_fd)
{
  pthread_mutex_lock(&server_data.lock);

  int logged_out_user_id = user_id;

  // Cập nhật trạng thái user trong in-memory structure
  for (int i = 0; i < server_data.user_count; i++)
  {
    if (server_data.users[i].user_id == user_id || 
        (user_id == -1 && server_data.users[i].socket_fd == socket_fd))
    {
      server_data.users[i].is_online = 0;
      server_data.users[i].socket_fd = -1;
      memset(server_data.users[i].session_token, 0, sizeof(server_data.users[i].session_token));
      
      logged_out_user_id = server_data.users[i].user_id;
      
      printf("[LOGOUT] User ID: %d (socket %d) logged out\n", 
             server_data.users[i].user_id, socket_fd);
      
      if (user_id > 0) {
        log_activity(user_id, "LOGOUT", "User logged out");
      }
      
      // Đồng bộ trạng thái offline vào database
      if (logged_out_user_id > 0) {
        char update_query[200];
        char *err_msg = 0;
        snprintf(update_query, sizeof(update_query),
                 "UPDATE users SET is_online = 0 WHERE id = %d;", logged_out_user_id);
        sqlite3_exec(db, update_query, 0, 0, &err_msg);
        if (err_msg) {
          fprintf(stderr, "[LOGOUT] Failed to update is_online in DB: %s\n", err_msg);
          sqlite3_free(err_msg);
        }
      }
      
      break;
    }
  }

  pthread_mutex_unlock(&server_data.lock);
}
