#ifndef RESULTS_H
#define RESULTS_H

#include "common.h"

void submit_answer(int socket_fd, int user_id, int room_id, int question_num, int answer);
void submit_test(int socket_fd, int user_id, int room_id);
void view_results(int socket_fd, int room_id);

#endif
