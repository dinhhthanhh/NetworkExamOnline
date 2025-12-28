#include "timer.h"
#include "db.h"
#include <time.h>
#include <pthread.h>

extern ServerData server_data;
extern sqlite3 *db;

RoomTimer room_timers[MAX_ROOMS];

void start_room_timer(int room_id, int time_limit)
{
  pthread_mutex_lock(&server_data.lock);

  if (room_id < MAX_ROOMS)
  {
    room_timers[room_id].room_id = room_id;
    room_timers[room_id].time_remaining = time_limit;
    room_timers[room_id].start_time = time(NULL);

    LOG_INFO("Timer started for room %d: %d seconds", room_id, time_limit);
  }

  pthread_mutex_unlock(&server_data.lock);
}

void broadcast_time_update(int room_id, int time_remaining)
{
  pthread_mutex_lock(&server_data.lock);
  // Send time update to participants of this room (lookup from DB)
  char update[100];
  snprintf(update, sizeof(update), "TIME_UPDATE|%d|%d\n", room_id, time_remaining);

  sqlite3_stmt *stmt;
  const char *sel = "SELECT user_id FROM participants WHERE room_id = ?";
  if (sqlite3_prepare_v2(db, sel, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, room_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int uid = sqlite3_column_int(stmt, 0);
      for (int i = 0; i < server_data.user_count; i++) {
        if (server_data.users[i].user_id == uid && server_data.users[i].is_online && server_data.users[i].socket_fd > 0) {
          send(server_data.users[i].socket_fd, update, strlen(update), 0);
        }
      }
    }
    sqlite3_finalize(stmt);
  }
  pthread_mutex_unlock(&server_data.lock);
}

void check_room_timeouts(void)
{
  pthread_mutex_lock(&server_data.lock);

  time_t now = time(NULL);
  sqlite3_stmt *stmt;

  // Find active rooms with end_time set
  const char *sel_rooms = "SELECT id, end_time FROM rooms WHERE is_active = 1 AND end_time > 0";
  if (sqlite3_prepare_v2(db, sel_rooms, -1, &stmt, NULL) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int rid = sqlite3_column_int(stmt, 0);
      long end_time = sqlite3_column_int64(stmt, 1);
      long remaining = end_time - now;

      if (remaining > 0) {
        broadcast_time_update(rid, (int)remaining);
      } else {
        // Time's up for this room - auto-submit all participants
        LOG_INFO("Room %d time's up (server) - auto-submitting participants", rid);

        // Get participants
        sqlite3_stmt *ps;
        const char *sel_parts = "SELECT user_id FROM participants WHERE room_id = ?";
        if (sqlite3_prepare_v2(db, sel_parts, -1, &ps, NULL) == SQLITE_OK) {
          sqlite3_bind_int(ps, 1, rid);
          while (sqlite3_step(ps) == SQLITE_ROW) {
            int uid = sqlite3_column_int(ps, 0);
            // call existing auto-submit function
            auto_submit_on_timeout(uid, rid);

            // notify connected user
            for (int i = 0; i < server_data.user_count; i++) {
              if (server_data.users[i].user_id == uid && server_data.users[i].is_online && server_data.users[i].socket_fd > 0) {
                char notify[128];
                snprintf(notify, sizeof(notify), "EXAM_FINISHED|%d\n", rid);
                send(server_data.users[i].socket_fd, notify, strlen(notify), 0);
              }
            }
          }
          sqlite3_finalize(ps);
        }

        // Mark room as inactive/closed
        const char *upd = "UPDATE rooms SET is_active = 0 WHERE id = ?";
        sqlite3_stmt *us;
        if (sqlite3_prepare_v2(db, upd, -1, &us, NULL) == SQLITE_OK) {
          sqlite3_bind_int(us, 1, rid);
          sqlite3_step(us);
          sqlite3_finalize(us);
        }
      }
    }
    sqlite3_finalize(stmt);
  }

  pthread_mutex_unlock(&server_data.lock);
}

// Background monitor thread helper
void *timer_monitor_loop(void *arg)
{
  (void)arg;
  while (1) {
    check_room_timeouts();
    sleep(1);
  }
  return NULL;
}

void cleanup_expired_rooms(void)
{
  pthread_mutex_lock(&server_data.lock);

  time_t now = time(NULL);
  const int ROOM_EXPIRE_TIME = 3600; // 1 hour

  for (int i = 0; i < server_data.room_count; i++)
  {
    TestRoom *room = &server_data.rooms[i];

    if (room->status == 2)
    { // Finished
      int age = (int)(now - room->end_time);

        if (age > ROOM_EXPIRE_TIME)
      {
        // Mark as expired (can be archived)
        LOG_INFO("Room %d expired and can be archived", i);
      }
    }
  }

  pthread_mutex_unlock(&server_data.lock);
}
