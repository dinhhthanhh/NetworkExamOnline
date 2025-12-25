#ifndef STATS_H
#define STATS_H

#include "common.h"

// ============================================================================
// LEADERBOARD FUNCTIONS
// ============================================================================

/**
 * Get global leaderboard (tất cả users)
 * - Tính tổng điểm từ tất cả bài thi
 * - Sắp xếp theo total_score DESC
 * - Response: LEADERBOARD|#rank|username|Score:X|Tests:Y|...
 * 
 * @param socket_fd Socket để gửi response
 * @param limit Số lượng top users (1-100, default 10)
 */
void get_leaderboard(int socket_fd, int limit);

/**
 * Get room-specific leaderboard
 * - Chỉ lấy kết quả từ một room cụ thể
 * - Sắp xếp theo score DESC, time_taken ASC
 * - Hiển thị submit_reason (manual/timeout/disconnect)
 * - Response: ROOM_LEADERBOARD|RoomID:X|#rank|username|score/total|time|reason|...
 * 
 * @param socket_fd Socket để gửi response
 * @param room_id ID của room cần xem leaderboard
 * @param limit Số lượng top users (1-100, default 20)
 */
void get_room_leaderboard(int socket_fd, int room_id, int limit);

// ============================================================================
// USER STATISTICS
// ============================================================================

/**
 * Get overall user statistics
 * - Tổng số bài thi đã hoàn thành
 * - Điểm trung bình (%)
 * - Điểm cao nhất trong một bài
 * - Tổng điểm tích lũy
 * - Tổng thời gian làm bài
 * - Response: USER_STATS|Tests:X|AvgScore:X.XX%|MaxScore:X|TotalScore:X|TotalTime:Xm
 * 
 * @param socket_fd Socket để gửi response
 * @param user_id ID của user cần xem stats
 */
void get_user_statistics(int socket_fd, int user_id);

/**
 * Get user performance by category
 * - Thống kê theo từng category (Math, Science, History, etc.)
 * - Số lượng bài thi và độ chính xác trung bình
 * - Response: CATEGORY_STATS|Category:tests:accuracy%|...
 * 
 * NOTE: Query đã được FIX - JOIN đúng qua user_answers để lấy category
 * 
 * @param socket_fd Socket để gửi response
 * @param user_id ID của user cần xem stats
 */
void get_category_stats(int socket_fd, int user_id);

/**
 * Get user performance by difficulty
 * - Thống kê theo từng difficulty level (Easy, Medium, Hard)
 * - Số lượng bài thi và độ chính xác trung bình
 * - Response: DIFFICULTY_STATS|Difficulty:tests:accuracy%|...
 * 
 * NOTE: Query đã được FIX - JOIN đúng qua user_answers
 * 
 * @param socket_fd Socket để gửi response
 * @param user_id ID của user cần xem stats
 */
void get_difficulty_stats(int socket_fd, int user_id);

/**
 * Get user test history
 * - 20 bài thi gần nhất
 * - Hiển thị room name, score, time, submit_reason, timestamp
 * - Sắp xếp theo submitted_at DESC
 * - Response: TEST_HISTORY|result_id|room_name|score|total|time|reason|timestamp|...
 * 
 * @param socket_fd Socket để gửi response
 * @param user_id ID của user cần xem history
 */
void get_user_test_history(int socket_fd, int user_id);

/**
 * Get submit reason statistics
 * - Đếm số lần submit theo từng loại (manual/timeout/disconnect)
 * - Giúp phân tích user behavior và connection stability
 * - Response: SUBMIT_REASON_STATS|reason:count|...
 * 
 * Use cases:
 * - Phát hiện user hay bị disconnect
 * - Kiểm tra user có hay timeout không
 * - So sánh manual submit vs auto submit
 * 
 * @param socket_fd Socket để gửi response
 * @param user_id ID của user cần xem stats
 */
void get_submit_reason_stats(int socket_fd, int user_id);

// ============================================================================
// NOTES
// ============================================================================
/**
 * IMPROVEMENTS IN THIS VERSION:
 * 
 * 1. PREPARED STATEMENTS:
 *    - Tất cả queries dùng sqlite3_bind_* thay vì snprintf
 *    - An toàn hơn, tránh SQL injection
 * 
 * 2. FIXED QUERIES:
 *    - get_category_stats: JOIN đúng qua user_answers
 *    - get_difficulty_stats: JOIN đúng qua user_answers
 *    - get_user_test_history: Dùng submitted_at thay vì completed_at
 * 
 * 3. NEW FEATURES:
 *    - get_room_leaderboard: Leaderboard riêng cho từng room
 *    - get_submit_reason_stats: Phân tích submit behavior
 *    - Hiển thị submit_reason trong các responses
 * 
 * 4. SAFETY:
 *    - Buffer overflow protection với append_safe()
 *    - NULL pointer checks cho sqlite3_column_text()
 *    - Validate input limits
 * 
 * 5. BETTER RESPONSES:
 *    - Format rõ ràng hơn
 *    - Thêm error responses (_FAIL)
 *    - Hiển thị time_taken trong minutes
 */

#endif // STATS_H