#ifndef TIMER_H
#define TIMER_H

#include "include/common.h"
#include <sys/socket.h>

void broadcast_time_update(int room_id, int time_remaining);
void check_room_timeouts(void);

#endif
