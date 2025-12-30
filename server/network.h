#ifndef NETWORK_H
#define NETWORK_H

#include "common.h"

void *handle_client(void *arg);
void broadcast_to_room_participants(int room_id, const char *message);
void broadcast_room_created(int room_id, const char *room_name, int duration);

#endif
