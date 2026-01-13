#include "db.h"
#include "practice.h"

extern sqlite3 *db;
extern ServerData server_data;

/*
 * Khởi tạo kết nối SQLite và toàn bộ schema cần thiết:
 *  - Bảng users, results, activity_log, rooms, participants, exam_answers
 *  - Cấu trúc mới cho exam_questions, practice_questions và các bảng practice
 *  - Thêm các cột mới phục vụ role, is_online, room_status, exam_start_time
 *  - Tạo tài khoản admin mặc định nếu chưa tồn tại.
 */
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

  // Exam questions table - linked to rooms
  const char *sql_questions = 
                            "CREATE TABLE IF NOT EXISTS exam_questions("
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
                            "is_selected INTEGER DEFAULT 1,"  // 1 = dùng trong đề, 0 = không dùng
                            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                            "FOREIGN KEY(room_id) REFERENCES rooms(id) ON DELETE CASCADE"
                            ");";
  
  // Practice questions table - separate from exam questions
  const char *sql_practice_questions = 
                            "CREATE TABLE IF NOT EXISTS practice_questions("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "practice_id INTEGER NOT NULL,"
                            "question_text TEXT NOT NULL,"
                            "option_a TEXT NOT NULL,"
                            "option_b TEXT NOT NULL,"
                            "option_c TEXT NOT NULL,"
                            "option_d TEXT NOT NULL,"
                            "correct_answer INTEGER NOT NULL,"  // 0=A, 1=B, 2=C, 3=D
                            "difficulty TEXT DEFAULT 'Easy',"
                            "category TEXT DEFAULT 'General',"
                            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                            "FOREIGN KEY(practice_id) REFERENCES practice_rooms(id) ON DELETE CASCADE"
                            ");";
  
  // Backward compatibility: Keep old questions table but rename if exists
  // This is for migration purpose only
  const char *sql_migrate_questions = 
                            "CREATE TABLE IF NOT EXISTS questions_backup("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "room_id INTEGER,"
                            "question_text TEXT,"
                            "option_a TEXT,"
                            "option_b TEXT,"
                            "option_c TEXT,"
                            "option_d TEXT,"
                            "correct_answer INTEGER,"
                            "difficulty TEXT,"
                            "category TEXT,"
                            "created_at DATETIME"
                            ");";
  const char *sql_rooms = 
                            "CREATE TABLE IF NOT EXISTS rooms("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "name TEXT NOT NULL,"
                            "host_id INTEGER NOT NULL,"
                            "duration INTEGER DEFAULT 30,"  // Thời gian thi (phút)
                            "is_active INTEGER DEFAULT 1,"  // 1: đang mở, 0: đã đóng (deprecated)
                            "room_status INTEGER DEFAULT 0,"  // 0=WAITING, 1=STARTED, 2=ENDED
                            "exam_start_time INTEGER DEFAULT 0,"  // Unix timestamp khi host start
                            "easy_count INTEGER DEFAULT 0,"  // Số câu Easy được cấu hình
                            "medium_count INTEGER DEFAULT 0,"  // Số câu Medium được cấu hình
                            "hard_count INTEGER DEFAULT 0,"  // Số câu Hard được cấu hình
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
  
  const char *sql_exam_answers = 
                            "CREATE TABLE IF NOT EXISTS exam_answers("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "user_id INTEGER NOT NULL,"
                            "room_id INTEGER NOT NULL,"
                            "question_id INTEGER NOT NULL,"
                            "selected_answer INTEGER NOT NULL,"  // 0=A, 1=B, 2=C, 3=D
                            "answered_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                            "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,"
                            "FOREIGN KEY(room_id) REFERENCES rooms(id) ON DELETE CASCADE,"
                            "FOREIGN KEY(question_id) REFERENCES exam_questions(id) ON DELETE CASCADE,"
                            "UNIQUE(user_id, room_id, question_id)"  // Mỗi user chỉ trả lời mỗi câu 1 lần
                            ");";

  sqlite3_exec(db, sql_users, 0, 0, &err_msg);
  sqlite3_exec(db, sql_results, 0, 0, &err_msg);
  sqlite3_exec(db, sql_activity_log, 0, 0, &err_msg);
  sqlite3_exec(db, sql_questions, 0, 0, &err_msg);
  sqlite3_exec(db, sql_practice_questions, 0, 0, &err_msg);
  sqlite3_exec(db, sql_migrate_questions, 0, 0, &err_msg);
  sqlite3_exec(db, sql_rooms, 0, 0, &err_msg);
  sqlite3_exec(db, sql_participants, 0, 0, &err_msg);
  sqlite3_exec(db, sql_exam_answers, 0, 0, &err_msg);

  // Initialize practice tables
  init_practice_tables();
  
  // Migration note: Move data from old 'questions' table to new structure
  // Old questions table is now split into:
  // - exam_questions: for exam rooms (linked to rooms.id)
  // - practice_questions: for practice rooms (linked to practice_rooms.id)
  // Run migration script if needed to transfer existing data
  const char *sql_alter_room_status = 
    "ALTER TABLE rooms ADD COLUMN room_status INTEGER DEFAULT 0;";
  sqlite3_exec(db, sql_alter_room_status, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Thêm cột exam_start_time vào rooms nếu chưa có
  const char *sql_alter_exam_start = 
    "ALTER TABLE rooms ADD COLUMN exam_start_time INTEGER DEFAULT 0;";
  sqlite3_exec(db, sql_alter_exam_start, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Thêm các cột cấu hình số câu hỏi theo độ khó nếu chưa có
  const char *sql_alter_easy_count =
    "ALTER TABLE rooms ADD COLUMN easy_count INTEGER DEFAULT 0;";
  sqlite3_exec(db, sql_alter_easy_count, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  const char *sql_alter_medium_count =
    "ALTER TABLE rooms ADD COLUMN medium_count INTEGER DEFAULT 0;";
  sqlite3_exec(db, sql_alter_medium_count, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  const char *sql_alter_hard_count =
    "ALTER TABLE rooms ADD COLUMN hard_count INTEGER DEFAULT 0;";
  sqlite3_exec(db, sql_alter_hard_count, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Thêm cột selection_mode vào rooms (0=random, 1=manual)
  const char *sql_alter_selection_mode =
    "ALTER TABLE rooms ADD COLUMN selection_mode INTEGER DEFAULT 0;";
  sqlite3_exec(db, sql_alter_selection_mode, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Thêm cột is_selected vào exam_questions nếu chưa có
  const char *sql_alter_exam_is_selected =
    "ALTER TABLE exam_questions ADD COLUMN is_selected INTEGER DEFAULT 1;";
  sqlite3_exec(db, sql_alter_exam_is_selected, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Thêm cột has_taken_exam vào participants
  const char *sql_alter_participants = 
    "ALTER TABLE participants ADD COLUMN has_taken_exam INTEGER DEFAULT 0;";
  sqlite3_exec(db, sql_alter_participants, 0, 0, &err_msg);
  if (err_msg) {
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

  // Thêm cột is_online để theo dõi trạng thái đăng nhập
  const char *sql_alter_users_online = 
    "ALTER TABLE users ADD COLUMN is_online INTEGER DEFAULT 0;";
  
  sqlite3_exec(db, sql_alter_users_online, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Reset tất cả is_online về 0 khi khởi động server
  const char *sql_reset_online = 
    "UPDATE users SET is_online = 0;";
  
  sqlite3_exec(db, sql_reset_online, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Tạo admin mặc định nếu chưa có (password: admin123)
  // Hash của "admin123" = 240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9
  const char *sql_create_admin = 
    "INSERT OR IGNORE INTO users (id, username, password, role) "
    "VALUES (1, 'admin', '240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9', 'admin');"
    "INSERT OR IGNORE INTO users (id, username, password, role) "
    "VALUES (2, 'admin2', '240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9', 'admin');"
    ;
  
  sqlite3_exec(db, sql_create_admin, 0, 0, &err_msg);
  if (err_msg) {
    sqlite3_free(err_msg);
    err_msg = NULL;
  }

  // Chỉ giữ lại thông tin admin mặc định cho người quản trị
  printf("Default admin account: username='admin', password='admin123'\n");
}

/*
 * Ghi log hoạt động người dùng vào bảng activity_log
 * để phục vụ audit và thống kê sau này.
 */
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

/*
 * Nạp toàn bộ danh sách user từ DB vào mảng server_data.users
 * khi khởi động server, mặc định tất cả ở trạng thái offline.
 */
void load_users_from_db(void) {
  char query[200];
  sqlite3_stmt *stmt;

  snprintf(query, sizeof(query), "SELECT id, username FROM users;");

  pthread_mutex_lock(&server_data.lock);

  server_data.user_count = 0;

  if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW && server_data.user_count < MAX_CLIENTS) {
      int idx = server_data.user_count;
      
      server_data.users[idx].user_id = sqlite3_column_int(stmt, 0);
      const char *username = (const char *)sqlite3_column_text(stmt, 1);
      if (username) {
        strncpy(server_data.users[idx].username, username, sizeof(server_data.users[idx].username) - 1);
      }
      
      // Khởi tạo trạng thái offline
      server_data.users[idx].is_online = 0;
      server_data.users[idx].socket_fd = -1;
      memset(server_data.users[idx].session_token, 0, sizeof(server_data.users[idx].session_token));
      server_data.users[idx].last_activity = 0;
      
      server_data.user_count++;
    }
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&server_data.lock);
}
