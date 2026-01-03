#ifndef STATS_H
#define STATS_H

#include "include/common.h"

void get_leaderboard(int socket_fd, int limit);
void get_user_statistics(int socket_fd, int user_id);
void get_category_stats(int socket_fd, int user_id);
void get_difficulty_stats(int socket_fd, int user_id);
void get_user_test_history(int socket_fd, int user_id);
void get_room_statistics(int socket_fd, int room_id, int requesting_user_id);

#endif
