#include "network.h"
#include "auth.h"
#include "rooms.h"
#include "questions.h"
#include "results.h"
#include "stats.h"
#include "admin.h"
#include "timer.h"
#include <sys/socket.h>
#include <unistd.h>

extern ServerData server_data;
extern sqlite3 *db;

void *handle_client(void *arg)
{
  int socket_fd = *(int *)arg;
  char buffer[BUFFER_SIZE];
  int user_id = -1;

  while (1)
  {
    memset(buffer, 0, BUFFER_SIZE);
    int n = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);

    if (n <= 0)
      break;

    buffer[n] = '\0';

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
    {
      buffer[len - 1] = '\0';
    }

    printf("Received: %s\n", buffer);

    char *cmd = strtok(buffer, "|");
    if (cmd == NULL)
      continue;

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
    else if (strcmp(cmd, "LIST_ROOMS") == 0)
    {
      list_test_rooms(socket_fd);
    }
    else if (strcmp(cmd, "CREATE_ROOM") == 0)
    {
      char *room_name = strtok(NULL, "|");
      int num_q = atoi(strtok(NULL, "|"));
      int time_limit = atoi(strtok(NULL, "|"));
      create_test_room(socket_fd, user_id, room_name, num_q, time_limit);
    }
    else if (strcmp(cmd, "JOIN_ROOM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      join_test_room(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "LIST_MY_ROOMS") == 0)
    {
      list_my_rooms(socket_fd, user_id);
    }
    else if (strcmp(cmd, "START_TEST") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      start_test(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "CLOSE_ROOM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      close_room(socket_fd, user_id, room_id);
    }
    // else if (strcmp(cmd, "GET_QUESTION") == 0)
    // {
    //   int room_id = atoi(strtok(NULL, "|"));
    //   int q_num = atoi(strtok(NULL, "|"));
    //   get_question(socket_fd, room_id, q_num);
    // }
    else if (strstr(buffer, "GET_USER_ROOMS")) 
    {
      char *user_id_str = buffer + 15; // skip "GET_USER_ROOMS|"
      int user_id = atoi(user_id_str);
      handle_get_user_rooms(socket_fd, user_id);
    }
    else if (strstr(buffer, "ADD_QUESTION")) {
    char *data = buffer + 13; // skip "ADD_QUESTION|"
    handle_add_question(socket_fd, data);
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
    }
    else if (strcmp(cmd, "VIEW_RESULTS") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      view_results(socket_fd, room_id);
    }
    // else if (strcmp(cmd, "PRACTICE_MODE") == 0)
    // {
    //   char *difficulty = strtok(NULL, "|");
    //   char *category = strtok(NULL, "|");
    //   int num_q = atoi(strtok(NULL, "|"));
    //   practice_mode(socket_fd, user_id, difficulty, category, num_q);
    // }
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
    else if (strcmp(cmd, "CATEGORY_STATS") == 0)
    {
      get_category_stats(socket_fd, user_id);
    }
    else if (strcmp(cmd, "DIFFICULTY_STATS") == 0)
    {
      get_difficulty_stats(socket_fd, user_id);
    }
    // else if (strcmp(cmd, "IMPORT_QUESTIONS") == 0)
    // {
    //   char *filename = strtok(NULL, "|");
    //   if (filename)
    //   {
    //     import_questions_from_csv(filename);
    //     char response[] = "IMPORT_OK|Questions imported\n";
    //     send(socket_fd, response, strlen(response), 0);
    //   }
    // }
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
