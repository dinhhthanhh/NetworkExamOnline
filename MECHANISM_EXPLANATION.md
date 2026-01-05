# CƠ CHẾ HOẠT ĐỘNG CỦA HỆ THỐNG THI TRỰC TUYẾN

## 1. KIẾN TRÚC TỔNG QUAN

Hệ thống sử dụng mô hình **Client-Server** với kết nối TCP socket:
- **Server**: Xử lý logic nghiệp vụ, quản lý database, và broadcast thông báo
- **Client**: GTK UI, xử lý tương tác người dùng và nhận broadcast

---

## 2. CƠ CHẾ NON-BLOCKING POLLING (200ms)

### 2.1. Vấn đề cần giải quyết

Trong hệ thống thi trực tuyến, Client cần:
1. **Gửi request thông thường** (join room, submit answer) và đợi response
2. **Nhận broadcast real-time** từ server (room started, time update) mà KHÔNG làm gián đoạn request-response

Nếu dùng **blocking I/O thuần túy**, client sẽ bị "đóng băng" khi đợi response, không thể nhận broadcast.

### 2.2. Giải pháp: Non-blocking Polling với GTK Timer

Client sử dụng **non-blocking socket** + **GTK timer** (200ms) để giải quyết:

```c
// File: client/broadcast.c

// GTK timeout callback - chạy mỗi 200ms
static gboolean poll_broadcasts(gpointer user_data) {
    // Bước 1: Tạm thời chuyển socket sang non-blocking mode
    set_nonblocking(client.socket_fd);
    
    // Bước 2: PEEK (xem trước) data mà KHÔNG remove khỏi buffer
    ssize_t n = recv(client.socket_fd, buffer, sizeof(buffer) - 1, 
                     MSG_PEEK | MSG_DONTWAIT);
    
    if (n > 0) {
        // Bước 3: Kiểm tra xem có phải broadcast message không?
        if (is_broadcast_message(buffer)) {
            // Là broadcast -> đọc và xử lý ngay
            n = recv(client.socket_fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
            handle_broadcast_message(buffer);
        }
        // Nếu KHÔNG phải broadcast -> GIỮ NGUYÊN trong buffer
        // để code request-response đọc sau
    }
    
    // Bước 4: Khôi phục lại blocking mode
    set_blocking(client.socket_fd);
    
    return TRUE; // Tiếp tục timer
}
```

### 2.3. Phân biệt Broadcast vs Response

**Broadcast messages** có format đặc biệt:
- `ROOM_STARTED|room_id|start_time`
- `ROOM_CREATED|room_id|room_name|duration`

**Response messages** (từ request-response):
- `BEGIN_EXAM_OK|remaining_seconds|questions...`
- `SUBMIT_TEST_OK|score|total|time`
- `LIST_ROOMS_OK|count|...`

Hàm `is_broadcast_message()` kiểm tra prefix để phân biệt:
```c
static int is_broadcast_message(const char *message) {
    return (strncmp(message, "ROOM_STARTED", 12) == 0 ||
            strncmp(message, "ROOM_CREATED", 12) == 0);
}
```

---

## 3. FLOW HOẠT ĐỘNG CỦA CƠ CHẾ THI

### 3.1. Admin Mở Room (START_ROOM)

```
┌─────────┐                                    ┌─────────┐
│ Admin   │                                    │ Server  │
└────┬────┘                                    └────┬────┘
     │                                              │
     │ 1. START_ROOM|room_id                       │
     │─────────────────────────────────────────────>│
     │                                              │
     │                                              │ 2. Cập nhật DB:
     │                                              │    - room_status = 1 (STARTED)
     │                                              │    - exam_start_time = now
     │                                              │
     │                                              │ 3. Broadcast đến ALL users
     │                                              │    trong room:
     │                                              │    ROOM_STARTED|room_id|start_time
     │                                              │
     │ 4. START_ROOM_OK                             │
     │<─────────────────────────────────────────────│
     │                                              │
```

