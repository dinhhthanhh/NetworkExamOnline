#ifndef PRACTICE_H
#define PRACTICE_H

#include "common.h"

// Server functions
void create_practice_room(int socket_fd, int creator_id, char *room_name, int time_limit, int show_answers);
void list_practice_rooms(int socket_fd);
void get_user_practice_rooms(int socket_fd, int user_id);
void join_practice_room(int socket_fd, int user_id, int practice_id);
void close_practice_room(int socket_fd, int user_id, int practice_id);
void open_practice_room(int socket_fd, int user_id, int practice_id);
void delete_practice_room(int socket_fd, int user_id, int practice_id);
void add_question_to_practice(int socket_fd, int user_id, int practice_id, int question_id);
void get_practice_participants(int socket_fd, int user_id, int practice_id);
void get_practice_questions(int socket_fd, int user_id, int practice_id);
void update_practice_question(int socket_fd, int user_id, int practice_id, int question_id, char *new_data);
void create_practice_question(int socket_fd, int user_id, int practice_id, char *question_data);
void import_practice_csv(int socket_fd, int user_id, int practice_id, const char *filename);
void submit_practice_answer(int socket_fd, int user_id, int practice_id, int question_num, int answer);
void finish_practice_session(int socket_fd, int user_id, int practice_id);
void view_practice_results(int socket_fd, int user_id, int practice_id);
void restart_practice(int socket_fd, int user_id, int practice_id);
void load_practice_rooms_from_db(void);
void save_practice_log(int user_id, int practice_id, int question_id, int answer, int is_correct);
void init_practice_tables(void);

#endif
