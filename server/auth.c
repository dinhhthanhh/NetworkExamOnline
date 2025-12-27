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
  
  if (!username || !password || strlen(username) == 0 || strlen(password) == 0)
  {
    snprintf(response, sizeof(response), "REGISTER_FAIL|Missing username or password\n");
    send(socket_fd, response, strlen(response), 0);
    return;
  }

  // SỬ DỤNG PREPARED STATEMENT thay vì string concatenation
  const char *query = "INSERT INTO users (username, password) VALUES (?, ?);";
  sqlite3_stmt *stmt;

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hashed_password, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_DONE)
    {
      snprintf(response, sizeof(response), "REGISTER_OK|Account created successfully\n");

      int idx = server_data.user_count;
      server_data.users[idx].user_id = (int)sqlite3_last_insert_rowid(db);
      strncpy(server_data.users[idx].username, username, 
              sizeof(server_data.users[idx].username) - 1);
      // LƯU HASHED PASSWORD, KHÔNG LƯU PLAINTEXT
      strncpy(server_data.users[idx].password, hashed_password, 
              sizeof(server_data.users[idx].password) - 1);
      server_data.users[idx].is_online = 0;
      server_data.users[idx].socket_fd = -1;
      server_data.user_count++;

      LOG_INFO("User registered: %s (ID: %d)", username, server_data.users[idx].user_id);
    }
    else
    {
      snprintf(response, sizeof(response), "REGISTER_FAIL|Username already exists\n");
      LOG_ERROR("SQL error: %s", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
  }
  else
  {
    snprintf(response, sizeof(response), "REGISTER_FAIL|Database error\n");
    LOG_ERROR("Prepare error: %s", sqlite3_errmsg(db));
  }

  pthread_mutex_unlock(&server_data.lock);
  send(socket_fd, response, strlen(response), 0);
}

void login_user(int socket_fd, char *username, char *password, int *user_id)
{
  char response[300];
  char hashed_password[65];
  char user_role[20] = "user";
  
  hash_password(password, hashed_password);

  // SỬ DỤNG PREPARED STATEMENT
  const char *query = "SELECT id, role FROM users WHERE username=? AND password=?;";
  sqlite3_stmt *stmt;

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hashed_password, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      *user_id = sqlite3_column_int(stmt, 0);
      
      const char *role = (const char *)sqlite3_column_text(stmt, 1);
      if (role) {
        strncpy(user_role, role, sizeof(user_role) - 1);
      }

      // Generate session token
      char token[64];
      generate_session_token(token, sizeof(token));

      // Update user data
      for (int i = 0; i < server_data.user_count; i++)
      {
        if (server_data.users[i].user_id == *user_id)
        {
          strncpy(server_data.users[i].session_token, token, 
                  sizeof(server_data.users[i].session_token) - 1);
          server_data.users[i].last_activity = time(NULL);
          server_data.users[i].is_online = 1;
          server_data.users[i].socket_fd = socket_fd;
          break;
        }
      }

      snprintf(response, sizeof(response), "LOGIN_OK|%d|%s|%s\n", 
               *user_id, token, user_role);
      log_activity(*user_id, "LOGIN", "User logged in");
    }
    else
    {
      snprintf(response, sizeof(response), "LOGIN_FAIL|Invalid credentials\n");
    }
    sqlite3_finalize(stmt);
  }
  else
  {
    snprintf(response, sizeof(response), "LOGIN_FAIL|Database error\n");
    fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(db));
  }

  pthread_mutex_unlock(&server_data.lock);
  send(socket_fd, response, strlen(response), 0);
}