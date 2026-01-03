#ifndef ROOMS_H
#define ROOMS_H

#include "common.h"

void create_test_room(int socket_fd, int creator_id, char *room_name, int num_q, int time_limit, int easy_count, int medium_count, int hard_count);
void create_practice_room(int socket_fd, int creator_id, char *room_name, int time_limit, int show_answer);
void set_room_difficulty_ratio(int socket_fd, int user_id, int room_id, int easy, int medium, int hard);
void update_room_settings(int socket_fd, int user_id, int room_id, int easy, int medium, int hard);
void get_room_settings(int socket_fd, int user_id, int room_id);
void list_test_rooms(int socket_fd);
void list_my_rooms(int socket_fd, int user_id);
void join_test_room(int socket_fd, int user_id, int room_id);
// void set_room_max_attempts(int socket_fd, int user_id, int room_id, int max_attempts); // REMOVED
void start_test(int socket_fd, int user_id, int room_id);
void handle_begin_exam(int socket_fd, int user_id, int room_id);
void handle_resume_exam(int socket_fd, int user_id, int room_id);
// void close_room(int socket_fd, int user_id, int room_id); // REMOVED - auto close
void close_room(int socket_fd, int user_id, int room_id);
void delete_room(int socket_fd, int user_id, int room_id);
void load_rooms_from_db(void);
void load_room_answers(int room_id, int user_id);

#endif


