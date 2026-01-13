#include "practice.h"
#include "db.h"
#include "network.h"
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>

extern ServerData server_data;
extern sqlite3 *db;

/*
 * Khởi tạo toàn bộ bảng liên quan đến chế độ luyện tập (practice):
 *  - practice_rooms, practice_room_questions, practice_sessions,
 *    practice_answers, practice_logs.
 */
void init_practice_tables() {
    char *err_msg = 0;
    
    // Practice rooms table
    const char *sql_rooms = 
        "CREATE TABLE IF NOT EXISTS practice_rooms ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "creator_id INTEGER NOT NULL,"
        "time_limit INTEGER DEFAULT 0,"
        "show_answers INTEGER DEFAULT 0,"
        "is_open INTEGER DEFAULT 1,"
        "created_at INTEGER,"
        "FOREIGN KEY(creator_id) REFERENCES users(id)"
        ");";
    
    if (sqlite3_exec(db, sql_rooms, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Failed to create practice_rooms table: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    
    // Practice room questions table - links to practice_questions
    const char *sql_room_questions = 
        "CREATE TABLE IF NOT EXISTS practice_room_questions ("
        "practice_id INTEGER,"
        "question_id INTEGER,"
        "question_order INTEGER,"
        "PRIMARY KEY(practice_id, question_id),"
        "FOREIGN KEY(practice_id) REFERENCES practice_rooms(id),"
        "FOREIGN KEY(question_id) REFERENCES practice_questions(id)"
        ");";
    
    if (sqlite3_exec(db, sql_room_questions, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Failed to create practice_room_questions table: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    
    // Practice sessions table
    const char *sql_sessions = 
        "CREATE TABLE IF NOT EXISTS practice_sessions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "practice_id INTEGER NOT NULL,"
        "user_id INTEGER NOT NULL,"
        "start_time INTEGER,"
        "end_time INTEGER,"
        "score INTEGER DEFAULT 0,"
        "total_questions INTEGER,"
        "is_active INTEGER DEFAULT 1,"
        "FOREIGN KEY(practice_id) REFERENCES practice_rooms(id),"
        "FOREIGN KEY(user_id) REFERENCES users(id)"
        ");";
    
    if (sqlite3_exec(db, sql_sessions, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Failed to create practice_sessions table: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    
    // Practice answers table
    const char *sql_answers = 
        "CREATE TABLE IF NOT EXISTS practice_answers ("
        "session_id INTEGER,"
        "question_id INTEGER,"
        "answer INTEGER,"
        "is_correct INTEGER,"
        "answered_at INTEGER,"
        "PRIMARY KEY(session_id, question_id),"
        "FOREIGN KEY(session_id) REFERENCES practice_sessions(id)"
        ");";
    
    if (sqlite3_exec(db, sql_answers, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Failed to create practice_answers table: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    
    // Practice logs table for tracking all attempts - links to practice_questions
    const char *sql_logs = 
        "CREATE TABLE IF NOT EXISTS practice_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id INTEGER,"
        "practice_id INTEGER,"
        "question_id INTEGER,"
        "answer INTEGER,"
        "is_correct INTEGER,"
        "attempt_time INTEGER,"
        "FOREIGN KEY(user_id) REFERENCES users(id),"
        "FOREIGN KEY(practice_id) REFERENCES practice_rooms(id),"
        "FOREIGN KEY(question_id) REFERENCES practice_questions(id)"
        ");";
    
    if (sqlite3_exec(db, sql_logs, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Failed to create practice_logs table: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
}

/*
 * Nạp danh sách practice_rooms từ DB vào server_data.practice_rooms
 * và load luôn danh sách câu hỏi thuộc từng phòng luyện tập.
 */
void load_practice_rooms_from_db() {
    pthread_mutex_lock(&server_data.lock);
    
    const char *sql = "SELECT id, name, creator_id, time_limit, show_answers, is_open, created_at FROM practice_rooms;";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare practice rooms query: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    server_data.practice_room_count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && server_data.practice_room_count < MAX_ROOMS) {
        PracticeRoom *room = &server_data.practice_rooms[server_data.practice_room_count];
        
        room->practice_id = sqlite3_column_int(stmt, 0);
        strncpy(room->room_name, (const char *)sqlite3_column_text(stmt, 1), sizeof(room->room_name) - 1);
        room->creator_id = sqlite3_column_int(stmt, 2);
        room->time_limit = sqlite3_column_int(stmt, 3);
        room->show_answers = sqlite3_column_int(stmt, 4);
        room->is_open = sqlite3_column_int(stmt, 5);
        room->created_time = sqlite3_column_int(stmt, 6);
        
        // Load questions for this practice room
        char q_sql[512];
        snprintf(q_sql, sizeof(q_sql), 
                 "SELECT question_id FROM practice_room_questions WHERE practice_id = %d ORDER BY question_order;",
                 room->practice_id);
        
        sqlite3_stmt *q_stmt;
        room->num_questions = 0;
        
        if (sqlite3_prepare_v2(db, q_sql, -1, &q_stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(q_stmt) == SQLITE_ROW && room->num_questions < MAX_QUESTIONS) {
                room->question_ids[room->num_questions++] = sqlite3_column_int(q_stmt, 0);
            }
            sqlite3_finalize(q_stmt);
        }
        
        server_data.practice_room_count++;
    }
    
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Tạo phòng luyện tập mới (chỉ admin):
 *  - Cho phép cấu hình thời gian chờ giữa các lần luyện (cooldown, phút), chế độ hiển thị đáp án
 *  - Lưu vào DB và cập nhật vào server_data.practice_rooms.
 */
void create_practice_room(int socket_fd, int creator_id, char *room_name, int time_limit, int show_answers) {
    pthread_mutex_lock(&server_data.lock);
    
    // Check if user is admin
    int is_admin = 0;
    for (int i = 0; i < server_data.user_count; i++) {
        if (server_data.users[i].user_id == creator_id) {
            char role_query[256];
            snprintf(role_query, sizeof(role_query), "SELECT role FROM users WHERE id = %d", creator_id);
            sqlite3_stmt *stmt;
            
            if (sqlite3_prepare_v2(db, role_query, -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char *role = (const char *)sqlite3_column_text(stmt, 0);
                    if (role && strcmp(role, "admin") == 0) {
                        is_admin = 1;
                    }
                }
                sqlite3_finalize(stmt);
            }
            break;
        }
    }
    
    if (!is_admin) {
        char response[] = "CREATE_PRACTICE_FAIL|Only admins can create practice rooms\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Validate inputs
    if (room_name == NULL || strlen(room_name) == 0) {
        char response[] = "CREATE_PRACTICE_FAIL|Room name cannot be empty\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Nếu admin bật Show Answers = YES, luôn bỏ qua cooldown
    if (show_answers) {
        time_limit = 0;
    }
    // Validate cooldown (minutes). 0 = no cooldown restriction.
    if (time_limit < 0 || time_limit > 300) {
        char response[] = "CREATE_PRACTICE_FAIL|Invalid cooldown (0-300 minutes, 0 = no limit)\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Insert into database (time_limit is used as cooldown minutes)
    const char *sql = "INSERT INTO practice_rooms (name, creator_id, time_limit, show_answers, is_open, created_at) VALUES (?, ?, ?, ?, 1, ?);";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        char response[] = "CREATE_PRACTICE_FAIL|Database error\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    time_t now = time(NULL);
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, creator_id);
    sqlite3_bind_int(stmt, 3, time_limit);
    sqlite3_bind_int(stmt, 4, show_answers);
    sqlite3_bind_int(stmt, 5, (int)now);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        char response[] = "CREATE_PRACTICE_FAIL|Failed to create practice room\n";
        send(socket_fd, response, strlen(response), 0);
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int practice_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    
    // Add to in-memory structure
    if (server_data.practice_room_count < MAX_ROOMS) {
        PracticeRoom *room = &server_data.practice_rooms[server_data.practice_room_count];
        room->practice_id = practice_id;
        room->creator_id = creator_id;
        strncpy(room->room_name, room_name, sizeof(room->room_name) - 1);
        room->time_limit = time_limit;
        room->show_answers = show_answers;
        room->is_open = 1;
        room->num_questions = 0;
        room->created_time = now;
        server_data.practice_room_count++;
    }
    
    char response[512];
    snprintf(response, sizeof(response), 
             "CREATE_PRACTICE_OK|%d|%s|%d|%d\n", 
             practice_id, room_name, time_limit, show_answers);
    send(socket_fd, response, strlen(response), 0);
    
    printf("[DEBUG] create_practice_room: id=%d, name=%s, limit=%d, show=%d\n",
           practice_id, room_name, time_limit, show_answers);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Trả về danh sách tất cả phòng luyện tập hiện có
 * cùng thông tin cấu hình cơ bản cho client.
 */
void list_practice_rooms(int socket_fd) {
    pthread_mutex_lock(&server_data.lock);
    
    char response[BUFFER_SIZE * 2];
    int offset = snprintf(response, sizeof(response), "PRACTICE_ROOMS_LIST|");
    
    // Mới nhất nằm ở cuối mảng -> duyệt ngược để phòng mới tạo hiển thị trên đầu
    for (int i = server_data.practice_room_count - 1; i >= 0; i--) {
        PracticeRoom *room = &server_data.practice_rooms[i];
        
        // Get creator username
        char creator_name[50] = "Unknown";
        for (int j = 0; j < server_data.user_count; j++) {
            if (server_data.users[j].user_id == room->creator_id) {
                strncpy(creator_name, server_data.users[j].username, sizeof(creator_name) - 1);
                break;
            }
        }
        
        // Count active participants: chỉ tính session đang active VÀ user đang online
        int active_count = 0;
        for (int j = 0; j < server_data.practice_session_count; j++) {
            PracticeSession *s = &server_data.practice_sessions[j];
            if (s->practice_id == room->practice_id && s->is_active == 1) {
                // Kiểm tra user này có đang online không
                for (int u = 0; u < server_data.user_count; u++) {
                    if (server_data.users[u].user_id == s->user_id &&
                        server_data.users[u].is_online == 1) {
                        active_count++;
                        break;
                    }
                }
            }
        }
        
        offset += snprintf(response + offset, sizeof(response) - offset,
                          "%d,%s,%s,%d,%d,%d,%d,%d;",
                          room->practice_id,
                          room->room_name,
                          creator_name,
                          room->time_limit,
                          room->show_answers,
                          room->is_open,
                          room->num_questions,
                          active_count);
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Gắn một câu hỏi vào phòng luyện tập:
 *  - Liên kết practice_id với practice_questions thông qua practice_room_questions.
 */
void add_question_to_practice(int socket_fd, int user_id, int practice_id, int question_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    if (room == NULL || room->creator_id != user_id) {
        char response[] = "ADD_PRACTICE_QUESTION_FAIL|Practice room not found or permission denied\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->num_questions >= MAX_QUESTIONS) {
        char response[] = "ADD_PRACTICE_QUESTION_FAIL|Maximum questions reached\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Check if question exists
    int question_exists = 0;
    for (int i = 0; i < server_data.question_count; i++) {
        if (server_data.questions[i].id == question_id) {
            question_exists = 1;
            break;
        }
    }
    
    if (!question_exists) {
        char response[] = "ADD_PRACTICE_QUESTION_FAIL|Question not found\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Add to database
    const char *sql = "INSERT INTO practice_room_questions (practice_id, question_id, question_order) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        char response[] = "ADD_PRACTICE_QUESTION_FAIL|Database error\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    sqlite3_bind_int(stmt, 1, practice_id);
    sqlite3_bind_int(stmt, 2, question_id);
    sqlite3_bind_int(stmt, 3, room->num_questions);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        char response[] = "ADD_PRACTICE_QUESTION_FAIL|Failed to add question\n";
        send(socket_fd, response, strlen(response), 0);
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    sqlite3_finalize(stmt);
    
    // Add to in-memory
    room->question_ids[room->num_questions++] = question_id;
    
    char response[256];
    snprintf(response, sizeof(response), "ADD_PRACTICE_QUESTION_OK|%d|%d\n", practice_id, question_id);
    send(socket_fd, response, strlen(response), 0);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * User tham gia một phòng luyện tập:
 *  - Tạo practice_session mới, kiểm tra điều kiện thời gian và trạng thái phòng
 *  - Trả về danh sách câu hỏi cho phiên luyện tập.
 */
void join_practice_room(int socket_fd, int user_id, int practice_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    if (room == NULL) {
        char response[] = "JOIN_PRACTICE_FAIL|Practice room not found\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->is_open == 0) {
        char response[] = "JOIN_PRACTICE_FAIL|Practice room is closed\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Ensure question list is loaded from database if in-memory mapping is empty
    if (room->num_questions == 0) {
        sqlite3_stmt *q_stmt = NULL;
        const char *count_sql = "SELECT id FROM practice_questions WHERE practice_id = ? ORDER BY id";
        if (sqlite3_prepare_v2(db, count_sql, -1, &q_stmt, NULL) == SQLITE_OK) {
            int idx = 0;
            while (sqlite3_step(q_stmt) == SQLITE_ROW && idx < MAX_QUESTIONS) {
                int qid = sqlite3_column_int(q_stmt, 0);
                room->question_ids[idx] = qid;
                
                // Also rebuild mapping table for future runs
                sqlite3_stmt *map_stmt = NULL;
                const char *map_sql = "INSERT OR IGNORE INTO practice_room_questions (practice_id, question_id, question_order) VALUES (?, ?, ?);";
                if (sqlite3_prepare_v2(db, map_sql, -1, &map_stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_int(map_stmt, 1, room->practice_id);
                    sqlite3_bind_int(map_stmt, 2, qid);
                    sqlite3_bind_int(map_stmt, 3, idx);
                    sqlite3_step(map_stmt);
                    sqlite3_finalize(map_stmt);
                }
                
                idx++;
            }
            sqlite3_finalize(q_stmt);
            room->num_questions = idx;
        }
    }

    if (room->num_questions == 0) {
        char response[] = "JOIN_PRACTICE_FAIL|No questions in practice room\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Check if user has an active session (always allowed to resume)
    for (int i = 0; i < server_data.practice_session_count; i++) {
        if (server_data.practice_sessions[i].user_id == user_id &&
            server_data.practice_sessions[i].practice_id == practice_id &&
            server_data.practice_sessions[i].is_active == 1) {
            
            // Resume existing session
            PracticeSession *session = &server_data.practice_sessions[i];
            
            // Dynamic allocation: 1KB per question for safety
            size_t buf_size = (size_t)room->num_questions * 1024 + 4096;
            char *response = malloc(buf_size);
            if (!response) {
                char err[] = "JOIN_PRACTICE_FAIL|Memory allocation error\n";
                send(socket_fd, err, strlen(err), 0);
                pthread_mutex_unlock(&server_data.lock);
                return;
            }
            
            int offset = snprintf(response, buf_size, 
                                 "JOIN_PRACTICE_OK|%d|%s|%d|%d|%d|%d|",
                                 practice_id, room->room_name, room->time_limit, 
                                 room->show_answers, room->num_questions, session->session_id);

            // Load questions from practice_questions table based on mapping
            for (int j = 0; j < room->num_questions; j++) {
                int qid = room->question_ids[j];
                sqlite3_stmt *q_stmt = NULL;
                const char *sql_q =
                    "SELECT question_text, option_a, option_b, option_c, option_d, difficulty "
                    "FROM practice_questions WHERE id = ?";

                if (sqlite3_prepare_v2(db, sql_q, -1, &q_stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_int(q_stmt, 1, qid);
                    if (sqlite3_step(q_stmt) == SQLITE_ROW) {
                        const char *q_text = (const char *)sqlite3_column_text(q_stmt, 0);
                        const char *opt_a = (const char *)sqlite3_column_text(q_stmt, 1);
                        const char *opt_b = (const char *)sqlite3_column_text(q_stmt, 2);
                        const char *opt_c = (const char *)sqlite3_column_text(q_stmt, 3);
                        const char *opt_d = (const char *)sqlite3_column_text(q_stmt, 4);
                        const char *difficulty = (const char *)sqlite3_column_text(q_stmt, 5);

                        offset += snprintf(response + offset, buf_size - offset,
                                          "%d~%s~%s~%s~%s~%s~%s~%d",
                                          qid,
                                          q_text ? q_text : "",
                                          opt_a ? opt_a : "",
                                          opt_b ? opt_b : "",
                                          opt_c ? opt_c : "",
                                          opt_d ? opt_d : "",
                                          difficulty ? difficulty : "",
                                          session->answers[j]);

                        if (j < room->num_questions - 1) {
                            offset += snprintf(response + offset, buf_size - offset, "|");
                        }
                    }
                    sqlite3_finalize(q_stmt);
                }
            }
            
            strcat(response, "\n");
            send(socket_fd, response, strlen(response), 0);
            free(response);
            pthread_mutex_unlock(&server_data.lock);
            return;
        }
    }

    // Nếu admin tắt show_answers và cấu hình time_limit > 0 thì coi như
    // đây là thời gian chờ (cooldown) giữa các lần luyện lại.
    if (room->show_answers == 0 && room->time_limit > 0) {
        time_t now = time(NULL);
        time_t latest_end = 0;

        // Tìm phiên luyện tập đã hoàn thành gần nhất
        for (int i = 0; i < server_data.practice_session_count; i++) {
            PracticeSession *s = &server_data.practice_sessions[i];
            if (s->user_id == user_id &&
                s->practice_id == practice_id &&
                s->is_active == 0 &&
                s->end_time > 0) {
                if (s->end_time > latest_end) {
                    latest_end = s->end_time;
                }
            }
        }

        if (latest_end > 0) {
            time_t diff = now - latest_end;
            time_t required = room->time_limit * 60; // phút -> giây
            if (diff < required) {
                int remaining = (int)((required - diff + 59) / 60); // làm tròn lên phút còn lại
                char response[256];
                snprintf(response, sizeof(response),
                         "JOIN_PRACTICE_FAIL|Please wait %d more minutes before practicing again\n",
                         remaining);
                send(socket_fd, response, strlen(response), 0);
                pthread_mutex_unlock(&server_data.lock);
                return;
            }
        }
    }

    // Create new practice session
    const char *sql = "INSERT INTO practice_sessions (practice_id, user_id, start_time, total_questions, is_active) VALUES (?, ?, ?, ?, 1);";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        char response[] = "JOIN_PRACTICE_FAIL|Database error\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    time_t now = time(NULL);
    sqlite3_bind_int(stmt, 1, practice_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_int(stmt, 3, (int)now);
    sqlite3_bind_int(stmt, 4, room->num_questions);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        char response[] = "JOIN_PRACTICE_FAIL|Failed to create session\n";
        send(socket_fd, response, strlen(response), 0);
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int session_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    
    // Add to in-memory
    if (server_data.practice_session_count < MAX_CLIENTS * MAX_ROOMS) {
        PracticeSession *session = &server_data.practice_sessions[server_data.practice_session_count];
        session->session_id = session_id;
        session->practice_id = practice_id;
        session->user_id = user_id;
        session->start_time = now;
        session->end_time = 0;
        session->score = 0;
        session->total_questions = room->num_questions;
        session->is_active = 1;
        
        for (int i = 0; i < MAX_QUESTIONS; i++) {
            session->answers[i] = -1;
            session->is_correct[i] = 0;
        }
        
        server_data.practice_session_count++;
    }
    
    // Send practice room data with questions - dynamic allocation
    size_t buf_size = (size_t)room->num_questions * 1024 + 4096;
    char *response = malloc(buf_size);
    if (!response) {
        char err[] = "JOIN_PRACTICE_FAIL|Memory allocation error\n";
        send(socket_fd, err, strlen(err), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int offset = snprintf(response, buf_size, 
                         "JOIN_PRACTICE_OK|%d|%s|%d|%d|%d|%d|",
                         practice_id, room->room_name, room->time_limit, 
                         room->show_answers, room->num_questions, session_id);
    
    // Send questions loaded from practice_questions table
    for (int i = 0; i < room->num_questions; i++) {
        int qid = room->question_ids[i];
        sqlite3_stmt *q_stmt = NULL;
        const char *sql_q =
            "SELECT question_text, option_a, option_b, option_c, option_d, difficulty "
            "FROM practice_questions WHERE id = ?";

        if (sqlite3_prepare_v2(db, sql_q, -1, &q_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(q_stmt, 1, qid);
            if (sqlite3_step(q_stmt) == SQLITE_ROW) {
                const char *q_text = (const char *)sqlite3_column_text(q_stmt, 0);
                const char *opt_a = (const char *)sqlite3_column_text(q_stmt, 1);
                const char *opt_b = (const char *)sqlite3_column_text(q_stmt, 2);
                const char *opt_c = (const char *)sqlite3_column_text(q_stmt, 3);
                const char *opt_d = (const char *)sqlite3_column_text(q_stmt, 4);
                const char *difficulty = (const char *)sqlite3_column_text(q_stmt, 5);

                offset += snprintf(response + offset, buf_size - offset,
                                  "%d~%s~%s~%s~%s~%s~%s~-1",
                                  qid,
                                  q_text ? q_text : "",
                                  opt_a ? opt_a : "",
                                  opt_b ? opt_b : "",
                                  opt_c ? opt_c : "",
                                  opt_d ? opt_d : "",
                                  difficulty ? difficulty : "");

                if (i < room->num_questions - 1) {
                    offset += snprintf(response + offset, buf_size - offset, "|");
                }
            }
            sqlite3_finalize(q_stmt);
        }
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    free(response);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Lưu đáp án cho một câu hỏi trong phiên luyện tập:
 *  - Cập nhật practice_answers và, nếu cấu hình, đánh dấu đúng/sai ngay.
 */
void submit_practice_answer(int socket_fd, int user_id, int practice_id, int question_num, int answer) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice session
    PracticeSession *session = NULL;
    for (int i = 0; i < server_data.practice_session_count; i++) {
        if (server_data.practice_sessions[i].user_id == user_id &&
            server_data.practice_sessions[i].practice_id == practice_id &&
            server_data.practice_sessions[i].is_active == 1) {
            session = &server_data.practice_sessions[i];
            break;
        }
    }
    
    if (session == NULL) {
        char response[] = "SUBMIT_PRACTICE_ANSWER_FAIL|No active session\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Find practice room
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    if (room == NULL || question_num < 0 || question_num >= room->num_questions) {
        char response[] = "SUBMIT_PRACTICE_ANSWER_FAIL|Invalid question\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Get the question id for this index
    int question_id = room->question_ids[question_num];

    // Look up correct answer from practice_questions in database
    int correct_answer = -1;
    sqlite3_stmt *stmt_q = NULL;
    const char *sql_q = "SELECT correct_answer FROM practice_questions WHERE id = ? AND practice_id = ?";
    if (sqlite3_prepare_v2(db, sql_q, -1, &stmt_q, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt_q, 1, question_id);
        sqlite3_bind_int(stmt_q, 2, practice_id);
        if (sqlite3_step(stmt_q) == SQLITE_ROW) {
            correct_answer = sqlite3_column_int(stmt_q, 0);
        }
        sqlite3_finalize(stmt_q);
    }

    if (correct_answer < 0) {
        char response[] = "SUBMIT_PRACTICE_ANSWER_FAIL|Question not found\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }

    // Check if answer is correct
    int is_correct = (answer == correct_answer) ? 1 : 0;
    
    // Update session
    session->answers[question_num] = answer;
    session->is_correct[question_num] = is_correct;
    
    // Save to database
    const char *sql = "INSERT OR REPLACE INTO practice_answers (session_id, question_id, answer, is_correct, answered_at) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        time_t now = time(NULL);
        sqlite3_bind_int(stmt, 1, session->session_id);
        sqlite3_bind_int(stmt, 2, question_id);
        sqlite3_bind_int(stmt, 3, answer);
        sqlite3_bind_int(stmt, 4, is_correct);
        sqlite3_bind_int(stmt, 5, (int)now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    // Log the attempt
    save_practice_log(user_id, practice_id, question_id, answer, is_correct);
    
    // Send response
    char response[256];
    if (room->show_answers) {
        snprintf(response, sizeof(response), 
                 "SUBMIT_PRACTICE_ANSWER_OK|%d|%d|%d\n", 
                 question_num, answer, is_correct);
    } else {
        snprintf(response, sizeof(response), 
                 "SUBMIT_PRACTICE_ANSWER_OK|%d|%d|-1\n", 
                 question_num, answer);
    }
    
    send(socket_fd, response, strlen(response), 0);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Kết thúc một phiên luyện tập:
 *  - Tính điểm, thời gian làm
 *  - Đánh dấu session là không còn active.
 */
void finish_practice_session(int socket_fd, int user_id, int practice_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice session
    PracticeSession *session = NULL;
    for (int i = 0; i < server_data.practice_session_count; i++) {
        if (server_data.practice_sessions[i].user_id == user_id &&
            server_data.practice_sessions[i].practice_id == practice_id &&
            server_data.practice_sessions[i].is_active == 1) {
            session = &server_data.practice_sessions[i];
            break;
        }
    }
    
    if (session == NULL) {
        char response[] = "FINISH_PRACTICE_FAIL|No active session\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Calculate score
    int score = 0;
    for (int i = 0; i < session->total_questions; i++) {
        if (session->is_correct[i] == 1) {
            score++;
        }
    }
    
    session->score = score;
    session->end_time = time(NULL);
    session->is_active = 0;
    
    // Update database
    const char *sql = "UPDATE practice_sessions SET score = ?, end_time = ?, is_active = 0 WHERE id = ?;";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, score);
        sqlite3_bind_int(stmt, 2, (int)session->end_time);
        sqlite3_bind_int(stmt, 3, session->session_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    char response[256];
    snprintf(response, sizeof(response), 
             "FINISH_PRACTICE_OK|%d|%d|%d\n", 
             practice_id, score, session->total_questions);
    send(socket_fd, response, strlen(response), 0);
    
    printf("[DEBUG] finish_practice_session: user=%d, room=%d, score=%d/%d\n",
           user_id, practice_id, score, session->total_questions);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Xem kết quả chi tiết của một phòng luyện tập cho user:
 *  - Trả về điểm, tổng câu hỏi, có thể kèm từng câu nếu được lưu.
 */
void view_practice_results(int socket_fd, int user_id, int practice_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find latest session for this user and practice
    PracticeSession *session = NULL;
    for (int i = server_data.practice_session_count - 1; i >= 0; i--) {
        if (server_data.practice_sessions[i].user_id == user_id &&
            server_data.practice_sessions[i].practice_id == practice_id) {
            session = &server_data.practice_sessions[i];
            break;
        }
    }
    
    if (session == NULL) {
        char response[] = "PRACTICE_RESULTS_FAIL|No session found\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Find practice room
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    if (room == NULL) {
        char response[] = "PRACTICE_RESULTS_FAIL|Practice room not found\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Build response with all questions and answers - dynamic allocation
    size_t buf_size = (size_t)room->num_questions * 1024 + 4096;
    char *response = malloc(buf_size);
    if (!response) {
        char err[] = "PRACTICE_RESULTS_FAIL|Memory allocation error\n";
        send(socket_fd, err, strlen(err), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int offset = snprintf(response, buf_size, 
                         "PRACTICE_RESULTS|%d|%d|%d|",
                         practice_id, session->score, session->total_questions);
    
    for (int i = 0; i < room->num_questions; i++) {
        int qid = room->question_ids[i];

        // Load question details from practice_questions
        sqlite3_stmt *stmt_q = NULL;
        const char *sql_q =
            "SELECT question_text, option_a, option_b, option_c, option_d, correct_answer "
            "FROM practice_questions WHERE id = ?";

        if (sqlite3_prepare_v2(db, sql_q, -1, &stmt_q, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt_q, 1, qid);
            if (sqlite3_step(stmt_q) == SQLITE_ROW) {
                const char *q_text = (const char *)sqlite3_column_text(stmt_q, 0);
                const char *opt_a = (const char *)sqlite3_column_text(stmt_q, 1);
                const char *opt_b = (const char *)sqlite3_column_text(stmt_q, 2);
                const char *opt_c = (const char *)sqlite3_column_text(stmt_q, 3);
                const char *opt_d = (const char *)sqlite3_column_text(stmt_q, 4);
                int correct_answer = sqlite3_column_int(stmt_q, 5);

                offset += snprintf(response + offset, buf_size - offset,
                                  "%d~%s~%s~%s~%s~%s~%d~%d~%d",
                                  qid,
                                  q_text ? q_text : "",
                                  opt_a ? opt_a : "",
                                  opt_b ? opt_b : "",
                                  opt_c ? opt_c : "",
                                  opt_d ? opt_d : "",
                                  correct_answer,
                                  session->answers[i],
                                  session->is_correct[i]);

                if (i < room->num_questions - 1) {
                    offset += snprintf(response + offset, buf_size - offset, "|");
                }
            }
            sqlite3_finalize(stmt_q);
        }
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    free(response);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Tạo lại session luyện tập mới cho cùng một phòng,
 * cho phép user luyện lại từ đầu.
 */
void restart_practice(int socket_fd, int user_id, int practice_id) {
    // Just join again - it will create a new session
    join_practice_room(socket_fd, user_id, practice_id);
}

/*
 * Đóng phòng luyện tập (chỉ chủ phòng/admin):
 *  - Không cho phép user mới tham gia, các session hiện tại vẫn giữ.
 */
void close_practice_room(int socket_fd, int user_id, int practice_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    if (room == NULL || room->creator_id != user_id) {
        char response[] = "CLOSE_PRACTICE_FAIL|Practice room not found or permission denied\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    room->is_open = 0;
    
    // Update database
    char sql[256];
    snprintf(sql, sizeof(sql), "UPDATE practice_rooms SET is_open = 0 WHERE id = %d;", practice_id);
    sqlite3_exec(db, sql, 0, 0, 0);
    
    // Kick out all active users
    for (int i = 0; i < server_data.practice_session_count; i++) {
        if (server_data.practice_sessions[i].practice_id == practice_id &&
            server_data.practice_sessions[i].is_active == 1) {
            
            int target_user_id = server_data.practice_sessions[i].user_id;
            
            // Find user's socket and send kick message
            for (int j = 0; j < server_data.user_count; j++) {
                if (server_data.users[j].user_id == target_user_id && 
                    server_data.users[j].is_online == 1) {
                    
                    char kick_msg[256];
                    snprintf(kick_msg, sizeof(kick_msg), 
                             "PRACTICE_CLOSED|%d|%s\n", 
                             practice_id, room->room_name);
                    send(server_data.users[j].socket_fd, kick_msg, strlen(kick_msg), 0);
                    break;
                }
            }
            
            // Mark session as inactive
            server_data.practice_sessions[i].is_active = 0;
        }
    }
    
    char response[256];
    snprintf(response, sizeof(response), "CLOSE_PRACTICE_OK|%d\n", practice_id);
    send(socket_fd, response, strlen(response), 0);
    
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Mở lại phòng luyện tập đã đóng để user có thể tiếp tục tham gia.
 */
void open_practice_room(int socket_fd, int user_id, int practice_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    if (room == NULL || room->creator_id != user_id) {
        char response[] = "OPEN_PRACTICE_FAIL|Practice room not found or permission denied\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    room->is_open = 1;
    
    // Update database
    char sql[256];
    snprintf(sql, sizeof(sql), "UPDATE practice_rooms SET is_open = 1 WHERE id = %d;", practice_id);
    sqlite3_exec(db, sql, 0, 0, 0);
    
    char response[256];
    snprintf(response, sizeof(response), "OPEN_PRACTICE_OK|%d\n", practice_id);
    send(socket_fd, response, strlen(response), 0);
    
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Lấy danh sách user đã hoặc đang tham gia một phòng luyện tập.
 */
void get_practice_participants(int socket_fd, int user_id, int practice_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Check if user is creator
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    if (room == NULL || room->creator_id != user_id) {
        char response[] = "PRACTICE_PARTICIPANTS_FAIL|Permission denied\n";
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Build response with active participants
    char response[BUFFER_SIZE];
    int offset = snprintf(response, sizeof(response), "PRACTICE_PARTICIPANTS|%d|", practice_id);
    
    int count = 0;
    for (int i = 0; i < server_data.practice_session_count; i++) {
        if (server_data.practice_sessions[i].practice_id == practice_id &&
            server_data.practice_sessions[i].is_active == 1) {
            
            PracticeSession *session = &server_data.practice_sessions[i];
            
            // Get username
            char username[50] = "Unknown";
            for (int j = 0; j < server_data.user_count; j++) {
                if (server_data.users[j].user_id == session->user_id) {
                    strncpy(username, server_data.users[j].username, sizeof(username) - 1);
                    break;
                }
            }
            
            // Calculate current score
            int current_score = 0;
            int answered = 0;
            for (int j = 0; j < session->total_questions; j++) {
                if (session->answers[j] != -1) {
                    answered++;
                    if (session->is_correct[j] == 1) {
                        current_score++;
                    }
                }
            }
            
            offset += snprintf(response + offset, sizeof(response) - offset,
                              "%d,%s,%d,%d,%d;",
                              session->user_id, username, current_score, 
                              answered, session->total_questions);
            count++;
        }
    }
    
    if (count == 0) {
        offset += snprintf(response + offset, sizeof(response) - offset, "NONE");
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Lưu log chi tiết cho từng lần trả lời câu hỏi trong chế độ luyện tập
 * vào bảng practice_logs để phân tích sau này.
 */
void save_practice_log(int user_id, int practice_id, int question_id, int answer, int is_correct) {
    const char *sql = "INSERT INTO practice_logs (user_id, practice_id, question_id, answer, is_correct, attempt_time) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        time_t now = time(NULL);
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_int(stmt, 2, practice_id);
        sqlite3_bind_int(stmt, 3, question_id);
        sqlite3_bind_int(stmt, 4, answer);
        sqlite3_bind_int(stmt, 5, is_correct);
        sqlite3_bind_int(stmt, 6, (int)now);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) {
        printf("[DEBUG] save_practice_log: user=%d, room=%d, qid=%d, ans=%d, correct=%d\n",
                   user_id, practice_id, question_id, answer, is_correct);
        }
        sqlite3_finalize(stmt);
    }
}

/*
 * Lấy danh sách các phòng luyện tập do một user (thường là admin) tạo.
 */
void get_user_practice_rooms(int socket_fd, int user_id) {
    pthread_mutex_lock(&server_data.lock);
    
    char response[BUFFER_SIZE * 2] = "PRACTICE_ROOMS_LIST";
    int has_rooms = 0;
    
    for (int i = 0; i < server_data.practice_room_count; i++) {
        PracticeRoom *room = &server_data.practice_rooms[i];
        
        if (room->creator_id == user_id) {
            has_rooms = 1;
            char room_entry[256];
            snprintf(room_entry, sizeof(room_entry), "|%d:%s", 
                    room->practice_id, room->room_name);
            strcat(response, room_entry);
        }
    }
    
    pthread_mutex_unlock(&server_data.lock);
    
    if (!has_rooms) {
        send(socket_fd, "NO_PRACTICE_ROOMS\n", 18, 0);
    } else {
        strcat(response, "\n");
        send(socket_fd, response, strlen(response), 0);
    }
}

/*
 * Xóa hoàn toàn một phòng luyện tập khỏi DB:
 *  - Kèm theo toàn bộ câu hỏi liên quan và session/answer/log.
 */
void delete_practice_room(int socket_fd, int user_id, int practice_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room
    PracticeRoom *room = NULL;
    int room_idx = -1;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            room_idx = i;
            break;
        }
    }
    
    char response[256];
    
    if (room == NULL) {
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->creator_id != user_id) {
        snprintf(response, sizeof(response), "DELETE_PRACTICE_FAIL|Permission denied\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Delete from database
    char query[512];
    char *err_msg = 0;
    
    // Delete practice sessions first (foreign key constraint)
    snprintf(query, sizeof(query), "DELETE FROM practice_sessions WHERE practice_id=%d;", practice_id);
    if (sqlite3_exec(db, query, 0, 0, &err_msg) != SQLITE_OK) {
        sqlite3_free(err_msg);
    }
    
    // Delete practice logs
    snprintf(query, sizeof(query), "DELETE FROM practice_logs WHERE practice_id=%d;", practice_id);
    if (sqlite3_exec(db, query, 0, 0, &err_msg) != SQLITE_OK) {
        sqlite3_free(err_msg);
    }
    
    // Delete practice_questions mapping
    snprintf(query, sizeof(query), "DELETE FROM practice_questions WHERE practice_id=%d;", practice_id);
    if (sqlite3_exec(db, query, 0, 0, &err_msg) != SQLITE_OK) {
        sqlite3_free(err_msg);
    }
    
    // Finally delete the practice room itself
    snprintf(query, sizeof(query), "DELETE FROM practice_rooms WHERE id=%d;", practice_id);
    if (sqlite3_exec(db, query, 0, 0, &err_msg) != SQLITE_OK) {
        snprintf(response, sizeof(response), "DELETE_PRACTICE_FAIL|Database error: %s\n", err_msg);
        sqlite3_free(err_msg);
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Remove from in-memory array
    for (int i = room_idx; i < server_data.practice_room_count - 1; i++) {
        server_data.practice_rooms[i] = server_data.practice_rooms[i + 1];
    }
    server_data.practice_room_count--;
    
    // Remove all related sessions from in-memory
    for (int i = 0; i < server_data.practice_session_count; ) {
        if (server_data.practice_sessions[i].practice_id == practice_id) {
            // Shift array
            for (int j = i; j < server_data.practice_session_count - 1; j++) {
                server_data.practice_sessions[j] = server_data.practice_sessions[j + 1];
            }
            server_data.practice_session_count--;
        } else {
            i++;
        }
    }
    
    send(socket_fd, response, strlen(response), 0);
    log_activity(user_id, "DELETE_PRACTICE", "Deleted practice room");
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Lấy danh sách câu hỏi thuộc một phòng luyện tập,
 * phục vụ màn hình quản trị hoặc chỉnh sửa.
 */
void get_practice_questions(int socket_fd, int user_id, int practice_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room and verify permission
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    char response[BUFFER_SIZE * 2];
    
    if (room == NULL) {
        snprintf(response, sizeof(response), "GET_PRACTICE_QUESTIONS_FAIL|Room not found\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->creator_id != user_id) {
        snprintf(response, sizeof(response), "GET_PRACTICE_QUESTIONS_FAIL|Permission denied\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Build response with question details from database
    int offset = snprintf(response, sizeof(response), "PRACTICE_QUESTIONS_LIST|%d", practice_id);
    
    // Query practice questions from database
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id, question_text, option_a, option_b, option_c, option_d, "
             "correct_answer, difficulty, category FROM practice_questions "
             "WHERE practice_id=%d ORDER BY id",
             practice_id);
    
    sqlite3_stmt *stmt;
    int question_count = 0;
    
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int q_id = sqlite3_column_int(stmt, 0);
            const char *q_text = (const char *)sqlite3_column_text(stmt, 1);
            const char *opt_a = (const char *)sqlite3_column_text(stmt, 2);
            const char *opt_b = (const char *)sqlite3_column_text(stmt, 3);
            const char *opt_c = (const char *)sqlite3_column_text(stmt, 4);
            const char *opt_d = (const char *)sqlite3_column_text(stmt, 5);
            int correct = sqlite3_column_int(stmt, 6);
            const char *difficulty = (const char *)sqlite3_column_text(stmt, 7);
            const char *category = (const char *)sqlite3_column_text(stmt, 8);
            
            offset += snprintf(response + offset, sizeof(response) - offset,
                             "|%d:%s:%s:%s:%s:%s:%d:%s:%s",
                             q_id, q_text ? q_text : "",
                             opt_a ? opt_a : "", opt_b ? opt_b : "",
                             opt_c ? opt_c : "", opt_d ? opt_d : "",
                             correct, difficulty ? difficulty : "", category ? category : "");
            question_count++;
        }
        sqlite3_finalize(stmt);
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
    printf("[DEBUG] get_practice_questions: count=%d, room=%d, user=%d\n",
           question_count, practice_id, user_id);
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Cập nhật nội dung một câu hỏi luyện tập (text, đáp án, độ khó, category).
 */
void update_practice_question(int socket_fd, int user_id, int practice_id, int question_id, char *new_data) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room and verify permission
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    char response[256];
    
    if (room == NULL) {
        snprintf(response, sizeof(response), "UPDATE_PRACTICE_QUESTION_FAIL|Room not found\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->creator_id != user_id) {
        snprintf(response, sizeof(response), "UPDATE_PRACTICE_QUESTION_FAIL|Permission denied\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Parse new_data: question_text|opt1|opt2|opt3|opt4|correct|difficulty|category
    char *q_text = strtok(new_data, "|");
    char *opt1 = strtok(NULL, "|");
    char *opt2 = strtok(NULL, "|");
    char *opt3 = strtok(NULL, "|");
    char *opt4 = strtok(NULL, "|");
    char *correct_str = strtok(NULL, "|");
    char *difficulty = strtok(NULL, "|");
    char *category = strtok(NULL, "|");
    
    if (!q_text || !opt1 || !opt2 || !opt3 || !opt4 || !correct_str || !difficulty || !category) {
        snprintf(response, sizeof(response), "UPDATE_PRACTICE_QUESTION_FAIL|Invalid data format\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int correct_answer = atoi(correct_str);
    
    // Update in database (practice_questions table)
    char query[2048];
    char *err_msg = 0;
    snprintf(query, sizeof(query),
             "UPDATE practice_questions SET question_text='%s', option_a='%s', option_b='%s', "
             "option_c='%s', option_d='%s', correct_answer=%d, difficulty='%s', category='%s' "
             "WHERE id=%d;",
             q_text, opt1, opt2, opt3, opt4, correct_answer, difficulty, category, question_id);
    
    if (sqlite3_exec(db, query, 0, 0, &err_msg) != SQLITE_OK) {
        snprintf(response, sizeof(response), "UPDATE_PRACTICE_QUESTION_FAIL|Database error\n");
        sqlite3_free(err_msg);
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Update in-memory
    for (int i = 0; i < server_data.question_count; i++) {
        if (server_data.questions[i].id == question_id) {
            strncpy(server_data.questions[i].text, q_text, sizeof(server_data.questions[i].text) - 1);
            strncpy(server_data.questions[i].options[0], opt1, sizeof(server_data.questions[i].options[0]) - 1);
            strncpy(server_data.questions[i].options[1], opt2, sizeof(server_data.questions[i].options[1]) - 1);
            strncpy(server_data.questions[i].options[2], opt3, sizeof(server_data.questions[i].options[2]) - 1);
            strncpy(server_data.questions[i].options[3], opt4, sizeof(server_data.questions[i].options[3]) - 1);
            server_data.questions[i].correct_answer = correct_answer;
            strncpy(server_data.questions[i].difficulty, difficulty, sizeof(server_data.questions[i].difficulty) - 1);
            strncpy(server_data.questions[i].category, category, sizeof(server_data.questions[i].category) - 1);
            break;
        }
    }
    
    snprintf(response, sizeof(response), "UPDATE_PRACTICE_QUESTION_OK|Question updated successfully\n");
    send(socket_fd, response, strlen(response), 0);
    
    printf("[DEBUG] update_practice_question: qid=%d, room=%d, user=%d\n",
           question_id, practice_id, user_id);
    log_activity(user_id, "UPDATE_PRACTICE_QUESTION", "Updated practice question");
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Tạo mới câu hỏi luyện tập trực tiếp từ phía admin
 * cho một phòng luyện tập nhất định.
 */
void create_practice_question(int socket_fd, int user_id, int practice_id, char *question_data) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room and verify permission
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    char response[256];
    
    if (room == NULL) {
        snprintf(response, sizeof(response), "ADD_PRACTICE_QUESTION_FAIL|Room not found\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->creator_id != user_id) {
        snprintf(response, sizeof(response), "ADD_PRACTICE_QUESTION_FAIL|Permission denied\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Parse: question_text|opt1|opt2|opt3|opt4|correct|difficulty|category
    char *q_text = strtok(question_data, "|");
    char *opt1 = strtok(NULL, "|");
    char *opt2 = strtok(NULL, "|");
    char *opt3 = strtok(NULL, "|");
    char *opt4 = strtok(NULL, "|");
    char *correct_str = strtok(NULL, "|");
    char *difficulty = strtok(NULL, "|");
    char *category = strtok(NULL, "|");
    
    if (!q_text || !opt1 || !opt2 || !opt3 || !opt4 || !correct_str || !difficulty || !category) {
        snprintf(response, sizeof(response), "ADD_PRACTICE_QUESTION_FAIL|Invalid data format\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int correct_answer = atoi(correct_str);
    
    // Insert into practice_questions table
    char *query = sqlite3_mprintf(
        "INSERT INTO practice_questions (practice_id, question_text, option_a, option_b, option_c, option_d, correct_answer, difficulty, category) "
        "VALUES (%d, %Q, %Q, %Q, %Q, %Q, %d, %Q, %Q)",
        practice_id, q_text, opt1, opt2, opt3, opt4, correct_answer, difficulty, category);
    
    if (!query) {
        snprintf(response, sizeof(response), "ADD_PRACTICE_QUESTION_FAIL|Memory allocation failed\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    char *err_msg = NULL;
    if (sqlite3_exec(db, query, NULL, NULL, &err_msg) != SQLITE_OK) {
        snprintf(response, sizeof(response), "ADD_PRACTICE_QUESTION_FAIL|Database error\n");
        sqlite3_free(err_msg);
        send(socket_fd, response, strlen(response), 0);
        sqlite3_free(query);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int question_id = sqlite3_last_insert_rowid(db);
    sqlite3_free(query);
    
    // Add mapping to practice_room_questions
    const char *sql_mapping = "INSERT INTO practice_room_questions (practice_id, question_id, question_order) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql_mapping, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, practice_id);
        sqlite3_bind_int(stmt, 2, question_id);
        sqlite3_bind_int(stmt, 3, room->num_questions);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    // Update in-memory
    if (room->num_questions < MAX_QUESTIONS) {
        room->question_ids[room->num_questions++] = question_id;
    }
    
    snprintf(response, sizeof(response), "ADD_PRACTICE_QUESTION_OK|Question added successfully\n");
    send(socket_fd, response, strlen(response), 0);
    
    printf("[DEBUG] create_practice_question: qid=%d, room=%d, user=%d\n",
           question_id, practice_id, user_id);
    log_activity(user_id, "CREATE_PRACTICE_QUESTION", "Created practice question");
    
    pthread_mutex_unlock(&server_data.lock);
}

/*
 * Import các câu hỏi luyện tập từ file CSV cho một phòng luyện tập.
 */
void import_practice_csv(int socket_fd, int user_id, int practice_id, const char *filename) {
    pthread_mutex_lock(&server_data.lock);
    
    // Find practice room and verify permission
    PracticeRoom *room = NULL;
    for (int i = 0; i < server_data.practice_room_count; i++) {
        if (server_data.practice_rooms[i].practice_id == practice_id) {
            room = &server_data.practice_rooms[i];
            break;
        }
    }
    
    char response[256];
    
    if (room == NULL) {
        snprintf(response, sizeof(response), "IMPORT_PRACTICE_CSV_FAIL|Room not found\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (room->creator_id != user_id) {
        snprintf(response, sizeof(response), "IMPORT_PRACTICE_CSV_FAIL|Permission denied\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        snprintf(response, sizeof(response), "IMPORT_PRACTICE_CSV_FAIL|Cannot open file\n");
        send(socket_fd, response, strlen(response), 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    char line[2048];
    int imported = 0;
    int line_num = 0;
    int header_skipped = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        
        // Skip empty lines and comments
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\r') continue;
        
        // Skip header row (first non-empty, non-comment line)
        if (!header_skipped) {
            header_skipped = 1;
            continue;
        }
        
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;
        
        // Parse CSV: question,optA,optB,optC,optD,correct,difficulty,category
        char q_text[512], opt_a[256], opt_b[256], opt_c[256], opt_d[256];
        char difficulty[32], category[128];
        int correct;
        
        char *token = strtok(line, ",");
        if (!token) continue;
        strncpy(q_text, token, sizeof(q_text)-1);
        q_text[sizeof(q_text)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(opt_a, token, sizeof(opt_a)-1);
        opt_a[sizeof(opt_a)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(opt_b, token, sizeof(opt_b)-1);
        opt_b[sizeof(opt_b)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(opt_c, token, sizeof(opt_c)-1);
        opt_c[sizeof(opt_c)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(opt_d, token, sizeof(opt_d)-1);
        opt_d[sizeof(opt_d)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        correct = atoi(token);
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(difficulty, token, sizeof(difficulty)-1);
        difficulty[sizeof(difficulty)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(category, token, sizeof(category)-1);
        category[sizeof(category)-1] = '\0';
        
        // Insert into database
        char *query = sqlite3_mprintf(
            "INSERT INTO practice_questions (practice_id, question_text, option_a, option_b, option_c, option_d, correct_answer, difficulty, category) "
            "VALUES (%d, %Q, %Q, %Q, %Q, %Q, %d, %Q, %Q);",
            practice_id, q_text, opt_a, opt_b, opt_c, opt_d, correct, difficulty, category);
        
        if (!query) continue;
        
        if (sqlite3_exec(db, query, NULL, NULL, NULL) == SQLITE_OK) {
            int question_id = sqlite3_last_insert_rowid(db);
            
            // Add mapping
            const char *sql_mapping = "INSERT INTO practice_room_questions (practice_id, question_id, question_order) VALUES (?, ?, ?);";
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(db, sql_mapping, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, practice_id);
                sqlite3_bind_int(stmt, 2, question_id);
                sqlite3_bind_int(stmt, 3, room->num_questions);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            
            // Update in-memory
            if (room->num_questions < MAX_QUESTIONS) {
                room->question_ids[room->num_questions++] = question_id;
            }
            
            imported++;
        }
        sqlite3_free(query);
    }
    
    fclose(fp);
    
    snprintf(response, sizeof(response), "IMPORT_PRACTICE_CSV_OK|%d\n", imported);
    send(socket_fd, response, strlen(response), 0);
    
    log_activity(user_id, "IMPORT_PRACTICE_CSV", "Imported practice questions from CSV");
    
    pthread_mutex_unlock(&server_data.lock);
}
