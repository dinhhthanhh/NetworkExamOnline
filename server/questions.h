#ifndef QUESTIONS_H
#define QUESTIONS_H

#include "common.h"

void handle_get_user_rooms(int client_socket, int user_id);
void handle_add_question(int client_socket, char *data);
int import_questions_from_csv(const char *filename, int room_id);
void handle_import_csv(int client_socket, char *data);

#endif
