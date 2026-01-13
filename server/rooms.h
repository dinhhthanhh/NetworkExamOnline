#ifndef ROOMS_H
#define ROOMS_H

#include "common.h"

void create_test_room(int socket_fd, int creator_id, char *room_name, int num_q, int time_limit, int easy_count, int medium_count, int hard_count);
void list_test_rooms(int socket_fd);
void list_my_rooms(int socket_fd, int user_id);
void delete_room(int socket_fd, int user_id, int room_id);
void join_test_room(int socket_fd, int user_id, int room_id);
void start_test(int socket_fd, int user_id, int room_id);
void handle_begin_exam(int socket_fd, int user_id, int room_id);
void handle_resume_exam(int socket_fd, int user_id, int room_id);
<<<<<<< HEAD
=======
void handle_get_user_rooms(int socket_fd, int user_id);
void get_exam_students_status(int socket_fd, int user_id, int room_id);
void get_room_questions(int socket_fd, int user_id, int room_id);
void get_question_detail(int socket_fd, int user_id, int room_id, int question_id);
void update_exam_question(int socket_fd, int user_id, int room_id, int question_id, char *new_data);
void update_room_question(int socket_fd, int user_id, int room_id, int question_id, char *new_data);
void get_room_members(int socket_fd, int user_id, int room_id);
void close_room(int socket_fd, int user_id, int room_id);
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
void load_rooms_from_db(void);
void load_room_answers(int room_id, int user_id);
void set_question_selected(int socket_fd, int user_id, int room_id, int question_id, int is_selected);
void set_room_selection_mode(int socket_fd, int user_id, int room_id, int selection_mode);
void update_room_difficulty(int socket_fd, int user_id, int room_id, int easy_count, int medium_count, int hard_count);

#endif