### 3.2. User Tham Gia Thi (JOIN_ROOM → BEGIN_EXAM)

```
┌─────────┐                                    ┌─────────┐
│ User    │                                    │ Server  │
└────┬────┘                                    └────┬────┘
     │                                              │
     │ 1. JOIN_ROOM|room_id                         │
     │─────────────────────────────────────────────>│
     │                                              │
     │                                              │ 2. Kiểm tra:
     │                                              │    - User có trong room chưa?
     │                                              │    - Room đã started chưa?
     │                                              │
     │ 3. JOIN_ROOM_OK                              │
     │<─────────────────────────────────────────────│
     │                                              │
     │ 4. BEGIN_EXAM|room_id                        │
     │─────────────────────────────────────────────>│
     │                                              │
     │                                              │ 5. Server:
     │                                              │    - Lấy questions từ DB
     │                                              │    - Tạo participant entry
     │                                              │    - start_time = now
     │                                              │
     │ 6. BEGIN_EXAM_OK|remaining_seconds|questions │
     │<─────────────────────────────────────────────│
     │                                              │
     │ 7. Client hiển thị UI thi                   │
     │    + Timer đếm ngược                         │
     │    + Questions với radio buttons             │
     │                                              │
```

### 3.3. Broadcast Listener (GTK Timer - 200ms)

```
┌─────────────┐                              ┌─────────┐
│ GTK Timer   │                              │ Socket  │
│  (200ms)    │                              │ Buffer  │
└──────┬──────┘                              └────┬────┘
       │                                          │
       │ Every 200ms:                             │
       │                                          │
       │ 1. Set socket -> non-blocking            │
       │──────────────────────────────────────────>│
       │                                          │
       │ 2. PEEK data (không remove)             │
       │──────────────────────────────────────────>│
       │                                          │
       │ 3. Data available?                       │
       │<──────────────────────────────────────────│
       │                                          │
       │ 4. Check: is_broadcast?                  │
       │    YES: Read + handle broadcast          │
       │    NO:  Leave in buffer (cho code khác)  │
       │                                          │
       │ 5. Set socket -> blocking                │
       │──────────────────────────────────────────>│
       │                                          │
       │ 6. Return TRUE (continue timer)          │
       │                                          │
```

**Khi có broadcast:**
```
Socket Buffer: "ROOM_STARTED|8|1735905602\n"

200ms check:
  ├─ PEEK: "ROOM_STARTED|8|1735905602\n"
  ├─ is_broadcast? → YES
  ├─ READ (remove): "ROOM_STARTED|8|1735905602\n"
  ├─ Parse: room_id=8, start_time=1735905602
  └─ Callback: on_room_started_broadcast(8, 1735905602)
      └─ GTK UI: Hiển thị popup "Room 8 has started!"
```

**Khi có response (không phải broadcast):**
```
Socket Buffer: "BEGIN_EXAM_OK|1800|questions...\n"

200ms check:
  ├─ PEEK: "BEGIN_EXAM_OK|1800|questions...\n"
  ├─ is_broadcast? → NO
  └─ KHÔNG đọc, để nguyên trong buffer

Code request-response:
  ├─ send_message("BEGIN_EXAM|8\n")
  ├─ receive_message(buffer) → đọc "BEGIN_EXAM_OK|..."
  └─ Parse và xử lý
```

---

## 4. XỬ LÝ REQUEST-RESPONSE BÌNH THƯỜNG

Các request thông thường (không phải broadcast) vẫn hoạt động blocking như thường:

```c
// User click "Submit Answer"
send_message("SAVE_ANSWER|room_id|question_id|answer\n");
char buffer[1024];
receive_message(buffer, sizeof(buffer));  // BLOCK đợi response
// -> Nhận: "SAVE_ANSWER_OK\n"
```

**Tại sao không conflict với broadcast polling?**
- Broadcast polling chỉ **PEEK** (xem trước), không remove data
- Chỉ remove khi **chắc chắn** là broadcast
- Request-response vẫn đọc được data vì nó còn nguyên trong buffer

---

