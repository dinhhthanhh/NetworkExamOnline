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

// Practice room structure
typedef struct {
    int practice_id;
    int creator_id;
    char room_name[100];
    int time_limit;              // 0 means no time limit
    int show_answers;            // 1 = show correct/incorrect immediately, 0 = only mark as answered
    int is_open;                 // 1 = open, 0 = closed
    int num_questions;
    int question_ids[MAX_QUESTIONS];
    time_t created_time;
} PracticeRoom;

// Practice session for a user
typedef struct {
    int session_id;
    int practice_id;
    int user_id;
    int answers[MAX_QUESTIONS];  // User's answers (-1 = not answered)
    int is_correct[MAX_QUESTIONS]; // Only filled if show_answers=1
    time_t start_time;
    time_t end_time;             // 0 if not finished
    int score;
    int total_questions;
    int is_active;               // 1 = currently practicing, 0 = finished
} PracticeSession;

typedef struct
{
  User users[MAX_CLIENTS];
  TestRoom rooms[MAX_ROOMS];
  Question questions[MAX_QUESTIONS];
  PracticeRoom practice_rooms[MAX_ROOMS];
  PracticeSession practice_sessions[MAX_CLIENTS * MAX_ROOMS];
  int user_count;
  int room_count;
  int question_count;
  int practice_room_count;
  int practice_session_count;
  sqlite3 *db;
  pthread_mutex_t lock;
} ServerData;

extern ServerData server_data;
extern sqlite3 *db;

#endif
