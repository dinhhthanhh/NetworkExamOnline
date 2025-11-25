#include "questions.h"
#include "db.h"
#include <sys/socket.h>

extern ServerData server_data;
extern sqlite3 *db;

void get_question(int socket_fd, int room_id, int question_num)
{
  pthread_mutex_lock(&server_data.lock);

  if (question_num >= server_data.question_count)
  {
    char response[] = "GET_QUESTION_FAIL|Question not found\n";
    send(socket_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
  }

  Question *q = &server_data.questions[question_num];
  char response[1024];

  snprintf(response, sizeof(response),
           "GET_QUESTION_OK|%d|%s|%s|%s|%s|%s\n",
           q->id, q->text, q->options[0], q->options[1], q->options[2], q->options[3]);

  send(socket_fd, response, strlen(response), 0);
  pthread_mutex_unlock(&server_data.lock);
}

void practice_mode(int socket_fd, int user_id, char *difficulty, char *category, int num_q)
{
  pthread_mutex_lock(&server_data.lock);

  int count = 0;
  char response[4096];
  strcpy(response, "PRACTICE_MODE|");

  for (int i = 0; i < server_data.question_count && count < num_q; i++)
  {
    Question *q = &server_data.questions[i];

    if ((difficulty[0] == '*' || strcmp(q->difficulty, difficulty) == 0) &&
        (category[0] == '*' || strcmp(q->category, category) == 0))
    {
      char q_info[500];
      snprintf(q_info, sizeof(q_info),
               "%d|%s|%s|%s|%s|%s|",
               q->id, q->text, q->options[0], q->options[1],
               q->options[2], q->options[3]);
      strcat(response, q_info);
      count++;
    }
  }

  strcat(response, "\n");
  send(socket_fd, response, strlen(response), 0);
  log_activity(user_id, "PRACTICE_MODE", "Entered practice mode");
  pthread_mutex_unlock(&server_data.lock);
}

void load_sample_questions()
{
  Question q1 = {1, "2 + 2 = ?", {"3", "4", "5", "6"}, 1, "easy", "math"};
  Question q2 = {2, "What is capital of France?", {"London", "Paris", "Berlin", "Madrid"}, 1, "easy", "geography"};
  Question q3 = {3, "What is 15 * 8?", {"100", "110", "120", "130"}, 2, "medium", "math"};
  Question q4 = {4, "Which planet is closest to sun?", {"Venus", "Mercury", "Earth", "Mars"}, 1, "medium", "science"};
  Question q5 = {5, "What is the integral of sin(x)?", {"-cos(x)", "cos(x)", "sin(x)", "-sin(x)"}, 0, "hard", "math"};

  server_data.questions[0] = q1;
  server_data.questions[1] = q2;
  server_data.questions[2] = q3;
  server_data.questions[3] = q4;
  server_data.questions[4] = q5;

  server_data.question_count = 5;
  printf("Sample questions loaded\n");
}

void add_question_to_bank(int creator_id, const char *question_text,
                          const char *opt_a, const char *opt_b,
                          const char *opt_c, const char *opt_d,
                          int correct, const char *difficulty,
                          const char *category)
{
  char query[800];
  snprintf(query, sizeof(query),
           "INSERT INTO questions (question_text, option_a, option_b, option_c, option_d, correct_answer, difficulty, category) VALUES ('%s', '%s', '%s', '%s', '%s', %d, '%s', '%s');",
           question_text, opt_a, opt_b, opt_c, opt_d, correct, difficulty, category);

  char *err_msg = 0;
  sqlite3_exec(db, query, 0, 0, &err_msg);
}

void get_question_bank(int socket_fd, const char *difficulty, const char *category)
{
  char query[400];
  if (strcmp(difficulty, "*") == 0 && strcmp(category, "*") == 0)
  {
    strcpy(query, "SELECT id, question_text, difficulty, category FROM questions;");
  }
  else if (strcmp(difficulty, "*") == 0)
  {
    snprintf(query, sizeof(query),
             "SELECT id, question_text, difficulty, category FROM questions WHERE category='%s';", category);
  }
  else if (strcmp(category, "*") == 0)
  {
    snprintf(query, sizeof(query),
             "SELECT id, question_text, difficulty, category FROM questions WHERE difficulty='%s';", difficulty);
  }
  else
  {
    snprintf(query, sizeof(query),
             "SELECT id, question_text, difficulty, category FROM questions WHERE difficulty='%s' AND category='%s';", difficulty, category);
  }

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
  {
    char response[4096];
    strcpy(response, "QUESTION_BANK|");

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      int id = sqlite3_column_int(stmt, 0);
      const char *text = (const char *)sqlite3_column_text(stmt, 1);
      const char *diff = (const char *)sqlite3_column_text(stmt, 2);
      const char *cat = (const char *)sqlite3_column_text(stmt, 3);

      char entry[500];
      snprintf(entry, sizeof(entry), "%d|%s|%s|%s|", id, text, diff, cat);
      strcat(response, entry);
    }

    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
  }

  sqlite3_finalize(stmt);
}

