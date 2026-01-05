# 🔧 FIX: Auto-submit khi hết giờ (Timeout)

## ❌ **VẤN ĐỀ TRƯỚC ĐÂY:**

### 1. **Timer CHỈ auto-submit users ONLINE**
```c
// timer.c - Logic CŨ (SAI)
if (is_online) {
    auto_submit_user(...);
} else {
    printf("User %d is offline - NOT auto-submitting\n", user_id);
}
```

**Hậu quả:**
- User disconnect → KHÔNG được submit khi hết giờ
- User login lại → Thấy room ENDED nhưng chưa có điểm
- RESUME gọi `auto_submit_on_disconnect()` → Gây xung đột logic
- Server crash hoặc data inconsistent

### 2. **Bug sử dụng sai room_id**
```c
// Dùng INDEX (i) thay vì room_id thực tế
"WHERE p.room_id = %d", i  // SAI! i là chỉ số mảng
```

**Hậu quả:**
- Query sai room → Không tìm thấy participants
- Không auto-submit được ai

### 3. **Không mark `has_taken_exam`**
- User có thể thi lại room sau khi timeout
- Logic phòng thi bị lỗi

---

## ✅ **GIẢI PHÁP ĐÃ THỰC HIỆN:**

### **Thay đổi 1: timer.c - Auto-submit TẤT CẢ users**

#### **TRƯỚC (Line 66-95):**
```c
// CHỈ auto-submit user đang online
if (is_online) {
    // Check đã submit chưa
    // Tính điểm
    // Insert results
    printf("[TIMER] Auto-submitted user %d (online)\n", user_id);
} else {
    printf("[TIMER] User %d is offline - NOT auto-submitting\n", user_id);
}
```

#### **SAU:**
```c
// AUTO-SUBMIT TẤT CẢ users chưa submit (cả online và offline)
if (!already_submitted) {
    // Tính điểm từ user_answers
    // Insert into results
    // Mark has_taken_exam = 1
    printf("[TIMER] Auto-submitted user %d in room %d - Score: %d/%d\n", 
           user_id, room->room_id, score, total_questions);
}
```

#### **Sửa bug room_id (Line 75):**
```c
// TRƯỚC: WHERE p.room_id = %d", i
// SAU:   WHERE p.room_id = %d", room->room_id
```

#### **Thêm mark has_taken_exam (Line 130-135):**
```c
char mark_taken[256];
snprintf(mark_taken, sizeof(mark_taken),
         "UPDATE room_participants SET has_taken_exam = 1 "
         "WHERE user_id = %d AND room_id = %d",
         user_id, room->room_id);
sqlite3_exec(db, mark_taken, NULL, NULL, NULL);
```

---

### **Thay đổi 2: rooms.c - RESUME xử lý timeout an toàn**

#### **TRƯỚC (Line 896-901):**
```c
if (remaining <= 0) {
    auto_submit_on_disconnect(user_id, room_id);
    send(socket_fd, "RESUME_TIME_EXPIRED\n", 20, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
}
```

#### **SAU:**
```c
if (remaining <= 0) {
    // Check lại xem timer đã auto-submit chưa
    SELECT id FROM results WHERE room_id = ? AND user_id = ?
    
    if (!was_auto_submitted) {
        // Timer chưa kịp → submit ngay
        auto_submit_on_disconnect(user_id, room_id);
        printf("[RESUME] Manual auto-submit for user %d (timer missed)\n", user_id);
    }
    
    send(socket_fd, "RESUME_TIME_EXPIRED\n", 20, 0);
    pthread_mutex_unlock(&server_data.lock);
    return;
}
```

**Lợi ích:**
- Tránh duplicate submit
- Xử lý case timer chưa kịp chạy
- Báo lỗi rõ ràng cho client

---

### **Thay đổi 3: results.c - auto_submit_on_disconnect() mark taken**

#### **TRƯỚC (Line 403-410):**
```c
sqlite3_exec(db, insert_query, NULL, NULL, &err_msg);
sqlite3_free(insert_query);

if (err_msg) {
    sqlite3_free(err_msg);
}

log_activity(user_id, "AUTO_SUBMIT", "Test auto-submitted on disconnect");
printf("[INFO] Auto-submitted for user %d\n", user_id);
```

#### **SAU:**
```c
sqlite3_exec(db, insert_query, NULL, NULL, &err_msg);
sqlite3_free(insert_query);

if (err_msg) {
    fprintf(stderr, "[AUTO_SUBMIT] Error: %s\n", err_msg);
    sqlite3_free(err_msg);
} else {
    // Mark user as taken exam
    UPDATE room_participants SET has_taken_exam = 1 
    WHERE user_id = ? AND room_id = ?
    
    printf("[AUTO_SUBMIT] User %d auto-submitted - Score: %d/%d\n", 
           user_id, score, total_questions);
}

log_activity(user_id, "AUTO_SUBMIT", "Test auto-submitted on timeout");
```

