#include "network.h"
#include "auth.h"
#include "rooms.h"
#include "questions.h"
#include "results.h"
#include "stats.h"
#include "admin.h"
#include "timer.h"
#include "practice.h"
#include <sys/socket.h>
#include <unistd.h>

extern ServerData server_data;
extern sqlite3 *db;

void *handle_client(void *arg)
{
  int socket_fd = *(int *)arg;
  char buffer[BUFFER_SIZE];
  int user_id = -1;
  int current_room_id = -1;

  while (1)
  {
    memset(buffer, 0, BUFFER_SIZE);
    int n = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);

    if (n <= 0)
    {
      // Client closed connection or error
      if (user_id > 0) {
        LOG_INFO("DISCONNECT: User %d disconnected", user_id);

        // If user was in a room, attempt auto-submit using existing logic
        if (current_room_id > 0) {
          LOG_INFO("User %d was in room %d — triggering auto-submit and notifying others", user_id, current_room_id);
          // Run auto-submit handler (will guard against double submits)
          auto_submit_on_disconnect(user_id, current_room_id);
        }

        // Mark user offline and clear their socket
        pthread_mutex_lock(&server_data.lock);
        for (int i = 0; i < server_data.user_count; i++) {
          if (server_data.users[i].user_id == user_id) {
            server_data.users[i].is_online = 0;
            server_data.users[i].socket_fd = -1;
            break;
          }
        }

        // Notify other connected clients that this user disconnected
        char notify[128];
        snprintf(notify, sizeof(notify), "USER_DISCONNECTED|%d\n", user_id);
        for (int i = 0; i < server_data.user_count; i++) {
          if (server_data.users[i].is_online && server_data.users[i].socket_fd > 0) {
            ssize_t s = send(server_data.users[i].socket_fd, notify, strlen(notify), 0);
            (void)s;
            LOG_INFO("Notified user %d about disconnect of user %d", server_data.users[i].user_id, user_id);
          }
        }
        pthread_mutex_unlock(&server_data.lock);
      } else {
        LOG_INFO("Connection closed (no authenticated user)");
      }

      break;
    }

    buffer[n] = '\0';

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
    {
      buffer[len - 1] = '\0';
    }

    LOG_DEBUG("Received: %s", buffer);

    char *cmd = strtok(buffer, "|");
    if (cmd == NULL)
      continue;

    // Authentication
    if (strcmp(cmd, "REGISTER") == 0)
    {
      char *username = strtok(NULL, "|");
      char *password = strtok(NULL, "|");
      register_user(socket_fd, username, password);
    }
    else if (strcmp(cmd, "LOGIN") == 0)
    {
      char *username = strtok(NULL, "|");
      char *password = strtok(NULL, "|");
      login_user(socket_fd, username, password, &user_id);
    }
    // Room Management
    else if (strcmp(cmd, "LIST_ROOMS") == 0)
    {
      list_test_rooms(socket_fd);
    }
    else if (strcmp(cmd, "CREATE_ROOM") == 0)
    {
      char *room_name = strtok(NULL, "|");
      char *time_str = strtok(NULL, "|");
      int time_limit = time_str ? atoi(time_str) : 30;
      create_test_room(socket_fd, user_id, room_name, 0, time_limit);
    }
    else if (strcmp(cmd, "JOIN_ROOM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      join_test_room(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "SET_MAX_ATTEMPTS") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      int max_attempts = atoi(strtok(NULL, "|"));
      set_room_max_attempts(socket_fd, user_id, room_id, max_attempts);
    }
    else if (strcmp(cmd, "LIST_MY_ROOMS") == 0)
    {
      list_my_rooms(socket_fd, user_id);
    }
    else if (strcmp(cmd, "START_ROOM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      start_test(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "CLOSE_ROOM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      close_room(socket_fd, user_id, room_id);
    }
    else if (strstr(buffer, "GET_USER_ROOMS"))
    {
      char *user_id_str = buffer + 15;
      int target_user_id = atoi(user_id_str);
      handle_get_user_rooms(socket_fd, target_user_id);
    }
    // Exam
    else if (strcmp(cmd, "BEGIN_EXAM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      current_room_id = room_id;
      handle_begin_exam(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "RESUME_EXAM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      current_room_id = room_id;
      handle_resume_exam(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "SAVE_ANSWER") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      int question_id = atoi(strtok(NULL, "|"));
      int answer = atoi(strtok(NULL, "|"));
      save_answer(socket_fd, user_id, room_id, question_id, answer);
    }
    else if (strcmp(cmd, "SUBMIT_ANSWER") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      int q_num = atoi(strtok(NULL, "|"));
      int answer = atoi(strtok(NULL, "|"));
      submit_answer(socket_fd, user_id, room_id, q_num, answer);
    }
    else if (strcmp(cmd, "SUBMIT_TEST") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      submit_test(socket_fd, user_id, room_id);
      current_room_id = -1;
    }
    else if (strcmp(cmd, "VIEW_RESULTS") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      view_results(socket_fd, room_id);
    }
    // Questions
    else if (strstr(buffer, "ADD_QUESTION"))
    {
      char *data = buffer + 13;
      handle_add_question(socket_fd, data);
    }
    // Practice Mode
    else if (strcmp(cmd, "BEGIN_PRACTICE") == 0)
    {
      char *room_str = strtok(NULL, "|");
      int room_id = room_str ? atoi(room_str) : -1;
      handle_begin_practice(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "SAVE_PRACTICE_ANSWER") == 0)
    {
      int question_id = atoi(strtok(NULL, "|"));
      int answer = atoi(strtok(NULL, "|"));
      save_practice_answer(socket_fd, user_id, question_id, answer);
    }
    else if (strcmp(cmd, "SUBMIT_PRACTICE") == 0)
    {
      submit_practice(socket_fd, user_id);
    }
    // Statistics
    else if (strcmp(cmd, "LEADERBOARD") == 0)
    {
      int limit = 10;
      char *limit_str = strtok(NULL, "|");
      if (limit_str)
        limit = atoi(limit_str);
      get_leaderboard(socket_fd, limit);
    }
    else if (strcmp(cmd, "USER_STATS") == 0)
    {
      get_user_statistics(socket_fd, user_id);
    }
    else if (strcmp(cmd, "TEST_HISTORY") == 0)
    {
      get_user_test_history(socket_fd, user_id);
    }
    else if (strcmp(cmd, "CATEGORY_STATS") == 0)
    {
      get_category_stats(socket_fd, user_id);
    }
    else if (strcmp(cmd, "DIFFICULTY_STATS") == 0)
    {
      get_difficulty_stats(socket_fd, user_id);
    }
    // Admin
    else if (strcmp(cmd, "IMPORT_CSV") == 0)
    {
      char *data = buffer + 11;
      handle_import_csv(socket_fd, data);
    }
    else if (strcmp(cmd, "ADMIN_DASHBOARD") == 0)
    {
      get_admin_dashboard(socket_fd, user_id);
    }
    else if (strcmp(cmd, "ADMIN_USERS") == 0)
    {
      manage_users(socket_fd, user_id);
    }
    else if (strcmp(cmd, "ADMIN_QUESTIONS") == 0)
    {
      manage_questions(socket_fd, user_id);
    }
    else if (strcmp(cmd, "ADMIN_STATS") == 0)
    {
      get_system_stats(socket_fd, user_id);
    }
    else if (strcmp(cmd, "BAN_USER") == 0)
    {
      int target_id = atoi(strtok(NULL, "|"));
      ban_user(socket_fd, user_id, target_id);
    }
    else if (strcmp(cmd, "DELETE_QUESTION") == 0)
    {
      int q_id = atoi(strtok(NULL, "|"));
      delete_question(socket_fd, user_id, q_id);
    }
    // Timer
    else if (strcmp(cmd, "CHECK_TIMERS") == 0)
    {
      check_room_timeouts();
      char response[] = "TIMERS_CHECKED\n";
      send(socket_fd, response, strlen(response), 0);
    }
  }

  close(socket_fd);
  free(arg);
  pthread_exit(NULL);
}