#include "results.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

void submit_answer(int socket_fd, int user_id, int room_id, int question_num, int answer)
{
  pthread_mutex_lock(&server_data.lock);

  TestRoom *room = &server_data.rooms[room_id];

  int user_idx = -1;
  for (int i = 0; i < room->participant_count; i++)
  {
    if (room->participants[i] == user_id)
    {
      user_idx = i;
      break;
    }
  }

  if (user_idx != -1)
  {
    room->answers[user_idx][question_num].user_id = user_id;
    room->answers[user_idx][question_num].answer = answer;
    room->answers[user_idx][question_num].submit_time = time(NULL);

    char response[] = "SUBMIT_ANSWER_OK\n";
    send(socket_fd, response, strlen(response), 0);
  }

  pthread_mutex_unlock(&server_data.lock);
}

void submit_test(int socket_fd, int user_id, int room_id)
{
  pthread_mutex_lock(&server_data.lock);

  TestRoom *room = &server_data.rooms[room_id];

  int user_idx = -1;
  for (int i = 0; i < room->participant_count; i++)
  {
    if (room->participants[i] == user_id)
    {
      user_idx = i;
      break;
    }
  }

  if (user_idx == -1)
  {
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  int score = 0;
  for (int i = 0; i < room->num_questions; i++)
  {
    if (room->answers[user_idx][i].answer == server_data.questions[i].correct_answer)
    {
      score++;
    }
  }

  room->scores[user_idx] = score;
  time_t elapsed = time(NULL) - room->start_time;

  char query[300];
  snprintf(query, sizeof(query),
           "INSERT INTO results (user_id, room_id, score, total_questions, time_taken) VALUES (%d, %d, %d, %d, %ld);",
           user_id, room_id, score, room->num_questions, elapsed);

  char *err_msg = 0;
  sqlite3_exec(db, query, 0, 0, &err_msg);

  char response[200];
  snprintf(response, sizeof(response), "SUBMIT_TEST_OK|%d/%d\n", score, room->num_questions);
  send(socket_fd, response, strlen(response), 0);

  log_activity(user_id, "SUBMIT_TEST", "Test submitted");
  pthread_mutex_unlock(&server_data.lock);
}

void view_results(int socket_fd, int room_id)
{
  pthread_mutex_lock(&server_data.lock);

  TestRoom *room = &server_data.rooms[room_id];
  char response[2048];
  strcpy(response, "VIEW_RESULTS|");

  for (int i = 0; i < room->participant_count; i++)
  {
    char result_info[200];
    snprintf(result_info, sizeof(result_info), "User %d: %d/%d|",
             room->participants[i], room->scores[i], room->num_questions);
    strcat(response, result_info);
  }

  strcat(response, "\n");
  send(socket_fd, response, strlen(response), 0);
  pthread_mutex_unlock(&server_data.lock);
}
