#ifndef TIMER_H
#define TIMER_H

#include "include/common.h"
#include <sys/socket.h>

typedef struct
{
  int room_id;
  int socket_fd;
  int user_id;
  int time_remaining;
  int start_time;
} RoomTimer;

void start_room_timer(int room_id, int time_limit);
void broadcast_time_update(int room_id, int time_remaining);
void check_room_timeouts(void);
void cleanup_expired_rooms(void);

#endif
