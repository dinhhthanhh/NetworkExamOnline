#include "network.h"
#include <string.h>
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

/*
 * Hàm gửi dữ liệu trung tâm cho server:
 *  - In log tất cả gói tin gửi ra kèm socket_fd
 *  - Gói lại hàm send() để dễ debug và thống kê.
 */
ssize_t server_send(int socket_fd, const char *msg) {
  if (!msg) return -1;
  printf("[SERVER SEND fd=%d] %s\n", socket_fd, msg);
  return send(socket_fd, msg, strlen(msg), 0);
}

/*
 * Luồng xử lý chính cho từng client TCP:
 *  - Nhận command dạng text ("CMD|arg1|arg2|...")
 *  - Parse và định tuyến sang các module: auth, rooms, results, practice, stats, admin
 *  - Quản lý trạng thái user_id và room hiện tại để hỗ trợ resume/bảo toàn đáp án.
 */
void *handle_client(void *arg)
{
  int socket_fd = *(int *)arg;
  char buffer[BUFFER_SIZE];
  int user_id = -1;
  int current_room_id = -1; // Track room user đang thi

  while (1)
  {
    memset(buffer, 0, BUFFER_SIZE);
    int n = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);

    if (n <= 0) {
      // Detect disconnect - GHI TẤT CẢ ANSWERS VÀO DB ĐỂ USER CÓ THỂ RESUME
      if (user_id > 0 && current_room_id > 0) {
        // **QUAN TRỌNG: Flush tất cả answers từ in-memory vào DB**
        flush_user_answers(user_id, current_room_id);
        
        // KHÔNG auto-submit để user có thể resume sau
        // auto_submit_on_disconnect(user_id, current_room_id); // DISABLED - allow resume
      }
      
      // Cập nhật trạng thái offline khi disconnect
      if (user_id > 0) {
        logout_user(user_id, socket_fd);
      } else {
        logout_user(-1, socket_fd);  // Logout bằng socket_fd nếu chưa có user_id
      }
      break;
    }

    buffer[n] = '\0';

    // Log raw command received from client
    printf("[SERVER RECV fd=%d] %s\n", socket_fd, buffer);

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
    {
      buffer[len - 1] = '\0';
    }

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
    else if (strcmp(cmd, "LOGOUT") == 0)
    {
      logout_user(user_id, socket_fd);
      user_id = -1;  // Reset user_id sau khi logout
      current_room_id = -1;
      char response[] = "LOGOUT_OK\n";
      send(socket_fd, response, strlen(response), 0);
    }
    else if (strcmp(cmd, "LIST_ROOMS") == 0)
    {
      list_test_rooms(socket_fd);
    }
    else if (strcmp(cmd, "CREATE_ROOM") == 0)
    {
      char *room_name = strtok(NULL, "|");
      char *time_str = strtok(NULL, "|");
      char *easy_str = strtok(NULL, "|");
      char *medium_str = strtok(NULL, "|");
      char *hard_str = strtok(NULL, "|");
      
      int time_limit = time_str ? atoi(time_str) : 30;
      int easy_count = easy_str ? atoi(easy_str) : 0;
      int medium_count = medium_str ? atoi(medium_str) : 0;
      int hard_count = hard_str ? atoi(hard_str) : 0;
      
      create_test_room(socket_fd, user_id, room_name, 0, time_limit, easy_count, medium_count, hard_count);
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
    else if (strcmp(cmd, "START_ROOM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      start_test(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "BEGIN_EXAM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      current_room_id = room_id; // Track room
      handle_begin_exam(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "RESUME_EXAM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      current_room_id = room_id; // Track room
      handle_resume_exam(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "CLOSE_ROOM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      close_room(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "DELETE_ROOM") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      delete_room(socket_fd, user_id, room_id);
    }
    else if (strcmp(cmd, "GET_ROOM_MEMBERS") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      get_room_members(socket_fd, user_id, room_id);
    }
    else if (strstr(buffer, "GET_USER_ROOMS")) 
    {
      char *user_id_str = buffer + 15; // skip "GET_USER_ROOMS|"
      int user_id = atoi(user_id_str);
      handle_get_user_rooms(socket_fd, user_id);
    }
    else if (strstr(buffer, "GET_PRACTICE_ROOMS"))
    {
      char *user_id_str = buffer + 19; // skip "GET_PRACTICE_ROOMS|"
      int user_id = atoi(user_id_str);
      get_user_practice_rooms(socket_fd, user_id);
    }
    else if (strstr(buffer, "ADD_QUESTION")) {
    char *data = buffer + 13; // skip "ADD_QUESTION|"
    handle_add_question(socket_fd, data);
    }
    else if (strcmp(cmd, "SAVE_ANSWER") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      int question_id = atoi(strtok(NULL, "|"));
      int answer = atoi(strtok(NULL, "|"));
      save_answer(socket_fd, user_id, room_id, question_id, answer);
    }
    else if (strcmp(cmd, "SUBMIT_TEST") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      submit_test(socket_fd, user_id, room_id);
      current_room_id = -1; // Clear room sau khi submit
    }
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
    else if (strcmp(cmd, "IMPORT_CSV") == 0)
    {
      char *data = buffer + 11; // skip "IMPORT_CSV|"
      handle_import_csv(socket_fd, data);
    }
    // Practice commands
    else if (strcmp(cmd, "CREATE_PRACTICE") == 0)
    {
      char *room_name = strtok(NULL, "|");
      char *time_str = strtok(NULL, "|");
      char *show_str = strtok(NULL, "|");
      // time_limit here is used as cooldown (minutes) between full practice sessions
      int time_limit = time_str ? atoi(time_str) : 0;
      int show_answers = show_str ? atoi(show_str) : 0;
      create_practice_room(socket_fd, user_id, room_name, time_limit, show_answers);
    }
    else if (strcmp(cmd, "LIST_PRACTICE") == 0)
    {
      list_practice_rooms(socket_fd);
    }
    else if (strcmp(cmd, "JOIN_PRACTICE") == 0)
    {
      int practice_id = atoi(strtok(NULL, "|"));
      join_practice_room(socket_fd, user_id, practice_id);
    }
    else if (strcmp(cmd, "CREATE_PRACTICE_QUESTION") == 0)
    {
      int practice_id = atoi(strtok(NULL, "|"));
      char *question_data = strtok(NULL, "\n");
      create_practice_question(socket_fd, user_id, practice_id, question_data);
    }
    else if (strcmp(cmd, "IMPORT_PRACTICE_CSV") == 0)
    {
      int practice_id = atoi(strtok(NULL, "|"));
      char *filename = strtok(NULL, "\n");
      import_practice_csv(socket_fd, user_id, practice_id, filename);
    }    else if (strcmp(cmd, "SUBMIT_PRACTICE_ANSWER") == 0)
    {
      int practice_id = atoi(strtok(NULL, "|"));
      int question_num = atoi(strtok(NULL, "|"));
      int answer = atoi(strtok(NULL, "|"));
      submit_practice_answer(socket_fd, user_id, practice_id, question_num, answer);
    }
    else if (strcmp(cmd, "FINISH_PRACTICE") == 0)
    {
      int practice_id = atoi(strtok(NULL, "|"));
      finish_practice_session(socket_fd, user_id, practice_id);
    }
    else if (strcmp(cmd, "VIEW_PRACTICE_RESULTS") == 0)
    {
      int practice_id = atoi(strtok(NULL, "|"));
      view_practice_results(socket_fd, user_id, practice_id);
    }
    else if (strcmp(cmd, "CHANGE_PASSWORD") == 0)
    {
      char *old_pass = strtok(NULL, "|");
      char *new_pass = strtok(NULL, "|");
      change_password(socket_fd, user_id, old_pass, new_pass);
    }
    else if (strcmp(cmd, "DELETE_PRACTICE") == 0)
    {
      int practice_id = atoi(strtok(NULL, "|"));
      delete_practice_room(socket_fd, user_id, practice_id);
    }
    else if (strcmp(cmd, "GET_QUESTION_DETAIL") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      int question_id = atoi(strtok(NULL, "|"));
      get_question_detail(socket_fd, user_id, room_id, question_id);
    }
    else if (strcmp(cmd, "UPDATE_EXAM_QUESTION") == 0)
    {
      int room_id = atoi(strtok(NULL, "|"));
      int question_id = atoi(strtok(NULL, "|"));
      char *new_data = strtok(NULL, "\n"); // Rest of buffer
      update_exam_question(socket_fd, user_id, room_id, question_id, new_data);
    }
  }

  close(socket_fd);
  free(arg);
  pthread_exit(NULL);
}

/*
 * Gửi một thông báo tới TẤT CẢ thí sinh trong một phòng thi
 * dựa trên danh sách participants trong server_data.rooms.
 */
void broadcast_to_room_participants(int room_id, const char *message) {
  pthread_mutex_lock(&server_data.lock);
  
  // Tìm room trong in-memory
  int room_idx = -1;
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      room_idx = i;
      break;
    }
  }
  
  if (room_idx == -1) {
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Gửi message đến tất cả participants
  TestRoom *room = &server_data.rooms[room_idx];
  for (int i = 0; i < room->participant_count; i++) {
    int user_id = room->participants[i];
    
    // Tìm socket_fd của user
    for (int j = 0; j < server_data.user_count; j++) {
      if (server_data.users[j].user_id == user_id && 
          server_data.users[j].is_online == 1) {
        send(server_data.users[j].socket_fd, message, strlen(message), 0);
        break;
      }
    }
  }
  
  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Broadcast tới tất cả thí sinh trong phòng, trừ một user xác định
 * (thường dùng để không gửi lại thông điệp cho admin/host).
 */
void broadcast_to_room_participants_except(int room_id, const char *message, int exclude_user_id) {
  pthread_mutex_lock(&server_data.lock);
  
  // Tìm room trong in-memory
  int room_idx = -1;
  for (int i = 0; i < server_data.room_count; i++) {
    if (server_data.rooms[i].room_id == room_id) {
      room_idx = i;
      break;
    }
  }
  
  if (room_idx == -1) {
    pthread_mutex_unlock(&server_data.lock);
    return;
  }
  
  // Gửi message đến tất cả participants EXCEPT exclude_user_id
  TestRoom *room = &server_data.rooms[room_idx];
  int sent_count = 0;
  for (int i = 0; i < room->participant_count; i++) {
    int user_id = room->participants[i];
    
    // Skip excluded user (admin)
    if (user_id == exclude_user_id) {
      continue;
    }
    
    // Tìm socket_fd của user
    for (int j = 0; j < server_data.user_count; j++) {
      if (server_data.users[j].user_id == user_id && 
          server_data.users[j].is_online == 1) {
        send(server_data.users[j].socket_fd, message, strlen(message), 0);
        sent_count++;
        break;
      }
    }
  }
  pthread_mutex_unlock(&server_data.lock);
}

/*
 * Thông báo tới toàn bộ user đang online rằng có phòng thi mới được tạo.
 * Hiện tại gửi broadcast cho tất cả online users, tương lai có thể thu hẹp
 * chỉ những người đang mở màn hình danh sách phòng.
 */
void broadcast_room_created(int room_id, const char *room_name, int duration) {
  pthread_mutex_lock(&server_data.lock);
  
  char message[512];
  snprintf(message, sizeof(message), 
           "ROOM_CREATED|%d|%s|%d\n",
           room_id, room_name, duration);
  
  // Gửi đến tất cả users online (tạm thời - sau này có thể track users đang xem list)
  for (int i = 0; i < server_data.user_count; i++) {
    if (server_data.users[i].is_online == 1) {
      send(server_data.users[i].socket_fd, message, strlen(message), 0);
    }
  }
  
  pthread_mutex_unlock(&server_data.lock);
}

