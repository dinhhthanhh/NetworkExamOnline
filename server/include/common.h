#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <pthread.h>

#define PORT 8888
#define MAX_CLIENTS 100
#define BUFFER_SIZE 4096
#define MAX_ROOMS 50
#define MAX_QUESTIONS 1000
#define MAX_ANSWERS 4

typedef struct
{
  int id;
  char text[500];
  char options[MAX_ANSWERS][200];
  int correct_answer;
  char difficulty[20];
  char category[50];
} Question;

typedef struct
{
  int user_id;
  int answer;
  time_t submit_time;
} UserAnswer;

typedef struct
{
  int room_id;
  int creator_id;
  char room_name[100];
  int num_questions;
  int time_limit;
  int room_status;  // 0=WAITING, 1=STARTED, 2=ENDED
  int current_question;
  time_t exam_start_time;  // Thời điểm host bắt đầu exam
  time_t end_time;
  int participants[MAX_CLIENTS];
  int participant_count;
  UserAnswer answers[MAX_CLIENTS][MAX_QUESTIONS];
  int scores[MAX_CLIENTS];
} TestRoom;

typedef struct
{
  int user_id;
  char username[50];
  char password[50];
  int is_online;
  int socket_fd;
  char difficulty_filter[20];
  char category_filter[50];
  char session_token[64];
  time_t last_activity;
  int total_tests_completed;
  int total_correct_answers;
} User;

typedef struct
{
  User users[MAX_CLIENTS];
  TestRoom rooms[MAX_ROOMS];
  Question questions[MAX_QUESTIONS];
  int user_count;
  int room_count;
  int question_count;
  sqlite3 *db;
  pthread_mutex_t lock;
} ServerData;

extern ServerData server_data;
extern sqlite3 *db;

#endif
