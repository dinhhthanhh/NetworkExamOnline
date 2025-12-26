#ifndef AUTH_H
#define AUTH_H

#include "common.h"

void register_user(int socket_fd, char *username, char *password);
void login_user(int socket_fd, char *username, char *password, int *user_id);
void logout_user(int user_id, int socket_fd);
void generate_session_token(char *token, size_t len);

#endif
