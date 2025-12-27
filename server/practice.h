#ifndef PRACTICE_H
#define PRACTICE_H

#include <sqlite3.h>

void handle_begin_practice(int socket_fd, int user_id, int room_id);
void save_practice_answer(int socket_fd, int user_id, int question_id, int answer);
void submit_practice(int socket_fd, int user_id);

#endif