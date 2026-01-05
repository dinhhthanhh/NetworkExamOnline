# FIX: Auto-Submit Khi Hết Giờ - Critical Bugs

## Vấn Đề Gặp Phải

### 1. Hệ thống KHÔNG tự động submit khi user disconnect và hết giờ
```
User disconnect → Timer hết → User vào lại → BỊ TREO
```

### 2. Lỗi "Resource temporarily unavailable" 
```
Send: RESUME_EXAM|1
Cannot receive: Receive failed: Resource temporarily unavailable
```

---

## Nguyên Nhân

### 1. **Server KHÔNG có background timer thread**
- `check_room_timeouts()` chỉ chạy khi có command `CHECK_TIMERS`
- Timer KHÔNG tự động chạy định kỳ
- User disconnect → Hết giờ → KHÔNG auto-submit → Vào lại bị treo

### 2. **Lỗi syntax trong timer.c**
```c
void check_room_timeouts(void)
{{  // ← Lỗi: 2 dấu ngoặc mở
```

### 3. **Socket non-blocking từ broadcast listener**
- Broadcast listener set socket → non-blocking mỗi 200ms
- Khi gửi RESUME_EXAM, socket vẫn non-blocking
- `recv()` trả về EAGAIN → "Resource temporarily unavailable"

---

## Giải Pháp Đã Áp Dụng

### 1. ✅ Sửa lỗi syntax trong timer.c

**File: `server/timer.c`**
```c
// BEFORE:
void check_room_timeouts(void)
{{  // ❌ Lỗi syntax

// AFTER:
void check_room_timeouts(void)
{   // ✅ Đúng
```

### 2. ✅ Tạo background timer thread

**File: `server/quiz_server.c`**

**Thêm timer thread function:**
```c
// Background timer thread
void *timer_thread(void *arg) {
    (void)arg;
    printf("[TIMER] Background timer thread started\n");
    
    while (1) {
        sleep(5); // Check every 5 seconds
        check_room_timeouts();
    }
    
    return NULL;
}
```

**Khởi động timer thread khi server start:**
```c
int main() {
    // ... init code ...
    
    printf("Server started on port %d\n", PORT);
    
    // Start background timer thread
    pthread_t timer_tid;
    if (pthread_create(&timer_tid, NULL, timer_thread, NULL) != 0) {
        perror("Failed to create timer thread");
    } else {
        pthread_detach(timer_tid);
        printf("[TIMER] Background timer thread started\n");
    }
    
    while (1) {
        // Accept connections...
    }
}
```

### 3. ✅ Đảm bảo socket blocking mode

**File: `client/net.c`**

**Thêm check và restore trong receive_message():**
```c
ssize_t receive_message(char *buffer, size_t bufsz) {
    if (client.socket_fd <= 0) {
        return -1;
    }
    
    // Đảm bảo socket ở blocking mode
    int flags = fcntl(client.socket_fd, F_GETFL, 0);
    if (flags >= 0 && (flags & O_NONBLOCK)) {
        if (net_debug_enabled) {
            printf("[NET] Socket was non-blocking, restoring to blocking mode\n");
        }
        fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    
    // Bây giờ recv() sẽ block đợi response
    ssize_t n = recv(client.socket_fd, buffer, bufsz - 1, 0);
    ...
}
```

### 4. ✅ Đảm bảo broadcast listener luôn restore blocking

**File: `client/broadcast.c`**
```c
static gboolean poll_broadcasts(gpointer user_data) {
    // ...
    
    // ALWAYS restore blocking mode before returning
    set_blocking(client.socket_fd);
    
    return TRUE; // Continue timer
}
```

---

## Flow Hoạt Động Sau Khi Fix

### Khi user disconnect và hết giờ:

