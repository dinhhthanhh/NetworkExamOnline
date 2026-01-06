#ifndef ADMIN_H
#define ADMIN_H

#include "include/common.h"

void get_admin_dashboard(int socket_fd, int admin_id);   // Tổng quan dashboard admin
void manage_users(int socket_fd, int admin_id);          // Danh sách user và trạng thái online
void manage_questions(int socket_fd, int admin_id);      // Danh sách câu hỏi đang cache
void get_system_stats(int socket_fd, int admin_id);      // Thống kê hệ thống
void ban_user(int socket_fd, int admin_id, int target_user_id);     // Ban/xóa user
void delete_question(int socket_fd, int admin_id, int question_id); // Xóa câu hỏi exam

#endif