## 5. DISCONNECT VÀ RESUME MECHANISM

### 5.1. Khi User Bị Disconnect

```
┌─────────┐                                    ┌─────────┐
│ User    │                                    │ Server  │
└────┬────┘                                    └────┬────┘
     │                                              │
     │ Đang thi...                                 │
     │                                              │
     │ ❌ DISCONNECT (mất mạng, đóng app...)       │
     X                                              │
                                                    │
                                         1. Server detect disconnect
                                         │  (recv/send return error)
                                         │
                                         2. Lưu tất cả answers vào DB:
                                         │  - user_answers table
                                         │  - participant.start_time giữ nguyên
                                         │
                                         3. KHÔNG auto-submit
                                         │  -> Để user resume sau
```

**Code Server:**
```c
// File: server/network.c
if (n <= 0) {
    // Detect disconnect - LƯU TẤT CẢ ANSWERS VÀO DB
    flush_user_answers_to_db(user_id, current_room_id);
    
    // KHÔNG auto-submit để user có thể resume sau
    // auto_submit_on_disconnect(user_id, current_room_id); // DISABLED
}
```

### 5.2. Khi User Resume Lại

```
┌─────────┐                                    ┌─────────┐
│ User    │                                    │ Server  │
└────┬────┘                                    └────┬────┘
     │                                              │
     │ 1. Login lại                                │
     │─────────────────────────────────────────────>│
     │                                              │
     │ 2. Vào Room UI, click vào room đã thi       │
     │                                              │
     │ 3. RESUME_EXAM|room_id                       │
     │─────────────────────────────────────────────>│
     │                                              │
     │                                              │ 4. Server kiểm tra:
     │                                              │    - SELECT start_time FROM participants
     │                                              │    - start_time > 0? → Đã bắt đầu thi
     │                                              │
     │                                              │ 5. Tính thời gian còn lại:
     │                                              │    elapsed = now - start_time
     │                                              │    remaining = duration*60 - elapsed
     │                                              │
     │                                              │ 6. Kiểm tra:
     │                                              │    - remaining <= 0? → TIME_EXPIRED
     │                                              │    - Already submitted? → ALREADY_SUBMITTED
     │                                              │    - OK → RESUME_EXAM_OK
     │                                              │
     │ 7. RESUME_EXAM_OK|remaining_sec|questions...│
     │    (bao gồm saved_answer cho mỗi câu)       │
     │<─────────────────────────────────────────────│
     │                                              │
     │ 8. Client hiển thị UI:                      │
     │    - Timer đếm từ remaining (TIẾP TỤC)      │
     │    - Questions với saved answers checked     │
     │                                              │
```

**Bug đã fix:** Timer không tiếp tục sau resume

**Trước khi fix:**
```c
// File: client/exam_ui.c - create_exam_page_from_resume()
exam_start_time = time(NULL);           // ❌ SAI: set = hiện tại
exam_duration = (remaining_seconds / 60) + 1;

// Timer callback tính:
remaining = (exam_duration * 60) - (now - exam_start_time)
          = (remaining_seconds) - (now - now)
          = remaining_seconds  // ✅ Đúng lúc đầu
          
// Nhưng 1 giây sau:
remaining = (remaining_seconds) - 1  // ❌ Đếm ngược từ đầu!
```

**Sau khi fix:**
```c
// File: client/exam_ui.c - create_exam_page_from_resume()
exam_duration = (remaining_seconds + 59) / 60;  // Làm tròn lên
exam_start_time = time(NULL) - (exam_duration * 60 - remaining_seconds);
// ✅ Điều chỉnh start_time ngược về quá khứ

// Ví dụ: 
// - remaining_seconds = 900 (15 phút)
// - exam_duration = 15
// - now = 1000
// - exam_start_time = 1000 - (15*60 - 900) = 1000 - 0 = 1000
//
// Sau 5 giây:
// - now = 1005
// - elapsed = 1005 - 1000 = 5
// - remaining = 15*60 - 5 = 895  ✅ ĐÚNG!
```

