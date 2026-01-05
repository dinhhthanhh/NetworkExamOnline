# 🔧 FIX: Timeout không auto-submit và crash khi rejoin

## ❌ **VẤN ĐỀ:**

### 1. **Hết giờ KHÔNG auto-submit**
- Timer thread **KHÔNG được khởi động** trong server
- User hết giờ → Không có kết quả
- JOIN lại → Server crash với lỗi:
  ```
  Cannot receive: Resource temporarily unavailable
  ```

### 2. **Room status không đồng bộ với DB**
- `handle_begin_exam()` load room từ DB nhưng:
  - `room_status` = 0 (WAITING) mặc định
  - `exam_start_time` = 0 mặc định
- → Không detect được room đã START hoặc ENDED

### 3. **Đáp án bị mất khi disconnect**
- `flush_user_answers()` chỉ được gọi khi disconnect
- Nhưng room có thể đã bị remove khỏi in-memory
- → Không flush được → Mất data

---

## ✅ **GIẢI PHÁP:**

### **Fix 1: Khởi động Timer Thread tự động**

#### **File: quiz_server.c**

**THÊM timer thread:**
```c
void *timer_thread(void *arg) {
    printf("[TIMER] Timer thread started\n");
    
    while (1) {
        sleep(5);  // Check mỗi 5 giây
        check_room_timeouts();
    }
    
    return NULL;
}
```

**Khởi động trong main():**
```c
printf("Server started on port %d\n", PORT);

// Khởi động timer thread
pthread_t timer_tid;
if (pthread_create(&timer_tid, NULL, timer_thread, NULL) != 0) {
    perror("Failed to create timer thread");
} else {
    pthread_detach(timer_tid);
    printf("[TIMER] Timer thread launched\n");
}
```

---

### **Fix 2: Load room_status và exam_start_time từ DB**

#### **File: rooms.c - handle_begin_exam()**

**TRƯỚC:**
```c
snprintf(room_info_query, sizeof(room_info_query),
         "SELECT name, host_id, duration FROM rooms WHERE id = %d", room_id);
// ...
server_data.rooms[room_idx].room_status = 0;  // WAITING by default
server_data.rooms[room_idx].exam_start_time = 0;
```

**SAU:**
```c
snprintf(room_info_query, sizeof(room_info_query),
         "SELECT name, host_id, duration, room_status, exam_start_time 
          FROM rooms WHERE id = %d", room_id);
// ...
server_data.rooms[room_idx].room_status = sqlite3_column_int(room_stmt, 3);
server_data.rooms[room_idx].exam_start_time = sqlite3_column_int64(room_stmt, 4);
```

---

### **Fix 3: Check timeout trong BEGIN_EXAM**

**THÊM logic check timeout:**
```c
// Case 2: ENDED - Đã kết thúc
if (room->room_status == 2) {
    // Check đã submit chưa
    if (already_submitted) {
        send("ERROR|Exam has ended and you have submitted\n");
    } else {
        send("ERROR|Exam has ended\n");
    }
    return;
}

// Case 2.5: STARTED nhưng đã hết giờ
if (room->room_status == 1) {
    time_t now = time(NULL);
    long elapsed = now - room->exam_start_time;
    long duration_seconds = room->time_limit * 60;
    
    if (elapsed >= duration_seconds) {
        // Hết giờ - update status
        room->room_status = 2;
        UPDATE rooms SET room_status = 2 WHERE id = ?
        
        send("ERROR|Exam time expired\n");
        return;
    }
}
```

---

### **Fix 4: Auto-submit trong RESUME khi timeout**

**THÊM auto-submit khi detect timeout:**
```c
if (remaining <= 0) {
    // Check đã submit chưa
    if (!was_auto_submitted) {
        // Unlock trước khi auto-submit (tránh deadlock)
        pthread_mutex_unlock(&server_data.lock);
        auto_submit_on_disconnect(user_id, room_id);
        pthread_mutex_lock(&server_data.lock);
    }
    
    // Update room status
    room->room_status = 2;
    UPDATE rooms SET room_status = 2 WHERE id = ?
    
    send("RESUME_TIME_EXPIRED\n");
    return;
}
```