void import_questions_from_csv(const char *filename)
{
  FILE *fp = fopen(filename, "r");
  if (!fp)
  {
    fprintf(stderr, "Cannot open file: %s\n", filename);
    return;
  }

  char line[1000];
  char *err_msg = 0;
  int imported = 0;

  // Skip header line
  if (!fgets(line, sizeof(line), fp))
  {
    fclose(fp);
    return;
  }

  pthread_mutex_lock(&server_data.lock);

  while (fgets(line, sizeof(line), fp))
  {
    // Remove newline
    line[strcspn(line, "\n")] = 0;

    char q_text[300], opt_a[100], opt_b[100], opt_c[100], opt_d[100];
    char difficulty[20], category[50];
    int correct;

    // Parse CSV line carefully
    int parsed = sscanf(line, "%299[^,],%99[^,],%99[^,],%99[^,],%99[^,],%d,%19[^,],%49[^\n]",
                        q_text, opt_a, opt_b, opt_c, opt_d, &correct, difficulty, category);

    if (parsed != 8)
      continue; // Skip malformed lines

    // Insert into database
    char query[1000];
    snprintf(query, sizeof(query),
             "INSERT INTO questions (question_text, option_a, option_b, option_c, option_d, correct_answer, difficulty, category) "
             "VALUES ('%s', '%s', '%s', '%s', '%s', %d, '%s', '%s');",
             q_text, opt_a, opt_b, opt_c, opt_d, correct, difficulty, category);

    if (sqlite3_exec(db, query, 0, 0, &err_msg) == SQLITE_OK)
    {
      imported++;

      // Also add to in-memory cache if space available
      if (server_data.question_count < MAX_QUESTIONS)
      {
        Question *q = &server_data.questions[server_data.question_count];
        q->id = imported;
        strncpy(q->text, q_text, sizeof(q->text) - 1);
        strncpy(q->options[0], opt_a, sizeof(q->options[0]) - 1);
        strncpy(q->options[1], opt_b, sizeof(q->options[1]) - 1);
        strncpy(q->options[2], opt_c, sizeof(q->options[2]) - 1);
        strncpy(q->options[3], opt_d, sizeof(q->options[3]) - 1);
        q->correct_answer = correct;
        strncpy(q->difficulty, difficulty, sizeof(q->difficulty) - 1);
        strncpy(q->category, category, sizeof(q->category) - 1);
        server_data.question_count++;
      }
    }
    else
    {
      fprintf(stderr, "SQL error importing: %s\n", err_msg);
      sqlite3_free(err_msg);
    }
  }

  pthread_mutex_unlock(&server_data.lock);
  fclose(fp);
  printf("Imported %d questions from %s\n", imported, filename);
}