---

## 6. TIMER MECHANISM (SERVER-SIDE)

### 6.1. Room Timer Thread

Server có thread riêng kiểm tra timeout cho các room đang thi:

```c
// File: server/timer.c

void check_room_timeouts(void) {
    time_t now = time(NULL);
    
    for (mỗi room đang STARTED) {
        // Tính thời gian đã trôi qua
        int elapsed = now - room->exam_start_time;
        int remaining = room->time_limit - elapsed;
        
        if (remaining <= 0) {
            // HẾT GIỜ
            room->room_status = ENDED;
            
            // Auto-submit CHỈ users đang ONLINE
            for (mỗi participant) {
                if (user_is_online(participant.user_id)) {
                    auto_submit_result(participant.user_id, room_id);
                } else {
                    // User offline → GIỮ LẠI để resume sau
                    printf("[TIMER] User offline - can resume later\n");
                }
            }
        }
    }
}
```

**Quy tắc quan trọng:**
- **Khi hết giờ**: Auto-submit **TẤT CẢ** users (cả online và offline) để tránh lỗi khi user offline vào lại
- **Nguyên nhân**: Nếu không submit user offline khi hết giờ, khi user vào lại sẽ gặp lỗi resume với thời gian âm
- **Trade-off**: User offline không thể resume sau khi hết giờ, nhưng đảm bảo hệ thống hoạt động ổn định

---

## 7. TÓM TẮT CÁC CƠ CHẾ CHÍNH

### 7.1. Non-blocking Polling (200ms)

| Thành phần | Vai trò |
|------------|---------|
| **GTK Timer** | Gọi callback mỗi 200ms |
| **MSG_PEEK** | Xem trước data KHÔNG remove |
| **is_broadcast()** | Phân biệt broadcast vs response |
| **Non-blocking mode** | Tạm thời để không block UI |
| **Blocking mode** | Khôi phục cho request-response |

### 7.2. Request-Response Flow

1. User action → `send_message(request)`
2. `receive_message()` → **BLOCK** đợi response
3. Parse response và xử lý
4. **Không conflict** với broadcast polling vì:
   - Polling chỉ xử lý broadcast messages
   - Response messages được để nguyên cho receive_message()

### 7.3. Disconnect & Resume Flow

| Trạng thái | Server | Client |
|------------|--------|--------|
| **Đang thi** | participant.start_time > 0 | Timer đang chạy |
| **Disconnect** | Lưu answers vào DB, GIỮ start_time | - |
| **Resume** | Tính remaining = duration - (now - start_time) | Điều chỉnh exam_start_time |
| **Timer** | Server-side check timeout | Client-side countdown |

### 7.4. Timer Synchronization

**Client Timer:**
```c
remaining = (exam_duration * 60) - (now - exam_start_time)
```

**Server Timer:**
```c
remaining = time_limit - (now - room->exam_start_time)
```

**Khi Resume:**
```c
// Server trả về remaining_seconds thực tế
// Client điều chỉnh exam_start_time để timer khớp:
exam_start_time = now - (total_duration - remaining_seconds)
```

---

## 8. VẤN ĐỀ VÀ GIẢI PHÁP ĐÃ FIX

### 8.1. Lỗi "Resource temporarily unavailable"

**Vấn đề:** 
Khi gửi request RESUME_EXAM hoặc BEGIN_EXAM, client nhận lỗi:
```
Cannot receive: Receive failed: Resource temporarily unavailable
```

**Nguyên nhân:**
- Broadcast listener set socket thành **non-blocking mode** (mỗi 200ms)
- Khi gửi request-response, socket vẫn ở non-blocking mode
- `recv()` với non-blocking socket trả về EAGAIN/EWOULDBLOCK nếu không có data ngay lập tức

**Giải pháp:**
Trong `receive_message()`, kiểm tra và khôi phục blocking mode:

