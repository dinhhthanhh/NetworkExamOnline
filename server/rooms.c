#include "rooms.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

void create_test_room(int socket_fd, int creator_id, char *room_name, int num_q, int time_limit) {
  pthread_mutex_lock(&server_data.lock);

  if (server_data.room_count >= MAX_ROOMS) {
    char response[] = "CREATE_ROOM_FAIL|Max rooms reached\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  TestRoom *room = &server_data.rooms[server_data.room_count];
  room->room_id = server_data.room_count;
  room->creator_id = creator_id;
  strncpy(room->room_name, room_name, sizeof(room->room_name)-1);
  room->num_questions = num_q;
  room->time_limit = time_limit;
  room->status = 0;
  room->participant_count = 0;
  room->participants[0] = creator_id;
  room->participant_count = 1;

  server_data.room_count++;

  char response[200];
  snprintf(response, sizeof(response), "CREATE_ROOM_OK|%d\n", room->room_id);
  send(socket_fd, response, strlen(response), 0);

  log_activity(creator_id, "CREATE_ROOM", room_name);
  pthread_mutex_unlock(&server_data.lock);
}

void list_test_rooms(int socket_fd) {
  pthread_mutex_lock(&server_data.lock);

  char response[4096];
  strcpy(response, "LIST_ROOMS|");

  for (int i = 0; i < server_data.room_count; i++) {
    TestRoom *room = &server_data.rooms[i];
    char room_info[500];
    char status_str[20];

    if (room->status == 0)
      strcpy(status_str, "Not Started");
    else if (room->status == 1)
      strcpy(status_str, "Ongoing");
    else
      strcpy(status_str, "Finished");

    snprintf(room_info, sizeof(room_info),
             "%d|%s|%d/%d|%s|%d users|",
             room->room_id, room->room_name, room->time_limit, room->num_questions,
             status_str, room->participant_count);
    strcat(response, room_info);
  }

  strcat(response, "\n");
  send(socket_fd, response, strlen(response), 0);
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
