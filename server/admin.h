#ifndef ADMIN_H
#define ADMIN_H

#include "include/common.h"

void get_admin_dashboard(int socket_fd, int admin_id);
void manage_users(int socket_fd, int admin_id);
void manage_questions(int socket_fd, int admin_id);
void get_system_stats(int socket_fd, int admin_id);
void ban_user(int socket_fd, int admin_id, int target_user_id);
void delete_question(int socket_fd, int admin_id, int question_id);

#endif
