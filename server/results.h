#ifndef RESULTS_H
#define RESULTS_H

#include "common.h"

// ============================================================================
// ANSWER & SUBMISSION FUNCTIONS
// ============================================================================

/**
 * Lưu câu trả lời của user realtime vào database
 * - Validate answer range [0-3] (A-D)
 * - Validate question_id thuộc room
 * - Chặn nếu đã submit test
 * - Chặn nếu chưa bắt đầu thi
 * - Chặn nếu đã hết giờ (timeout)
 * - UPDATE cache score cho leaderboard realtime
 * - Response: SAVE_ANSWER_OK hoặc SAVE_ANSWER_FAIL|reason
 */
void save_answer(int socket_fd, int user_id, int room_id, int question_id, int selected_answer);

/**
 * Submit bài thi thủ công (user nhấn nút Submit)
 * - CHẶN DOUBLE SUBMIT (kiểm tra đầu tiên)
 * - Validate user đã bắt đầu thi
 * - Tính điểm từ user_answers
 * - Cap time_taken ở max duration nếu vượt quá
 * - Lưu kết quả với submit_reason = "manual"
 * - XÓA cache sau khi submit thành công
 * - Response: SUBMIT_TEST_OK|score|total|time_minutes hoặc SUBMIT_TEST_FAIL|reason
 */
void submit_test(int socket_fd, int user_id, int room_id);

/**
 * Auto-submit khi hết thời gian làm bài
 * - CHẶN DOUBLE SUBMIT (nếu đã submit rồi thì skip)
 * - Validate user đã bắt đầu thi
 * - Time_taken = duration (thời gian tối đa)
 * - Tính điểm từ các câu trả lời đã lưu
 * - Lưu kết quả với submit_reason = "timeout"
 * - XÓA cache
 * - Gọi từ timer thread khi room hết giờ
 */
void auto_submit_on_timeout(int user_id, int room_id);

/**
 * Auto-submit khi user disconnect đột ngột
 * - CHẶN DOUBLE SUBMIT (nếu đã submit rồi thì skip)
 * - Validate user đã bắt đầu thi
 * - Time_taken = thời gian thực tế (elapsed time)
 * - Tính điểm từ các câu trả lời đã lưu
 * - Lưu kết quả với submit_reason = "disconnect"
 * - XÓA cache
 * - Gọi từ handle_client khi detect disconnect
 */
void auto_submit_on_disconnect(int user_id, int room_id);

/**
 * Xem kết quả thi của room (leaderboard)
 * - Query từ results table (đã submit)
 * - JOIN với users để lấy username
 * - Sắp xếp theo score DESC, time_taken ASC
 * - Format: VIEW_RESULTS|username:score/total (time,reason)|...
 */
void view_results(int socket_fd, int room_id);

/**
 * Legacy function - backward compatible
 * Redirect sang save_answer() với question_id = question_num
 * @deprecated Sử dụng save_answer() thay thế
 */
void submit_answer(int socket_fd, int user_id, int room_id, int question_num, int answer);

// ============================================================================
// CACHE MANAGEMENT - Realtime Score cho Leaderboard
// ============================================================================

/**
 * Update score cache khi user trả lời câu hỏi
 * - Tính score realtime từ user_answers + questions
 * - Lưu vào in-memory cache để query nhanh
 * - Tự động được gọi từ save_answer()
 * - Dùng cho leaderboard realtime (chưa submit)
 */
void update_score_cache(int user_id, int room_id);

/**
 * Lấy score từ cache cho leaderboard realtime
 * @param user_id ID của user
 * @param room_id ID của room
 * @param score Output pointer - điểm số hiện tại
 * @param total_questions Output pointer - tổng số câu hỏi
 * @return 1 nếu tìm thấy trong cache, 0 nếu không tìm thấy
 * 
 * Sử dụng:
 *   int score, total;
 *   if (get_cached_score(user_id, room_id, &score, &total)) {
 *       printf("Current score: %d/%d\n", score, total);
 *   }
 */
int get_cached_score(int user_id, int room_id, int *score, int *total_questions);

/**
 * Xóa cache entry khi user submit test
 * - Tránh stale data
 * - Tự động được gọi từ submit_test(), auto_submit_*()
 * - Sau khi xóa, dữ liệu chính thức nằm trong results table
 */
void clear_score_cache(int user_id, int room_id);

// ============================================================================
// NOTES
// ============================================================================
/**
 * FLOW CHẶN DOUBLE SUBMIT:
 * 1. User login lần 1 → bắt đầu thi → trả lời câu hỏi
 * 2. User login lần 2 (cùng lúc) → bắt đầu thi → trả lời câu hỏi
 * 3. Session 1 submit → Lưu vào results → OK
 * 4. Session 2 submit → Check has_user_submitted() → CHẶN
 * 5. Session 1 disconnect → auto_submit_on_disconnect() → Check → ĐÃ SUBMIT → Skip
 * 6. Timeout trigger → auto_submit_on_timeout() → Check → ĐÃ SUBMIT → Skip
 * 
 * SUBMIT_REASON:
 * - "manual"     : User nhấn nút Submit
 * - "timeout"    : Hết giờ, tự động submit
 * - "disconnect" : User ngắt kết nối đột ngột
 * 
 * CACHE USAGE:
 * - Cache dùng cho leaderboard REALTIME (user chưa submit)
 * - Sau khi submit, data chính thức ở results table
 * - Cache tự động clear khi submit
 */

#endif // RESULTS_H