---

## 📊 **FLOW MỚI - Xử lý timeout:**

```
┌─────────────────────────────────────────────────────┐
│ User đang làm bài (online hoặc offline)             │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ Timer check: elapsed > duration                     │
│ room->room_status = 2 (ENDED)                       │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ Query tất cả participants có start_time > 0         │
│ SELECT user_id FROM participants WHERE...           │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ For EACH user:                                      │
│   ✓ Check chưa submit                               │
│   ✓ Tính điểm từ user_answers                       │
│   ✓ INSERT INTO results                             │
│   ✓ UPDATE has_taken_exam = 1                       │
│   (KHÔNG phân biệt online/offline)                  │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ Case 1: User online                                 │
│   → Nhận thông báo timeout từ server                │
│   → Chuyển sang màn hình kết quả                    │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ Case 2: User offline (đã disconnect)                │
│   → Login lại                                       │
│   → RESUME_EXAM|room_id                             │
│   → Server check: already_submitted = true          │
│   → Response: RESUME_ALREADY_SUBMITTED              │
│   → Client: Hiển thị "Exam completed"               │
└─────────────────────────────────────────────────────┘
```

---

## 🎯 **KẾT QUẢ:**

### ✅ **Trước đây (BUG):**
```
User disconnect → Không submit
Hết giờ → Chỉ submit users online
User login lại → Room ENDED, chưa có điểm → CRASH
```

### ✅ **Bây giờ (FIXED):**
```
User disconnect → Flush answers to DB
Hết giờ → Auto-submit TẤT CẢ users (cả offline)
User login lại → Check đã submit → Báo "ALREADY_SUBMITTED"
```

---

## 🧪 **TEST CASES:**

### **Test 1: User online khi hết giờ**
```
1. User đăng nhập, join room, begin exam
2. Làm 3/10 câu
3. Đợi hết giờ
4. ✅ Server auto-submit → results có điểm 3/10
5. ✅ has_taken_exam = 1
6. ✅ Không thể thi lại
```

### **Test 2: User disconnect rồi hết giờ**
```
1. User begin exam, làm 5/10 câu
2. Disconnect (network.c flush answers to DB)
3. Đợi hết giờ
4. ✅ Timer auto-submit user offline → results có 5/10
5. ✅ has_taken_exam = 1
6. Login lại, RESUME
7. ✅ Response: "RESUME_ALREADY_SUBMITTED"
8. ✅ Không crash server
```

### **Test 3: User disconnect, login trước khi hết giờ**
```
1. User begin exam, làm 2/10 câu
2. Disconnect
3. Login lại, RESUME (còn 5 phút)
4. ✅ Load được 2 câu đã làm
5. Làm thêm 3 câu (tổng 5/10)
6. Hết giờ
7. ✅ Auto-submit với điểm 5/10
```

### **Test 4: Multiple users, mixed online/offline**
```
Room có 5 users:
- User 1, 2, 3: Online (đang làm bài)
- User 4: Disconnect 2 phút trước
- User 5: Disconnect 5 phút trước

Hết giờ:
✅ Tất cả 5 users đều được auto-submit
✅ has_taken_exam = 1 cho cả 5
✅ Không ai thi lại được
```

---

## 📝 **FILES CHANGED:**

1. **server/timer.c** (Line 66-150)
   - Bỏ check `is_online`
   - Sửa `i` → `room->room_id`
   - Thêm mark `has_taken_exam = 1`
   - Improve logging

2. **server/rooms.c** (Line 896-920)
   - Thêm check `was_auto_submitted` trong RESUME
   - Tránh duplicate submit
   - Better error handling

3. **server/results.c** (Line 403-420)
   - Mark `has_taken_exam = 1` khi auto-submit
   - Improve error logging
   - Update activity log message

---

## ⚠️ **LƯU Ý:**

1. **Timer frequency**: Cần chạy `check_room_timeouts()` định kỳ (mỗi 1-5 giây)
2. **Database index**: Nên tạo index cho `(room_id, user_id)` trong `results`
3. **Race condition**: Logic đã được bảo vệ bằng `pthread_mutex_lock`
4. **Backward compatibility**: User cũ (chưa có data) vẫn hoạt động bình thường

---

**Ngày sửa:** January 5, 2026  
**Status:** ✅ RESOLVED
