#include "results.h"
#include "db.h"
#include <sys/socket.h>
#include <time.h>
#include <string.h>

extern ServerData server_data;
extern sqlite3 *db;

// ============================================================================
// HELPER FUNCTIONS - Using Prepared Statements
// ============================================================================

// Helper: Lấy start_time của user trong room
static long get_user_start_time(int user_id, int room_id)
{
    const char *sql = "SELECT start_time FROM participants WHERE room_id = ? AND user_id = ?";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[ERROR] Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, user_id);
    
    long start_time = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        start_time = sqlite3_column_int64(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return start_time;
}

// Helper: Kiểm tra user đã submit chưa
static int has_user_submitted(int user_id, int room_id)
{
    const char *sql = "SELECT id FROM results WHERE room_id = ? AND user_id = ?";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[ERROR] Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, user_id);
    
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return exists;
}

// Helper: Lấy duration của room (phút)
static int get_room_duration(int room_id)
{
    const char *sql = "SELECT duration FROM rooms WHERE id = ?";
    
    sqlite3_stmt *stmt;
    int duration = 60; // Default 60 phút
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, room_id);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            duration = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        fprintf(stderr, "[ERROR] Failed to get room duration: %s\n", sqlite3_errmsg(db));
    }
    
    return duration;
}

// Helper: Tính điểm từ user_answers
static void calculate_score(int user_id, int room_id, int *score, int *total_questions)
{
    // Đếm số câu trả lời đúng
    const char *sql_score = 
        "SELECT COUNT(*) FROM user_answers ua "
        "JOIN questions q ON ua.question_id = q.id "
        "WHERE ua.user_id = ? AND ua.room_id = ? "
        "AND ua.selected_answer = q.correct_answer";
    
    sqlite3_stmt *stmt;
    *score = 0;
    
    if (sqlite3_prepare_v2(db, sql_score, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_int(stmt, 2, room_id);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *score = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    // Đếm tổng số câu hỏi
    const char *sql_total = "SELECT COUNT(*) FROM questions WHERE room_id = ?";
    
    *total_questions = 0;
    if (sqlite3_prepare_v2(db, sql_total, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, room_id);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *total_questions = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
}

// Helper: Lưu kết quả vào database với submit_reason
static int save_result_to_db(int user_id, int room_id, int score, 
                             int total_questions, long time_taken, 
                             const char *submit_reason)
{
    const char *sql = 
        "INSERT INTO results (user_id, room_id, score, total_questions, time_taken, submit_reason, submitted_at) "
        "VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[ERROR] Failed to prepare insert: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, room_id);
    sqlite3_bind_int(stmt, 3, score);
    sqlite3_bind_int(stmt, 4, total_questions);
    sqlite3_bind_int64(stmt, 5, time_taken);
    sqlite3_bind_text(stmt, 6, submit_reason, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[ERROR] Failed to insert result: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    return 0;
}

// Helper: Validate question_id exists in room
static int validate_question(int room_id, int question_id)
{
    const char *sql = "SELECT id FROM questions WHERE room_id = ? AND id = ?";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, question_id);
    
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return exists;
}

// ============================================================================
// CACHE MANAGEMENT - Score realtime cho leaderboard
// ============================================================================

typedef struct {
    int user_id;
    int room_id;
    int score;
    int total_questions;
    long time_taken;
    time_t last_update;
} ScoreCache;

#define MAX_CACHE_ENTRIES 1000
static ScoreCache score_cache[MAX_CACHE_ENTRIES];
static int cache_count = 0;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

// Update cache khi user trả lời câu hỏi
void update_score_cache(int user_id, int room_id)
{
    pthread_mutex_lock(&cache_lock);
    
    int score, total_questions;
    calculate_score(user_id, room_id, &score, &total_questions);
    
    long start_time = get_user_start_time(user_id, room_id);
    long time_taken = (start_time > 0) ? (time(NULL) - start_time) : 0;
    
    // Tìm entry trong cache
    int found = -1;
    for (int i = 0; i < cache_count; i++) {
        if (score_cache[i].user_id == user_id && score_cache[i].room_id == room_id) {
            found = i;
            break;
        }
    }
    
    if (found >= 0) {
        // Update existing entry
        score_cache[found].score = score;
        score_cache[found].total_questions = total_questions;
        score_cache[found].time_taken = time_taken;
        score_cache[found].last_update = time(NULL);
    } else if (cache_count < MAX_CACHE_ENTRIES) {
        // Add new entry
        score_cache[cache_count].user_id = user_id;
        score_cache[cache_count].room_id = room_id;
        score_cache[cache_count].score = score;
        score_cache[cache_count].total_questions = total_questions;
        score_cache[cache_count].time_taken = time_taken;
        score_cache[cache_count].last_update = time(NULL);
        cache_count++;
    }
    
    pthread_mutex_unlock(&cache_lock);
}

// Lấy score từ cache (cho leaderboard realtime)
int get_cached_score(int user_id, int room_id, int *score, int *total_questions)
{
    pthread_mutex_lock(&cache_lock);
    
    for (int i = 0; i < cache_count; i++) {
        if (score_cache[i].user_id == user_id && score_cache[i].room_id == room_id) {
            *score = score_cache[i].score;
            *total_questions = score_cache[i].total_questions;
            pthread_mutex_unlock(&cache_lock);
            return 1; // Found
        }
    }
    
    pthread_mutex_unlock(&cache_lock);
    return 0; // Not found
}

// Xóa cache khi user submit (để tránh stale data)
void clear_score_cache(int user_id, int room_id)
{
    pthread_mutex_lock(&cache_lock);
    
    for (int i = 0; i < cache_count; i++) {
        if (score_cache[i].user_id == user_id && score_cache[i].room_id == room_id) {
            // Shift array left
            for (int j = i; j < cache_count - 1; j++) {
                score_cache[j] = score_cache[j + 1];
            }
            cache_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&cache_lock);
}

// ============================================================================
// MAIN FUNCTIONS
// ============================================================================

// Lưu đáp án realtime (INSERT OR REPLACE để update nếu đã tồn tại)
void save_answer(int socket_fd, int user_id, int room_id, int question_id, int selected_answer)
{
    pthread_mutex_lock(&server_data.lock);
    
    // Validate selected_answer (0-3 = A-D)
    if (selected_answer < 0 || selected_answer > 3) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Invalid answer\n", 33, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Validate question_id belongs to room
    if (!validate_question(room_id, question_id)) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Invalid question\n", 35, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Chặn nếu đã submit
    if (has_user_submitted(user_id, room_id)) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Already submitted\n", 36, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Kiểm tra đã bắt đầu thi chưa
    long start_time = get_user_start_time(user_id, room_id);
    if (start_time == 0) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Not started yet\n", 34, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Kiểm tra timeout
    int duration = get_room_duration(room_id);
    long elapsed = time(NULL) - start_time;
    if (elapsed > duration * 60) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Time expired\n", 31, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // INSERT OR REPLACE với prepared statement
    const char *sql = 
        "INSERT OR REPLACE INTO user_answers (user_id, room_id, question_id, selected_answer, answered_at) "
        "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        send(socket_fd, "SAVE_ANSWER_FAIL|Database error\n", 33, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, room_id);
    sqlite3_bind_int(stmt, 3, question_id);
    sqlite3_bind_int(stmt, 4, selected_answer);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        send(socket_fd, "SAVE_ANSWER_OK\n", 15, 0);
        
        // Update cache cho leaderboard realtime
        update_score_cache(user_id, room_id);
        
        printf("[DEBUG] User %d answered Q%d = %d in room %d\n", 
               user_id, question_id, selected_answer, room_id);
    } else {
        char error[128];
        snprintf(error, sizeof(error), "SAVE_ANSWER_FAIL|%s\n", sqlite3_errmsg(db));
        send(socket_fd, error, strlen(error), 0);
    }
    
    pthread_mutex_unlock(&server_data.lock);
}

// Submit test (manual) - với chặn double submit
void submit_test(int socket_fd, int user_id, int room_id)
{
    pthread_mutex_lock(&server_data.lock);
    
    // CHẶN DOUBLE SUBMIT - Kiểm tra đầu tiên
    if (has_user_submitted(user_id, room_id)) {
        send(socket_fd, "SUBMIT_TEST_FAIL|Already submitted\n", 36, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Kiểm tra user đã bắt đầu thi chưa
    long start_time = get_user_start_time(user_id, room_id);
    if (start_time == 0) {
        send(socket_fd, "SUBMIT_TEST_FAIL|Not started yet\n", 34, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Tính thời gian và cap ở max duration
    time_t now = time(NULL);
    long elapsed = now - start_time;
    int duration = get_room_duration(room_id);
    long max_time = duration * 60;
    
    if (elapsed > max_time) {
        elapsed = max_time; // Cap ở max time
    }
    
    // Tính điểm
    int score, total_questions;
    calculate_score(user_id, room_id, &score, &total_questions);
    
    // Lưu kết quả với submit_reason = "manual"
    if (save_result_to_db(user_id, room_id, score, total_questions, 
                          elapsed, "manual") != 0) {
        send(socket_fd, "SUBMIT_TEST_FAIL|Database error\n", 33, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Xóa cache sau khi submit
    clear_score_cache(user_id, room_id);
    
    // Response
    char response[200];
    snprintf(response, sizeof(response), "SUBMIT_TEST_OK|%d|%d|%ld\n", 
             score, total_questions, elapsed / 60);
    send(socket_fd, response, strlen(response), 0);
    
    log_activity(user_id, "SUBMIT_TEST", "Test submitted manually");
    printf("[INFO] User %d submitted test (manual) - Score: %d/%d, Time: %ld min\n", 
           user_id, score, total_questions, elapsed / 60);
    
    pthread_mutex_unlock(&server_data.lock);
}

// Auto-submit khi timeout
void auto_submit_on_timeout(int user_id, int room_id)
{
    pthread_mutex_lock(&server_data.lock);
    
    printf("[INFO] Auto-submit (timeout) for user %d in room %d\n", user_id, room_id);
    
    // CHẶN DOUBLE SUBMIT
    if (has_user_submitted(user_id, room_id)) {
        printf("[DEBUG] User %d already submitted, skipping auto-submit\n", user_id);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    long start_time = get_user_start_time(user_id, room_id);
    if (start_time == 0) {
        pthread_mutex_unlock(&server_data.lock);
        return; // Chưa bắt đầu thi
    }
    
    // Tính thời gian = duration (vì timeout)
    int duration = get_room_duration(room_id);
    long time_taken = duration * 60;
    
    // Tính điểm
    int score, total_questions;
    calculate_score(user_id, room_id, &score, &total_questions);
    
    // Lưu kết quả với submit_reason = "timeout"
    save_result_to_db(user_id, room_id, score, total_questions, 
                     time_taken, "timeout");
    
    // Xóa cache
    clear_score_cache(user_id, room_id);
    
    log_activity(user_id, "AUTO_SUBMIT_TIMEOUT", "Test auto-submitted on timeout");
    printf("[INFO] Auto-submitted (timeout) for user %d - Score: %d/%d\n", 
           user_id, score, total_questions);
    
    pthread_mutex_unlock(&server_data.lock);
}

// Auto-submit khi user disconnect
void auto_submit_on_disconnect(int user_id, int room_id)
{
    pthread_mutex_lock(&server_data.lock);
    
    printf("[INFO] Auto-submit (disconnect) for user %d in room %d\n", user_id, room_id);
    
    // CHẶN DOUBLE SUBMIT
    if (has_user_submitted(user_id, room_id)) {
        printf("[DEBUG] User %d already submitted, skipping auto-submit\n", user_id);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    long start_time = get_user_start_time(user_id, room_id);
    if (start_time == 0) {
        pthread_mutex_unlock(&server_data.lock);
        return; // Chưa bắt đầu thi
    }
    
    // Tính thời gian thực tế
    time_t now = time(NULL);
    long elapsed = now - start_time;
    int duration = get_room_duration(room_id);
    long max_time = duration * 60;
    
    if (elapsed > max_time) {
        elapsed = max_time;
    }
    
    // Tính điểm
    int score, total_questions;
    calculate_score(user_id, room_id, &score, &total_questions);
    
    // Lưu kết quả với submit_reason = "disconnect"
    save_result_to_db(user_id, room_id, score, total_questions, 
                     elapsed, "disconnect");
    
    // Xóa cache
    clear_score_cache(user_id, room_id);
    
    log_activity(user_id, "AUTO_SUBMIT_DISCONNECT", "Test auto-submitted on disconnect");
    printf("[INFO] Auto-submitted (disconnect) for user %d - Score: %d/%d\n", 
           user_id, score, total_questions);
    
    pthread_mutex_unlock(&server_data.lock);
}

// View results (có thể dùng cache hoặc DB)
void view_results(int socket_fd, int room_id)
{
    pthread_mutex_lock(&server_data.lock);
    
    char response[4096] = "VIEW_RESULTS|";
    
    // Query từ database với prepared statement
    const char *sql = 
        "SELECT r.user_id, u.username, r.score, r.total_questions, r.time_taken, r.submit_reason "
        "FROM results r "
        "JOIN users u ON r.user_id = u.id "
        "WHERE r.room_id = ? "
        "ORDER BY r.score DESC, r.time_taken ASC";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, room_id);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            // int user_id = sqlite3_column_int(stmt, 0);
            const char *username = (const char *)sqlite3_column_text(stmt, 1);
            int score = sqlite3_column_int(stmt, 2);
            int total = sqlite3_column_int(stmt, 3);
            long time_taken = sqlite3_column_int64(stmt, 4);
            const char *reason = (const char *)sqlite3_column_text(stmt, 5);
            
            char line[256];
            snprintf(line, sizeof(line), "%s:%d/%d (%ldm,%s)|",
                     username ? username : "Unknown",
                     score, total, time_taken / 60, reason ? reason : "unknown");
            strcat(response, line);
        }
        sqlite3_finalize(stmt);
    } else {
        fprintf(stderr, "[ERROR] Failed to query results: %s\n", sqlite3_errmsg(db));
    }
    
    strcat(response, "\n");
    send(socket_fd, response, strlen(response), 0);
    
    pthread_mutex_unlock(&server_data.lock);
}

// Legacy function - giữ lại để backward compatible
void submit_answer(int socket_fd, int user_id, int room_id, int question_num, int answer)
{
    // Redirect to save_answer với question_id = question_num
    save_answer(socket_fd, user_id, room_id, question_num, answer);
}