```
┌─────────────────────────────────────────────────────────────┐
│ 1. User đang thi, còn 10 phút                              │
│ 2. User disconnect (mất mạng, đóng app)                    │
│ 3. Server lưu answers vào DB                                │
│                                                              │
│ 4. 15 phút sau: Background timer thread phát hiện hết giờ  │
│    ├─ check_room_timeouts() chạy mỗi 5 giây               │
│    ├─ remaining = duration - elapsed < 0                   │
│    └─ Auto-submit TẤT CẢ users (cả offline)               │
│        ├─ Tính điểm từ user_answers                        │
│        └─ INSERT INTO results                               │
│                                                              │
│ 5. User vào lại và JOIN_ROOM|1                            │
│ 6. User gửi RESUME_EXAM|1                                  │
│    ├─ receive_message() restore blocking mode              │
│    └─ Socket đúng state, không bị EAGAIN                   │
│                                                              │
│ 7. Server check results → Đã submit                       │
│ 8. Server gửi: RESUME_ALREADY_SUBMITTED                   │
│ 9. Client hiển thị: "You have already completed this exam"│
│                                                              │
│ ✅ KHÔNG bị treo, KHÔNG bị lỗi                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Các Thay Đổi Chi Tiết

### server/timer.c
- ✅ Sửa lỗi syntax `{{` → `{`
- ✅ Logic auto-submit TẤT CẢ users khi hết giờ (đã có từ trước)

### server/quiz_server.c
- ✅ Thêm `#include "timer.h"`
- ✅ Thêm `timer_thread()` function
- ✅ Khởi động timer thread khi server start

### client/net.c
- ✅ Thêm `#include <fcntl.h>`
- ✅ Thêm check và restore blocking mode trong `receive_message()`
- ✅ Thêm debug log khi restore blocking mode

### client/broadcast.c
- ✅ Comment rõ ràng hơn về restore blocking mode

---

## Compile và Test

### 1. Compile Server
```bash
cd server
make clean
make
```

### 2. Compile Client
```bash
cd client
make clean
make
```

### 3. Test Scenario

**Terminal 1: Chạy server**
```bash
cd server
./quiz_server
# Quan sát log: [TIMER] Background timer thread started
```

**Terminal 2: Admin tạo và open room**
```
1. Login admin
2. Create room với duration 1 phút
3. Open room
```

**Terminal 3: User thi và disconnect**
```
1. Login user
2. Join room và bắt đầu thi
3. Trả lời vài câu
4. Đóng client (disconnect)
5. Đợi > 1 phút
6. Quan sát server log: [TIMER] Auto-submitted user X when time expired
```

**Terminal 3: User vào lại**
```
1. Chạy client lại
2. Login user
3. Click vào room đã thi
4. Quan sát:
   - Receive: JOIN_ROOM_OK  ✅
   - Send: RESUME_EXAM|X    ✅
   - Receive: RESUME_ALREADY_SUBMITTED  ✅
   - Dialog: "You have already completed this exam"  ✅
```

---

## Debugging Tips

### Kiểm tra timer thread có chạy không:
```bash
# Trong server log, tìm:
[TIMER] Background timer thread started
[TIMER] Auto-submitted user X when time expired
```

### Kiểm tra socket mode:
```bash
# Trong client log, tìm:
[NET] Socket was non-blocking, restoring to blocking mode
```

### Kiểm tra broadcast listener:
```bash
# Nếu broadcast vẫn hoạt động, tìm:
[BROADCAST] Listener started (polling every 200ms with non-blocking I/O)
[BROADCAST] Received: ROOM_STARTED|...
```

---

## Kết Luận

✅ **Timer tự động chạy** mỗi 5 giây để check timeout  
✅ **Auto-submit TẤT CẢ users** khi hết giờ (cả offline)  
✅ **Socket luôn ở blocking mode** khi gọi receive_message()  
✅ **Không còn lỗi EAGAIN** ("Resource temporarily unavailable")  
✅ **User có thể vào lại sau khi hết giờ** và nhận thông báo đã hoàn thành  

**Trade-offs:**
- ⚠️ Timer check mỗi 5 giây → Có thể chậm tối đa 5 giây khi auto-submit
- ⚠️ User offline không thể resume sau khi hết giờ (nhưng đây là hành vi đúng)
