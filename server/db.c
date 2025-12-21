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
                    "role TEXT DEFAULT 'user',"  // 'user' or 'admin'
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

  const char *sql_activity_log = "CREATE TABLE IF NOT EXISTS activity_log("
                           "id INTEGER PRIMARY KEY,"
                           "user_id INTEGER,"
                           "action TEXT,"
                           "details TEXT,"
                           "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                           "FOREIGN KEY(user_id) REFERENCES users(id));";

  const char *sql_questions = 
                            "CREATE TABLE IF NOT EXISTS questions("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "room_id INTEGER NOT NULL,"
                            "question_text TEXT NOT NULL,"
                            "option_a TEXT NOT NULL,"
                            "option_b TEXT NOT NULL,"
                            "option_c TEXT NOT NULL,"
                            "option_d TEXT NOT NULL,"
                            "correct_answer INTEGER NOT NULL,"  // 0=A, 1=B, 2=C, 3=D
                            "difficulty TEXT DEFAULT 'Easy',"
                            "category TEXT DEFAULT 'General',"
                            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                            "FOREIGN KEY(room_id) REFERENCES rooms(id) ON DELETE CASCADE"
                            ");";
  const char *sql_rooms = 
                            "CREATE TABLE IF NOT EXISTS rooms("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "name TEXT NOT NULL,"
                            "host_id INTEGER NOT NULL,"
                            "duration INTEGER DEFAULT 30,"  // Thời gian thi (phút)
                            "is_active INTEGER DEFAULT 1,"  // 1: đang mở, 0: đã đóng
                            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                            "FOREIGN KEY(host_id) REFERENCES users(id) ON DELETE CASCADE"
                            ");";
  
  const char *sql_participants = 
                            "CREATE TABLE IF NOT EXISTS participants("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "room_id INTEGER NOT NULL,"
                            "user_id INTEGER NOT NULL,"
                            "start_time INTEGER DEFAULT 0,"  // Unix timestamp khi bắt đầu thi
                            "joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                            "FOREIGN KEY(room_id) REFERENCES rooms(id) ON DELETE CASCADE,"
                            "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,"
                            "UNIQUE(room_id, user_id)"  // Một user chỉ join room một lần
                            ");";
  
  const char *sql_user_answers = 
                            "CREATE TABLE IF NOT EXISTS user_answers("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "user_id INTEGER NOT NULL,"
                            "room_id INTEGER NOT NULL,"
                            "question_id INTEGER NOT NULL,"
                            "selected_answer INTEGER NOT NULL,"  // 0=A, 1=B, 2=C, 3=D
                            "answered_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                            "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,"
                            "FOREIGN KEY(room_id) REFERENCES rooms(id) ON DELETE CASCADE,"
                            "FOREIGN KEY(question_id) REFERENCES questions(id) ON DELETE CASCADE,"
                            "UNIQUE(user_id, room_id, question_id)"  // Mỗi user chỉ trả lời mỗi câu 1 lần
                            ");";

  sqlite3_exec(db, sql_users, 0, 0, &err_msg);
  sqlite3_exec(db, sql_results, 0, 0, &err_msg);
  sqlite3_exec(db, sql_activity_log, 0, 0, &err_msg);
  sqlite3_exec(db, sql_questions, 0, 0, &err_msg);
  sqlite3_exec(db, sql_rooms, 0, 0, &err_msg);
  sqlite3_exec(db, sql_participants, 0, 0, &err_msg);
  sqlite3_exec(db, sql_user_answers, 0, 0, &err_msg);

  // Thêm cột max_attempts vào rooms nếu chưa có (0 = unlimited)
  const char *sql_alter_rooms = 
    "ALTER TABLE rooms ADD COLUMN max_attempts INTEGER DEFAULT 0;";
  
  // Bỏ qua lỗi nếu cột đã tồn tại
  sqlite3_exec(db, sql_alter_rooms, 0, 0, &err_msg);
  if (err_msg) {
    // Cột đã tồn tại hoặc lỗi khác - không quan trọng
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Thêm cột role vào users nếu chưa có (default 'user')
  const char *sql_alter_users = 
    "ALTER TABLE users ADD COLUMN role TEXT DEFAULT 'user';";
  
  sqlite3_exec(db, sql_alter_users, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Tạo admin mặc định nếu chưa có (password: admin123)
  // Hash của "admin123" = 240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9
  const char *sql_create_admin = 
    "INSERT OR IGNORE INTO users (id, username, password, role) "
    "VALUES (1, 'admin', '240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9', 'admin');";
  
  sqlite3_exec(db, sql_create_admin, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  printf("Database initialized successfully\n");
  printf("Default admin account: username='admin', password='admin123'\n");
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