```c
// File: client/net.c
ssize_t receive_message(char *buffer, size_t bufsz) {
    // Đảm bảo socket ở blocking mode
    int flags = fcntl(client.socket_fd, F_GETFL, 0);
    if (flags >= 0 && (flags & O_NONBLOCK)) {
        fcntl(client.socket_fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    
    // Bây giờ recv() sẽ block đợi response
    ssize_t n = recv(client.socket_fd, buffer, bufsz - 1, 0);
    ...
}
```

### 8.2. Lỗi User Offline Hết Giờ

**Vấn đề:**
User disconnect trong khi thi → Hết thời gian → User vào lại → Bị đơ/lỗi

**Kịch bản:**
```
1. User đang thi, còn 10 phút
2. User disconnect (mất mạng, đóng app)
3. 15 phút sau: Thời gian hết
4. Server KHÔNG auto-submit (vì user offline, để resume)
5. User vào lại và gửi RESUME_EXAM
6. Server tính remaining = duration - elapsed < 0 (âm!)
7. Server gọi auto_submit và gửi RESUME_TIME_EXPIRED
8. Client nhận RESUME_TIME_EXPIRED nhưng tiếp tục gửi BEGIN_EXAM
9. ❌ Lỗi: Socket state không đồng bộ
```

**Giải pháp:**
Timer auto-submit **TẤT CẢ** users (cả offline) khi hết giờ:

```c
// File: server/timer.c - check_room_timeouts()

if (remaining <= 0) {
    room->room_status = ENDED;
    
    // Query tất cả participants đã bắt đầu thi
    for (mỗi user_id) {
        // KHÔNG kiểm tra is_online nữa
        // Auto-submit TẤT CẢ users khi hết giờ
        
        if (!already_submitted) {
            // Tính điểm và insert vào results
            auto_submit_result(user_id, room_id);
            printf("[TIMER] Auto-submitted user %d when time expired\n", user_id);
        }
    }
}
```

**Trade-off:**
- ✅ **Ưu điểm**: Hệ thống ổn định, không bị lỗi khi user offline vào lại
- ⚠️ **Nhược điểm**: User offline không thể resume sau khi hết giờ (nhưng đây là hành vi đúng)

**Flow sau khi fix:**
```
1. User disconnect, còn 10 phút
2. 15 phút sau: Timer hết
3. Server auto-submit user (kể cả offline) → Insert vào results
4. User vào lại và gửi RESUME_EXAM
5. Server check results → Thấy đã submit
6. Server gửi: RESUME_ALREADY_SUBMITTED
7. Client hiển thị: "You have already completed this exam"
8. ✅ Không lỗi, user biết bài thi đã kết thúc
```

---

## 9. KẾT LUẬN

Hệ thống sử dụng **non-blocking polling + blocking request-response** kết hợp để:

✅ **Nhận broadcast real-time** (room started, time update)  
✅ **Xử lý request-response bình thường** (join, submit, save answer)  
✅ **Resume sau disconnect** với timer tiếp tục chính xác  
✅ **Không làm block GTK UI** nhờ non-blocking I/O  
✅ **Auto-submit khi hết giờ** (cả user online và offline) để đảm bảo tính nhất quán  
✅ **Tự động khôi phục blocking mode** trong receive_message() để tránh EAGAIN

**Key Insights:**
- `MSG_PEEK` là chìa khóa để "xem trước" mà không làm mất data
- Điều chỉnh `exam_start_time` ngược về quá khứ để timer resume đúng
- Server giữ `start_time` trong DB để tính remaining chính xác
- Auto-submit TẤT CẢ users khi hết giờ để tránh race condition
- `receive_message()` phải đảm bảo socket ở blocking mode để tránh EAGAIN từ broadcast listener
- Phân biệt user online/offline chỉ để logging, không ảnh hưởng logic auto-submit

**Các lỗi đã fix:**
1. ✅ Timer không chạy tiếp sau resume → Điều chỉnh exam_start_time
2. ✅ "Resource temporarily unavailable" → Khôi phục blocking mode trong receive_message()
3. ✅ User offline hết giờ bị lỗi khi vào lại → Auto-submit tất cả users khi timer expires
