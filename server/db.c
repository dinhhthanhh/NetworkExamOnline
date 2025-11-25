#include "db.h"

extern sqlite3 *db;
extern ServerData server_data;

void init_database() {
  char *err_msg = 0;
  int rc = sqlite3_open("quiz_app.db", &db);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    return;
  }

  const char *sql_users = "CREATE TABLE IF NOT EXISTS users("
                    "id INTEGER PRIMARY KEY,"
                    "username TEXT UNIQUE,"
                    "password TEXT,"
                    "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

  const char *sql_results = "CREATE TABLE IF NOT EXISTS results("
                      "id INTEGER PRIMARY KEY,"
                      "user_id INTEGER,"
                      "room_id INTEGER,"
                      "score INTEGER,"
                      "total_questions INTEGER,"
                      "time_taken INTEGER,"
                      "completed_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                      "FOREIGN KEY(user_id) REFERENCES users(id));";

  const char *sql_questions = "CREATE TABLE IF NOT EXISTS questions("
                        "id INTEGER PRIMARY KEY,"
                        "question_text TEXT,"
                        "option_a TEXT,"
                        "option_b TEXT,"
                        "option_c TEXT,"
                        "option_d TEXT,"
                        "correct_answer INTEGER,"
                        "difficulty TEXT,"
                        "category TEXT);";

  const char *sql_activity_log = "CREATE TABLE IF NOT EXISTS activity_log("
                           "id INTEGER PRIMARY KEY,"
                           "user_id INTEGER,"
                           "action TEXT,"
                           "details TEXT,"
                           "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                           "FOREIGN KEY(user_id) REFERENCES users(id));";

  sqlite3_exec(db, sql_users, 0, 0, &err_msg);
  sqlite3_exec(db, sql_results, 0, 0, &err_msg);
  sqlite3_exec(db, sql_questions, 0, 0, &err_msg);
  sqlite3_exec(db, sql_activity_log, 0, 0, &err_msg);

  printf("Database initialized successfully\n");
}

void log_activity(int user_id, const char *action, const char *details) {
  char query[500];
  char *err_msg = 0;

  snprintf(query, sizeof(query),
           "INSERT INTO activity_log (user_id, action, details) VALUES (%d, '%s', '%s');",
           user_id, action, details);

  if (sqlite3_exec(db, query, 0, 0, &err_msg) != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }
}