---

### **Fix 5: Lưu exam_start_time vào DB khi START**

#### **File: rooms.c - start_test()**

**TRƯỚC:**
```c
UPDATE rooms SET room_status = 1 WHERE id = ?
```

**SAU:**
```c
UPDATE rooms SET room_status = 1, exam_start_time = %ld WHERE id = ?
```

---

### **Fix 6: Realtime save đáp án (không đợi mỗi 5 câu)**

#### **File: results.c - save_answer()**

**TRƯỚC:**
```c
// AUTO-SAVE mỗi 5 câu
if (answered_count % 5 == 0 || answered_count == room->num_questions) {
    flush_answers_to_db(user_id, room_id, room, user_idx);
}
```

**SAU:**
```c
// LUÔN FLUSH VÀO DB NGAY (tránh mất data)
flush_answers_to_db(user_id, room_id, room, user_idx);
```

---

### **Fix 7: Simplify flush_user_answers() (đã save realtime)**

**SAU khi save realtime, flush_user_answers() không cần làm gì:**
```c
void flush_user_answers(int user_id, int room_id) {
    pthread_mutex_lock(&server_data.lock);
    
    // Đáp án đã được lưu vào DB realtime - không cần action
    printf("[FLUSH] Answers already saved to DB in realtime\n");
    
    pthread_mutex_unlock(&server_data.lock);
}
```

---

### **Fix 8: Tránh deadlock trong timer**

#### **File: timer.c - check_room_timeouts()**

**TRƯỚC:**
```c
pthread_mutex_lock(&server_data.lock);

for (rooms) {
    // Query DB trong lock → Deadlock
    SELECT ... FROM participants
    SELECT ... FROM results
    // ...
}

pthread_mutex_unlock(&server_data.lock);
```

**SAU:**
```c
// PHASE 1: Copy data nhanh trong lock
pthread_mutex_lock(&server_data.lock);
for (rooms) {
    rooms_to_check[i] = copy_room_data();
}
pthread_mutex_unlock(&server_data.lock);

// PHASE 2: Query DB NGOÀI lock (an toàn)
for (rooms_to_check) {
    SELECT ... FROM participants
    SELECT ... FROM results
    INSERT INTO results
    // ...
}
```

---

## 📊 **FLOW MỚI:**

```
┌─────────────────────────────────────────────────────┐
│ SERVER STARTUP                                      │
│   ✓ Init DB                                         │
│   ✓ Load users from DB                              │
│   ✓ Load rooms from DB (với room_status + start_time)│
│   ✓ START TIMER THREAD (check mỗi 5s)              │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ HOST START EXAM                                     │
│   → room_status = 1 (STARTED)                       │
│   → exam_start_time = now                           │
│   → UPDATE DB (cả 2 fields)                         │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ USER BEGIN/RESUME EXAM                              │
│   1. Load room từ DB (status + start_time)          │
│   2. Check timeout: elapsed >= duration?            │
│   3. Nếu timeout:                                   │
│      → Auto-submit (nếu chưa)                       │
│      → Update status = ENDED                        │
│      → Return "TIME_EXPIRED"                        │
│   4. Nếu OK: Load questions + saved answers         │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ USER SAVE ANSWER                                    │
│   → Lưu vào in-memory                               │
│   → FLUSH NGAY VÀO DB (realtime)                    │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ TIMER THREAD (mỗi 5 giây)                           │
│   1. Copy room data trong lock (nhanh)              │
│   2. Release lock                                   │
│   3. Query DB check timeout (an toàn)               │
│   4. Nếu timeout:                                   │
│      → Auto-submit TẤT CẢ users chưa submit         │
│      → Update room_status = ENDED                   │
│      → Mark has_taken_exam = 1                      │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ USER DISCONNECT                                     │
│   → flush_user_answers() (no-op, đã save realtime)  │
│   → logout_user() (is_online = 0)                   │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│ USER RE-LOGIN + RESUME                              │
│   1. Check room_status + exam_start_time từ DB      │
│   2. Nếu ENDED hoặc timeout:                        │
│      → Check đã submit → Báo "ALREADY_SUBMITTED"    │
│      → Chưa submit → Auto-submit → "TIME_EXPIRED"   │
│   3. Nếu còn thời gian:                             │
│      → Load questions + saved answers từ DB         │
│      → Tiếp tục làm bài                             │
└─────────────────────────────────────────────────────┘
```

