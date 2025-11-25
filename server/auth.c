#include "auth.h"
#include "db.h"
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>

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

void register_user(int socket_fd, char *username, char *password)
{
  char response[200];

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
           username, password);

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

  snprintf(query, sizeof(query),
           "SELECT id FROM users WHERE username='%s' AND password='%s';", username, password);

  pthread_mutex_lock(&server_data.lock);

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      *user_id = sqlite3_column_int(stmt, 0);

      // Generate session token
      char token[64];
      generate_session_token(token, sizeof(token));

      // Update user data with token and last activity
      for (int i = 0; i < server_data.user_count; i++)
      {
        if (server_data.users[i].user_id == *user_id)
        {
          strncpy(server_data.users[i].session_token, token, sizeof(server_data.users[i].session_token) - 1);
          server_data.users[i].last_activity = time(NULL);
          server_data.users[i].is_online = 1;
          server_data.users[i].socket_fd = socket_fd;
          break;
        }
      }

      snprintf(response, sizeof(response), "LOGIN_OK|%d|%s\n", *user_id, token);
      log_activity(*user_id, "LOGIN", "User logged in");
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
