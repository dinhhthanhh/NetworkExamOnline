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

    printf("Timer started for room %d: %d seconds\n", room_id, time_limit);
  }

  pthread_mutex_unlock(&server_data.lock);
}

void broadcast_time_update(int room_id, int time_remaining)
{
  pthread_mutex_lock(&server_data.lock);

  TestRoom *room = &server_data.rooms[room_id];

  // Send time update to all participants
  char update[100];
  snprintf(update, sizeof(update), "TIME_UPDATE|%d|%d\n", room_id, time_remaining);

  for (int i = 0; i < room->participant_count; i++)
  {
    // In a real app, we'd need to track client sockets for each participant
    // For now, just log the broadcast
    printf("Broadcast time update: Room %d has %d seconds remaining\n", room_id, time_remaining);
  }

  pthread_mutex_unlock(&server_data.lock);
}

void check_room_timeouts(void)
{
  pthread_mutex_lock(&server_data.lock);

  time_t now = time(NULL);

  for (int i = 0; i < server_data.room_count; i++)
  {
    TestRoom *room = &server_data.rooms[i];

    if (room->status == 1)
    { // Ongoing
      int elapsed = (int)(now - room->start_time);
      int remaining = room->time_limit - elapsed;

      if (remaining > 0)
      {
        broadcast_time_update(i, remaining);
      }
      else if (room->status == 1)
      {
        // Time's up - auto-submit all
        room->status = 2; // Finished
        printf("Room %d time's up - test finished\n", i);

        for (int j = 0; j < room->participant_count; j++)
        {
          int user_id = room->participants[j];

          // Calculate scores
          int score = 0;
          for (int q = 0; q < room->num_questions; q++)
          {
            if (room->answers[j][q].answer == server_data.questions[q].correct_answer)
            {
              score++;
            }
          }
          room->scores[j] = score;

          // Insert into results
          char query[300];
          snprintf(query, sizeof(query),
                   "INSERT INTO results (user_id, room_id, score, total_questions, time_taken) "
                   "VALUES (%d, %d, %d, %d, %d);",
                   user_id, i, score, room->num_questions, room->time_limit);

          char *err_msg = 0;
          sqlite3_exec(db, query, 0, 0, &err_msg);
        }
      }
    }
  }

  pthread_mutex_unlock(&server_data.lock);
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
        printf("Room %d expired and can be archived\n", i);
      }
    }
  }

  pthread_mutex_unlock(&server_data.lock);
}