---

## 🧪 **TEST SCENARIOS:**

### **Test 1: Timer auto-submit (normal)**
```
1. Host start exam → timer = 30 phút
2. User 1,2,3 begin exam, làm bài
3. Đợi 30 phút
4. ✅ Timer thread detect timeout
5. ✅ Auto-submit cả 3 users
6. ✅ room_status = ENDED
7. ✅ has_taken_exam = 1 cho cả 3
8. User 4 muốn BEGIN_EXAM
9. ✅ Response: "ERROR|Exam has ended"
```

### **Test 2: User disconnect → timeout → rejoin**
```
1. User begin exam, làm 3/10 câu
2. Disconnect (answers saved to DB realtime)
3. Đợi đến hết giờ
4. ✅ Timer auto-submit user offline → results có 3/10
5. Login lại, RESUME_EXAM
6. ✅ Response: "RESUME_ALREADY_SUBMITTED"
7. ✅ Không crash server
```

### **Test 3: Timeout trước khi timer chạy**
```
1. User begin exam (còn 2 phút)
2. Disconnect
3. Timer chưa kịp chạy (sleep 5s)
4. Login lại, RESUME_EXAM (đã quá giờ)
5. ✅ RESUME detect timeout
6. ✅ Auto-submit ngay trong RESUME
7. ✅ Response: "RESUME_TIME_EXPIRED"
8. ✅ Có kết quả trong DB
```

### **Test 4: BEGIN_EXAM khi room đã ENDED**
```
1. Room 1 đã hết giờ, status = ENDED
2. User mới BEGIN_EXAM|1
3. ✅ Load room với status = 2 từ DB
4. ✅ Detect ENDED
5. ✅ Response: "ERROR|Exam has ended"
6. ✅ Không crash
```

---

## 📝 **FILES CHANGED:**

### 1. **server/quiz_server.c**
- ✅ Thêm `timer_thread()` function
- ✅ Start timer thread trong `main()`
- ✅ Include `timer.h`

### 2. **server/rooms.c**
- ✅ Load `room_status` + `exam_start_time` từ DB trong `handle_begin_exam()`
- ✅ Check timeout trong `handle_begin_exam()` (case ENDED + case timeout)
- ✅ Auto-submit trong `handle_resume_exam()` khi timeout
- ✅ Update DB với `exam_start_time` trong `start_test()`

### 3. **server/results.c**
- ✅ Realtime save: Gọi `flush_answers_to_db()` ngay trong `save_answer()`
- ✅ Simplify `flush_user_answers()` (no-op vì đã realtime)

### 4. **server/timer.c**
- ✅ Refactor `check_room_timeouts()` với 2-phase locking
- ✅ Copy data trong lock → Query DB ngoài lock
- ✅ Tránh deadlock

---

## ⚠️ **LƯU Ý:**

### **Performance:**
- Realtime save có thể chậm hơn batch save
- Nhưng đảm bảo data integrity 100%
- Nếu cần optimize: Dùng WAL mode cho SQLite

### **Timer frequency:**
- Hiện tại: 5 giây
- Có thể điều chỉnh: 1-10 giây tùy load

### **Database schema:**
- `exam_start_time` column đã có sẵn trong table `rooms`
- Không cần migration

---

**Ngày sửa:** January 5, 2026  
**Status:** ✅ RESOLVED - Timer tự động, timeout được xử lý đúng
