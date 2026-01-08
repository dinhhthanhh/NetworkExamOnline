# BÁO CÁO KỸ THUẬT HỆ THỐNG THI TRẮC NGHIỆM TRỰC TUYẾN

## Môn: Lập trình mạng

---

## 1️⃣ GIỚI THIỆU ĐỀ TÀI

### 1.1 Tên đề tài

**Hệ thống thi trắc nghiệm trực tuyến (Online Quiz System)**

### 1.2 Mục tiêu hệ thống

#### 1.2.1 Mục tiêu chính

- Xây dựng hệ thống thi trắc nghiệm theo mô hình **Client-Server** qua mạng TCP/IP
- Hỗ trợ nhiều thí sinh thi đồng thời trong các phòng thi khác nhau
- Quản lý câu hỏi, phòng thi, kết quả và thống kê điểm số
- Cung cấp giao diện đồ họa trực quan cho người dùng

#### 1.2.2 So sánh với các giải pháp thi trực tuyến khác

| Tiêu chí                | Hệ thống này                                  | Google Forms                                 | Microsoft Forms           | Moodle                           |
| ----------------------- | --------------------------------------------- | -------------------------------------------- | ------------------------- | -------------------------------- |
| **Kết nối mạng**        | TCP/IP trực tiếp, độ trễ thấp (~1-5ms)        | HTTP/HTTPS qua cloud, độ trễ cao (~50-200ms) | HTTP/HTTPS qua cloud      | HTTP/HTTPS                       |
| **Đồng bộ thời gian**   | Server quản lý tập trung, chính xác tuyệt đối | Dựa vào thời gian client                     | Dựa vào thời gian client  | Server-side nhưng phụ thuộc HTTP |
| **Resume khi mất mạng** | ✅ Tự động lưu và phục hồi đáp án             | ❌ Mất dữ liệu chưa submit                   | ⚠️ Có nhưng không ổn định | ⚠️ Phụ thuộc cấu hình            |
| **Triển khai**          | Chạy local/LAN, không cần internet            | Bắt buộc internet                            | Bắt buộc internet         | Cần server web                   |
| **Bảo mật đáp án**      | Đáp án không bao giờ gửi về client            | Có thể xem source                            | Có thể xem source         | Tùy cấu hình                     |
| **Tùy biến**            | Hoàn toàn tùy biến (open source)              | Giới hạn                                     | Giới hạn                  | Cao nhưng phức tạp               |
| **Chi phí**             | Miễn phí, không giới hạn                      | Miễn phí nhưng giới hạn                      | Cần Office 365            | Miễn phí nhưng cần hosting       |

**Ưu điểm nổi bật của hệ thống này:**

1. **Hoạt động độc lập không cần internet**: Phù hợp cho các trường học, tổ chức có mạng nội bộ (LAN) mà không phụ thuộc kết nối internet.

2. **Đồng bộ thời gian chính xác**: Server là nguồn thời gian duy nhất, đảm bảo tất cả thí sinh có cùng thời gian làm bài, không bị ảnh hưởng bởi đồng hồ máy tính cá nhân.

3. **Khả năng phục hồi mạnh mẽ**: Khi mất kết nối đột ngột, hệ thống tự động lưu đáp án đã chọn vào database và cho phép thí sinh tiếp tục khi reconnect.

4. **Hiệu năng cao**: Sử dụng TCP socket trực tiếp với multi-threading giúp xử lý hàng trăm thí sinh đồng thời với độ trễ tối thiểu.

5. **Bảo mật đáp án**: Đáp án đúng chỉ được lưu trên server và chỉ gửi về client sau khi nộp bài, ngăn chặn gian lận.

### 1.3 Phạm vi chức năng

| Chức năng chính     | Mô tả                                                        |
| ------------------- | ------------------------------------------------------------ |
| Quản lý tài khoản   | Đăng ký, đăng nhập, đổi mật khẩu, phân quyền admin/user      |
| Thi chính thức      | Tạo phòng thi, tham gia, làm bài, nộp bài, tính điểm tự động |
| Chế độ luyện tập    | Practice mode với hiển thị đáp án ngay lập tức               |
| Quản lý câu hỏi     | Import CSV, phân loại theo độ khó (Easy/Medium/Hard)         |
| Thống kê & xếp hạng | Leaderboard, lịch sử thi, thống kê cá nhân                   |

### 1.4 Lý do chọn mô hình Client-Server

- **Tập trung hóa dữ liệu**: Tất cả câu hỏi, kết quả lưu trên server → đảm bảo tính nhất quán
- **Đồng bộ thời gian**: Server quản lý thời gian thi chung cho tất cả thí sinh
- **Bảo mật**: Đáp án chỉ lưu trên server, client không thể truy cập trực tiếp
- **Scalability**: Hỗ trợ nhiều client kết nối đồng thời (tối đa 100 clients)

### 1.5 Liên hệ với môn Lập trình mạng

| Kiến thức môn học   | Áp dụng trong đề tài                                 |
| ------------------- | ---------------------------------------------------- |
| Socket programming  | TCP socket (SOCK_STREAM) cho giao tiếp client-server |
| Multi-threading     | pthread cho xử lý đồng thời nhiều client             |
| Protocol design     | Thiết kế giao thức ứng dụng dạng text-based          |
| Concurrency control | Mutex lock để tránh race condition                   |
| Network I/O         | Blocking I/O với timeout handling                    |

### 1.6 So sánh và lựa chọn công nghệ

#### 1.6.1 Tại sao chọn Multi-threading thay vì các giải pháp khác?

Trong lập trình mạng có nhiều mô hình xử lý concurrent connections khác nhau. Dưới đây là phân tích so sánh:

| Mô hình                            | Mô tả                                     | Ưu điểm                                                              | Nhược điểm                                                              |
| ---------------------------------- | ----------------------------------------- | -------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| **Multi-threading (pthread)** ✅   | Mỗi client được xử lý bởi 1 thread riêng  | Code đơn giản, dễ debug; Mỗi client độc lập; Tận dụng multi-core CPU | Overhead memory (~8KB/thread); Context switching                        |
| **Multi-process (fork)**           | Mỗi client được xử lý bởi 1 process riêng | Isolation tốt; Một process crash không ảnh hưởng khác                | Overhead lớn (~MB/process); Khó share data giữa processes               |
| **I/O Multiplexing (poll/select)** | 1 thread xử lý nhiều connections          | Tiết kiệm memory; Không cần synchronization                          | Code phức tạp (state machine); Không tận dụng multi-core; Callback hell |
| **Event-driven (epoll/kqueue)**    | Non-blocking I/O với event loop           | Hiệu năng cao với rất nhiều connections                              | Phức tạp; Không phù hợp CPU-bound tasks                                 |

**Lý do chọn Multi-threading cho dự án này:**

1. **Đơn giản và dễ bảo trì**: Logic xử lý cho mỗi client là sequential, dễ đọc và debug. Mỗi thread có flow riêng biệt: `recv() → process → send()`.

2. **Phù hợp với quy mô dự án**: Với giới hạn 100 clients, memory overhead của threads (~800KB total) là chấp nhận được. Không cần tối ưu cực đoan như event-driven.

3. **Tận dụng multi-core**: Các threads có thể chạy song song trên nhiều CPU cores, phù hợp với các thao tác xử lý bài thi (tính điểm, truy vấn DB).

4. **Dễ dàng triển khai resume/disconnect handling**: Mỗi thread quản lý state của 1 client, dễ dàng phát hiện disconnect và lưu trạng thái.

**So sánh chi tiết với fork():**

```
fork() approach:
┌─────────────┐    fork()    ┌─────────────┐
│   Parent    │ ──────────── │   Child 1   │  (Client 1)
│   Process   │    fork()    │   Child 2   │  (Client 2)
│             │ ──────────── │   ...       │
└─────────────┘              └─────────────┘
                                   ↓
                         Không share data trực tiếp
                         → Cần IPC (shared memory, pipe)
                         → Phức tạp hơn nhiều!

pthread() approach (đã chọn):
┌────────────────────────────────────────────┐
│              Main Process                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐    │
│  │ Thread 1 │ │ Thread 2 │ │ Thread N │    │
│  │ Client 1 │ │ Client 2 │ │ Client N │    │
│  └──────────┘ └──────────┘ └──────────┘    │
│         │           │           │          │
│         └───────────┼───────────┘          │
│                     ↓                       │
│            ┌─────────────────┐              │
│            │   server_data   │ (shared)     │
│            │   + mutex lock  │              │
│            └─────────────────┘              │
└────────────────────────────────────────────┘
Chia sẻ dữ liệu dễ dàng qua biến global
→ Chỉ cần mutex để đồng bộ
→ Đơn giản hơn nhiều!
```

**So sánh chi tiết với poll/select:**

```c
// poll/select approach - phức tạp, cần state machine:
while (1) {
    int nfds = poll(fds, num_clients, -1);
    for (int i = 0; i < num_clients; i++) {
        if (fds[i].revents & POLLIN) {
            // Phải tự quản lý state của từng client
            switch (client_states[i].state) {
                case WAITING_LOGIN:
                    // xử lý login, chuyển state
                    break;
                case WAITING_COMMAND:
                    // xử lý command, có thể cần nhiều recv() calls
                    break;
                // ... nhiều states khác, rất phức tạp!
            }
        }
    }
}

// pthread approach (đã chọn) - đơn giản, sequential:
void *handle_client(void *arg) {
    // Mỗi client có thread riêng, code là sequential
    while (1) {
        recv(socket, buffer, ...);  // Blocking OK
        process_command(buffer);     // Xử lý đơn giản
        send(socket, response, ...); // Gửi response
    }
}
```

#### 1.6.2 Tại sao sử dụng Mutex? Mutex là gì và hoạt động như thế nào?

**Mutex là gì?**

Mutex (Mutual Exclusion) là cơ chế đồng bộ hóa trong lập trình đa luồng, đảm bảo tại một thời điểm chỉ có **một thread duy nhất** được truy cập vào vùng critical section (vùng code thao tác với shared data).

**Tại sao cần Mutex trong dự án này?**

Khi nhiều threads cùng truy cập và thay đổi dữ liệu chung (`server_data`), có thể xảy ra **Race Condition** - tình huống kết quả phụ thuộc vào thứ tự thực thi không thể dự đoán được.

**Ví dụ Race Condition trong hệ thống thi:**

```
Giả sử 2 thí sinh (Thread 1 và Thread 2) cùng join room đồng thời:

Thread 1 (User A join room):           Thread 2 (User B join room):
─────────────────────────────          ─────────────────────────────
1. Đọc participant_count = 5           1. Đọc participant_count = 5
2. participants[5] = user_A            2. participants[5] = user_B  ← GHI ĐÈ!
3. participant_count = 6               3. participant_count = 6     ← SAI!

Kết quả: User A bị mất! participant_count = 6 nhưng chỉ có 5 người!
```

**Giải pháp với Mutex:**

```c
// Cấu trúc ServerData có mutex:
typedef struct {
    User users[MAX_CLIENTS];
    TestRoom rooms[MAX_ROOMS];
    // ... other fields ...
    pthread_mutex_t lock;  // Mutex để bảo vệ toàn bộ data
} ServerData;

// Sử dụng mutex trong server/rooms.c khi join room:
void join_test_room(int socket_fd, int room_id, int user_id) {
    pthread_mutex_lock(&server_data.lock);    // 🔒 KHÓA

    // ===== CRITICAL SECTION - chỉ 1 thread được vào =====
    TestRoom *room = find_room(room_id);
    room->participants[room->participant_count] = user_id;
    room->participant_count++;
    // ==================================================

    pthread_mutex_unlock(&server_data.lock);  // 🔓 MỞ KHÓA

    // Gửi response ngoài critical section để giảm thời gian lock
    send_response(socket_fd, "JOIN_ROOM_OK");
}
```

**Các vị trí sử dụng Mutex trong dự án:**

| Module      | Thao tác         | Lý do cần mutex                                  |
| ----------- | ---------------- | ------------------------------------------------ |
| `auth.c`    | Login/Logout     | Cập nhật `is_online`, `socket_fd` của user       |
| `auth.c`    | Register         | Thêm user mới vào `server_data.users[]`          |
| `rooms.c`   | Create room      | Thêm room vào `server_data.rooms[]`              |
| `rooms.c`   | Join room        | Cập nhật `participants[]` và `participant_count` |
| `rooms.c`   | Start room       | Cập nhật `room_status` và `exam_start_time`      |
| `results.c` | Save answer      | Cập nhật `answers[][]` array                     |
| `results.c` | Submit test      | Tính điểm và cập nhật `scores[]`                 |
| `db.c`      | Database queries | SQLite không thread-safe mặc định                |

**Pattern sử dụng Mutex trong code:**

```c
// Pattern chung được áp dụng:
void some_function(int socket_fd, ...) {
    char response[BUFFER_SIZE];

    pthread_mutex_lock(&server_data.lock);
    // ------------------------------------------------
    // Thao tác với shared data (đọc/ghi)
    // Chuẩn bị response string
    // ------------------------------------------------
    pthread_mutex_unlock(&server_data.lock);

    // Gửi response NGOÀI lock để các thread khác có thể chạy
    server_send(socket_fd, response);
}
```

**Tối ưu hiệu năng với Mutex:**

1. **Minimize critical section**: Chỉ lock những phần thực sự cần thiết
2. **Gửi response ngoài lock**: Tránh block các thread khác khi đang I/O
3. **Tránh nested locks**: Không gọi function khác có lock bên trong lock → deadlock

#### 1.6.3 Chi tiết về POSIX Threads (pthread)

**Tại sao chọn pthread thay vì các threading libraries khác?**

| Library        | Mô tả               | Ưu điểm                                                    | Nhược điểm                   |
| -------------- | ------------------- | ---------------------------------------------------------- | ---------------------------- |
| **pthread** ✅ | POSIX standard      | Portable (Linux/macOS/BSD); Lightweight; Low-level control | Không có trên Windows native |
| C11 threads    | C language standard | Portable; Modern syntax                                    | Ít tài liệu; Không phổ biến  |
| OpenMP         | Parallel computing  | Dễ dùng cho data parallelism                               | Không phù hợp cho I/O-bound  |
| Win32 threads  | Windows API         | Native Windows                                             | Không portable               |

**Cách sử dụng pthread trong dự án:**

```c
// server/quiz_server.c - Main accept loop
while (1) {
    // Allocate socket on heap (quan trọng - thread cần pointer ổn định)
    client_socket = malloc(sizeof(int));

    // Accept connection
    *client_socket = accept(server_socket, ...);

    // Tạo thread mới để xử lý client
    pthread_create(&thread_id, NULL, handle_client, (void *)client_socket);

    // Detach thread - tự động cleanup khi kết thúc
    pthread_detach(thread_id);
}
```

**Thread lifecycle trong hệ thống:**

```
Client connect
      │
      ▼
┌─────────────────────────────────────────────┐
│          pthread_create()                    │
│  Tạo thread mới với entry point:            │
│  handle_client(socket)                       │
└─────────────────────────────────────────────┘
      │
      ▼
┌─────────────────────────────────────────────┐
│          pthread_detach()                    │
│  Main thread tiếp tục accept()               │
│  Không cần join - thread tự cleanup          │
└─────────────────────────────────────────────┘
      │
      ▼
┌─────────────────────────────────────────────┐
│          handle_client() loop                │
│  while (connected) {                         │
│      recv() → process() → send()            │
│  }                                           │
└─────────────────────────────────────────────┘
      │
      ▼
┌─────────────────────────────────────────────┐
│          Thread exit (implicit return)       │
│  - Close socket                              │
│  - Free resources                            │
│  - Thread auto-destroyed (detached)          │
└─────────────────────────────────────────────┘
```

**Tại sao dùng `pthread_detach()` thay vì `pthread_join()`?**

- `pthread_join()`: Main thread phải đợi child thread kết thúc → Blocking, không phù hợp cho server
- `pthread_detach()`: Thread tự động giải phóng resources khi kết thúc → Non-blocking, phù hợp cho server

**Synchronization primitives có sẵn trong pthread:**

| Primitive           | Dùng trong dự án | Mục đích                             |
| ------------------- | ---------------- | ------------------------------------ |
| `pthread_mutex_t`   | ✅ Có            | Mutual exclusion cho shared data     |
| `pthread_cond_t`    | ❌ Không         | Condition variables (chưa cần)       |
| `pthread_rwlock_t`  | ❌ Không         | Read-write locks (có thể tối ưu sau) |
| `pthread_barrier_t` | ❌ Không         | Synchronization barriers             |

---

## 2️⃣ CÔNG NGHỆ – NGÔN NGỮ – THƯ VIỆN SỬ DỤNG

### 2.1 Ngôn ngữ lập trình

| Thành phần | Ngôn ngữ    | Ghi chú                              |
| ---------- | ----------- | ------------------------------------ |
| **Server** | C (GNU C99) | Hiệu năng cao, kiểm soát bộ nhớ      |
| **Client** | C (GNU C99) | Đồng bộ với server, tái sử dụng code |

### 2.2 Công nghệ & nền tảng

#### Socket & Networking

| Công nghệ       | Chi tiết                            |
| --------------- | ----------------------------------- |
| **Socket type** | TCP (SOCK_STREAM) - đảm bảo tin cậy |
| **Socket API**  | POSIX Socket (BSD Socket)           |
| **Protocol**    | IPv4 (AF_INET)                      |
| **Port**        | 8888 (định nghĩa trong `common.h`)  |

**Evidence từ code:**

```c
// server/quiz_server.c:39
server_socket = socket(AF_INET, SOCK_STREAM, 0);

// server/include/common.h:11
#define PORT 8888
```

#### Multi-threading

| Công nghệ           | Chi tiết                |
| ------------------- | ----------------------- |
| **Thread library**  | POSIX Threads (pthread) |
| **Model**           | 1 thread per client     |
| **Synchronization** | pthread_mutex_t         |

**Evidence từ code:**

```c
// server/quiz_server.c:86
pthread_create(&thread_id, NULL, handle_client, (void *)client_socket);
pthread_detach(thread_id);

// server/include/common.h:107
pthread_mutex_t lock;
```

#### Database

| Công nghệ        | Chi tiết                 |
| ---------------- | ------------------------ |
| **Database**     | SQLite 3 (embedded)      |
| **File**         | `quiz_app.db`            |
| **Transactions** | Có hỗ trợ (BEGIN/COMMIT) |

#### GUI Framework

| Công nghệ      | Chi tiết           |
| -------------- | ------------------ |
| **Library**    | GTK+ 3.0           |
| **Rendering**  | Cairo (cho đồ họa) |
| **Event loop** | GLib main loop     |

### 2.3 Thư viện sử dụng

| Thư viện          | Dùng cho                                           | File liên quan                           |
| ----------------- | -------------------------------------------------- | ---------------------------------------- |
| `<sys/socket.h>`  | Socket API (socket, bind, listen, accept, connect) | `quiz_server.c`, `net.c`, `network.c`    |
| `<netinet/in.h>`  | Cấu trúc sockaddr_in, htons, INADDR_ANY            | `quiz_server.c`, `main.c`                |
| `<arpa/inet.h>`   | inet_aton, inet_ntoa                               | `main.c`, `net.c`                        |
| `<pthread.h>`     | Multi-threading (pthread_create, pthread_mutex)    | Tất cả file server                       |
| `<sqlite3.h>`     | Database operations                                | `db.c`, `auth.c`, `rooms.c`, `results.c` |
| `<openssl/sha.h>` | SHA-256 password hashing                           | `auth.c`                                 |
| `<gtk/gtk.h>`     | GUI widgets                                        | Tất cả file client `*_ui.c`              |
| `<cairo.h>`       | Graphics rendering                                 | `ui.c`                                   |
| `<glib.h>`        | String utilities, memory management                | Client UI files                          |
| `<sys/time.h>`    | Socket timeout (struct timeval)                    | `net.c`                                  |
| `<errno.h>`       | Error handling (EAGAIN, ECONNRESET)                | `net.c`                                  |
| `<time.h>`        | Timestamp, time functions                          | `auth.c`, `rooms.c`, `timer.c`           |
| `<unistd.h>`      | close(), usleep()                                  | Multiple files                           |

---

## 3️⃣ CẤU TRÚC THƯ MỤC & Ý NGHĨA TỪNG FILE

### 3.1 Cây thư mục

```text
NetworkExamOnline/
├── server/                         # Server application
│   ├── include/
│   │   └── common.h                # Shared definitions & data structures
│   ├── quiz_server.c               # Main entry point, TCP listener
│   ├── network.c                   # Protocol handler, message router
│   ├── network.h                   # Network function declarations
│   ├── auth.c                      # Authentication (login, register, session)
│   ├── auth.h
│   ├── db.c                        # Database initialization & queries
│   ├── db.h
│   ├── rooms.c                     # Exam room management
│   ├── rooms.h
│   ├── practice.c                  # Practice mode logic
│   ├── practice.h
│   ├── questions.c                 # Question bank management
│   ├── questions.h
│   ├── results.c                   # Answer saving & scoring
│   ├── results.h
│   ├── stats.c                     # Statistics & leaderboard
│   ├── stats.h
│   ├── admin.c                     # Admin-specific functions
│   ├── admin.h
│   ├── timer.c                     # Room timeout checking
│   ├── timer.h
│   └── Makefile
├── client/                         # Client application
│   ├── include/
│   │   └── client_common.h         # Client data structures
│   ├── main.c                      # Entry point, GTK init, connect
│   ├── net.c                       # Socket communication
│   ├── net.h
│   ├── ui.c                        # Main menu logic
│   ├── ui.h
│   ├── ui_utils.c                  # UI helper functions
│   ├── ui_utils.h
│   ├── auth_ui.c                   # Login/Register screens
│   ├── auth_ui.h
│   ├── room_ui.c                   # Room list & selection
│   ├── room_ui.h
│   ├── exam_ui.c                   # Exam taking interface
│   ├── exam_ui.h
│   ├── practice_ui.c               # Practice mode UI
│   ├── practice_ui.h
│   ├── stats_ui.c                  # Statistics display
│   ├── stats_ui.h
│   ├── admin_ui.c                  # Admin panel
│   ├── admin_ui.h
│   ├── question_ui.c               # Question management UI
│   ├── question_ui.h
│   ├── password_ui.c               # Change password dialog
│   ├── password_ui.h
│   ├── broadcast.c                 # Handle server broadcasts
│   ├── broadcast.h
│   ├── exam_room_creator.c         # Room creation wizard
│   ├── exam_room_creator.h
│   └── Makefile
├── data/                           # Sample data files
│   ├── questions.csv
│   ├── questions_sample.csv
│   ├── sample_exam_questions.csv
│   └── users.csv
└── README.md
```

### 3.2 Bảng giải thích chi tiết

#### Server Files

| File            | Vai trò                        | Chức năng liên quan                                |
| --------------- | ------------------------------ | -------------------------------------------------- |
| `quiz_server.c` | Entry point, TCP listener loop | Khởi tạo socket, accept connections, spawn threads |
| `network.c`     | Protocol handler chính         | Parse commands, route to modules, broadcast        |
| `common.h`      | Định nghĩa cấu trúc dữ liệu    | Question, TestRoom, User, ServerData structs       |
| `auth.c`        | Xác thực người dùng            | Login, register, logout, session token, SHA-256    |
| `db.c`          | Khởi tạo & thao tác database   | Schema creation, migration, load data              |
| `rooms.c`       | Quản lý phòng thi              | Create, join, start, close, delete rooms           |
| `practice.c`    | Chế độ luyện tập               | Practice rooms, sessions, immediate feedback       |
| `questions.c`   | Quản lý ngân hàng câu hỏi      | Import CSV, add/edit/delete questions              |
| `results.c`     | Xử lý đáp án & điểm            | Save answers, calculate score, auto-submit         |
| `stats.c`       | Thống kê & xếp hạng            | Leaderboard, user stats, test history              |
| `admin.c`       | Chức năng admin                | Quản lý users, import data                         |
| `timer.c`       | Kiểm tra timeout phòng thi     | Auto-submit khi hết giờ                            |

#### Client Files

| File            | Vai trò                   | Chức năng liên quan                      |
| --------------- | ------------------------- | ---------------------------------------- |
| `main.c`        | Entry point               | GTK init, connect to server, main window |
| `net.c`         | Socket communication      | send/receive message, timeout, reconnect |
| `ui.c`          | Main menu                 | Show menu based on role (admin/user)     |
| `auth_ui.c`     | Authentication screens    | Login, register forms                    |
| `room_ui.c`     | Room management UI        | List rooms, join room                    |
| `exam_ui.c`     | Exam interface            | Question display, timer, submit          |
| `practice_ui.c` | Practice mode UI          | Practice with immediate feedback         |
| `stats_ui.c`    | Statistics display        | User stats, leaderboard, history         |
| `admin_ui.c`    | Admin panel               | Room management, question import         |
| `password_ui.c` | Password change           | Change password dialog                   |
| `broadcast.c`   | Handle push notifications | Room created, room deleted events        |

---

## 4️⃣ PHÂN TÍCH NGHIỆP VỤ & CHỨC NĂNG

### 4.1 Đối chiếu đề bài ↔ Code

| Chức năng đề bài    | Đã làm | File/Module                       | Ghi chú                               |
| ------------------- | ------ | --------------------------------- | ------------------------------------- |
| Thi chính thức      | ✅     | `rooms.c`, `exam_ui.c`            | Phòng thi với thời gian giới hạn      |
| Luyện tập           | ✅     | `practice.c`, `practice_ui.c`     | Hiển thị đáp án ngay hoặc sau khi nộp |
| Tạo phòng thi       | ✅     | `rooms.c:create_test_room()`      | Chỉ admin, chọn số câu theo độ khó    |
| Quản lý thời gian   | ✅     | `timer.c`, `rooms.c`              | Countdown, auto-submit khi hết giờ    |
| Nộp bài             | ✅     | `results.c:submit_test()`         | Tính điểm tự động                     |
| Chấm điểm           | ✅     | `results.c`                       | JOIN exam_answers với exam_questions  |
| Xem kết quả         | ✅     | `stats.c`, `stats_ui.c`           | Điểm, thời gian làm bài               |
| Phân loại câu hỏi   | ✅     | `questions.c`                     | Easy/Medium/Hard                      |
| Lưu lịch sử         | ✅     | `stats.c:get_user_test_history()` | Bảng results trong DB                 |
| Thống kê            | ✅     | `stats.c`                         | Avg score, max score, total tests     |
| Bảng xếp hạng       | ✅     | `stats.c:get_leaderboard()`       | Top N users by total score            |
| Đăng ký/Đăng nhập   | ✅     | `auth.c`                          | SHA-256 password hashing              |
| Phân quyền          | ✅     | `auth.c`, `ui.c`                  | admin vs user roles                   |
| Resume khi mất mạng | ✅     | `network.c`, `results.c`          | Flush answers to DB on disconnect     |
| Import câu hỏi CSV  | ✅     | `questions.c`, `admin_ui.c`       | Bulk import                           |

### 4.2 Chi tiết chức năng theo role

#### Admin Features

- Tạo/Xóa phòng thi
- Start room (bắt đầu bài thi)
- Import câu hỏi từ CSV
- Quản lý practice rooms
- Xem thành viên trong phòng

#### User Features

- Đăng ký tài khoản mới
- Tham gia phòng thi
- Làm bài và nộp bài
- Xem kết quả và thống kê cá nhân
- Xem bảng xếp hạng
- Chế độ luyện tập

---

## 5️⃣ KIẾN TRÚC HỆ THỐNG TỔNG THỂ

### 5.1 Mô hình Client-Server

```plantuml
@startuml
!theme plain
skinparam componentStyle rectangle

package "Client Side" {
    [GTK+ 3.0 GUI] as GUI
    [Network Module\n(net.c)] as ClientNet
    [UI Controllers\n(*_ui.c)] as Controllers
}

package "Server Side" {
    [TCP Listener\n(quiz_server.c)] as Listener
    [Thread Pool\n(1 thread/client)] as Threads
    [Protocol Handler\n(network.c)] as Handler
    [Business Logic\n(rooms, auth, results)] as Logic
    [SQLite Database\n(quiz_app.db)] as DB
}

cloud "TCP/IP Network" as Network

GUI --> Controllers
Controllers --> ClientNet
ClientNet --> Network : TCP Port 8888
Network --> Listener
Listener --> Threads
Threads --> Handler
Handler --> Logic
Logic --> DB

@enduml
```

### 5.2 Luồng xử lý chính

```plantuml
@startuml
!theme plain
participant "Client" as C
participant "Server Main\n(quiz_server.c)" as S
participant "Handler Thread\n(network.c)" as H
participant "Business Logic" as B
database "SQLite DB" as DB

C -> S: connect()
S -> S: accept()
S -> H: pthread_create(handle_client)
activate H

C -> H: "LOGIN|user|pass\n"
H -> B: login_user()
B -> DB: SELECT id, role FROM users
DB --> B: user_id, role
B --> H: "LOGIN_OK|id|token|role\n"
H --> C: response

C -> H: "JOIN_ROOM|room_id\n"
H -> B: join_test_room()
B -> DB: INSERT participants
B --> H: "JOIN_ROOM_OK\n"
H --> C: response

C -> H: "SAVE_ANSWER|room|q|ans\n"
H -> B: save_answer()
B -> DB: INSERT exam_answers
B --> H: "SAVE_ANSWER_OK\n"
H --> C: response

C -> H: "SUBMIT_TEST|room_id\n"
H -> B: submit_test()
B -> DB: Calculate score, INSERT results
B --> H: "SUBMIT_TEST_OK|score|total|time\n"
H --> C: response

deactivate H
@enduml
```

### 5.3 Thành phần chính

```plantuml
@startuml
!theme plain
package "Server Components" {
    component [quiz_server.c] as Main
    component [network.c] as Net
    component [auth.c] as Auth
    component [rooms.c] as Rooms
    component [practice.c] as Practice
    component [results.c] as Results
    component [stats.c] as Stats
    component [db.c] as DB
    component [timer.c] as Timer
}

Main --> Net : spawn threads
Net --> Auth : REGISTER, LOGIN, LOGOUT
Net --> Rooms : CREATE_ROOM, JOIN_ROOM, START_ROOM
Net --> Practice : CREATE_PRACTICE, JOIN_PRACTICE
Net --> Results : SAVE_ANSWER, SUBMIT_TEST
Net --> Stats : LEADERBOARD, USER_STATS
Net --> DB : init, queries

Timer --> Rooms : check_room_timeouts()
Results --> DB : flush_answers_to_db()

@enduml
```

---

## 6️⃣ THIẾT KẾ GIAO THỨC TRUYỀN THÔNG TẦNG ỨNG DỤNG

### 6.1 Kiểu truyền

| Đặc điểm        | Chi tiết                       |
| --------------- | ------------------------------ | ---------------------------- |
| **Transport**   | TCP Stream (SOCK_STREAM)       |
| **Đóng gói**    | Text-based, newline-terminated |
| **Delimiter**   | `                              | ` (pipe) phân tách các field |
| **Encoding**    | ASCII/UTF-8                    |
| **Buffer size** | 4096 bytes                     |

**Evidence từ code:**

```c
// server/network.c:76
char *cmd = strtok(buffer, "|");

// server/include/common.h:13
#define BUFFER_SIZE 4096
```

### 6.2 Định dạng bản tin

```
COMMAND|arg1|arg2|...|argN\n
```

| Thành phần     | Mô tả                        |
| -------------- | ---------------------------- | --- |
| **COMMAND**    | Tên lệnh (uppercase)         |
| **args**       | Các tham số, phân tách bởi ` | `   |
| **Terminator** | `\n` (newline)               |

**Response format:**

```
COMMAND_OK|data1|data2|...\n
COMMAND_FAIL|error_message\n
```

### 6.3 Danh sách message

#### Authentication Messages

| Message                     | Ý nghĩa              | Hướng | Ví dụ            |
| --------------------------- | -------------------- | ----- | ---------------- | ---------------------- | --------- | ------- |
| `REGISTER\|user\|pass`      | Đăng ký tài khoản    | C→S   | `REGISTER        | john                   | 123456\n` |
| `REGISTER_OK`               | Đăng ký thành công   | S→C   | `REGISTER_OK     | Account created\n`     |
| `REGISTER_FAIL\|reason`     | Đăng ký thất bại     | S→C   | `REGISTER_FAIL   | Username exists\n`     |
| `LOGIN\|user\|pass`         | Đăng nhập            | C→S   | `LOGIN           | john                   | 123456\n` |
| `LOGIN_OK\|id\|token\|role` | Đăng nhập thành công | S→C   | `LOGIN_OK        | 5                      | abc123    | user\n` |
| `LOGIN_FAIL\|reason`        | Đăng nhập thất bại   | S→C   | `LOGIN_FAIL      | Invalid credentials\n` |
| `LOGOUT`                    | Đăng xuất            | C→S   | `LOGOUT\n`       |
| `LOGOUT_OK`                 | Đăng xuất thành công | S→C   | `LOGOUT_OK\n`    |
| `CHANGE_PASSWORD\|old\|new` | Đổi mật khẩu         | C→S   | `CHANGE_PASSWORD | old123                 | new456\n` |

#### Room Management Messages

| Message                                             | Ý nghĩa                 | Hướng |
| --------------------------------------------------- | ----------------------- | ----- |
| `LIST_ROOMS`                                        | Lấy danh sách phòng     | C→S   |
| `LIST_ROOMS_OK\|count`                              | Response với số phòng   | S→C   |
| `ROOM\|id\|name\|duration\|status\|questions\|host` | Thông tin 1 phòng       | S→C   |
| `CREATE_ROOM\|name\|time\|easy\|medium\|hard`       | Tạo phòng (admin)       | C→S   |
| `CREATE_ROOM_OK\|id\|name\|time\|count`             | Tạo thành công          | S→C   |
| `JOIN_ROOM\|room_id`                                | Tham gia phòng          | C→S   |
| `JOIN_ROOM_OK`                                      | Tham gia thành công     | S→C   |
| `START_ROOM\|room_id`                               | Bắt đầu bài thi (admin) | C→S   |
| `CLOSE_ROOM\|room_id`                               | Đóng phòng              | C→S   |
| `DELETE_ROOM\|room_id`                              | Xóa phòng               | C→S   |

#### Exam Messages

| Message                               | Ý nghĩa                       | Hướng |
| ------------------------------------- | ----------------------------- | ----- |
| `BEGIN_EXAM\|room_id`                 | Bắt đầu làm bài               | C→S   |
| `RESUME_EXAM\|room_id`                | Tiếp tục bài thi bị gián đoạn | C→S   |
| `SAVE_ANSWER\|room\|question\|answer` | Lưu đáp án                    | C→S   |
| `SAVE_ANSWER_OK`                      | Lưu thành công                | S→C   |
| `SUBMIT_TEST\|room_id`                | Nộp bài                       | C→S   |
| `SUBMIT_TEST_OK\|score\|total\|time`  | Nộp bài thành công            | S→C   |

#### Practice Messages

| Message                                 | Ý nghĩa                  | Hướng |
| --------------------------------------- | ------------------------ | ----- |
| `CREATE_PRACTICE\|name\|cooldown\|show` | Tạo phòng luyện tập      | C→S   |
| `LIST_PRACTICE`                         | Danh sách practice rooms | C→S   |
| `JOIN_PRACTICE\|practice_id`            | Tham gia luyện tập       | C→S   |
| `SUBMIT_PRACTICE_ANSWER\|id\|q\|ans`    | Nộp đáp án practice      | C→S   |
| `FINISH_PRACTICE\|practice_id`          | Kết thúc luyện tập       | C→S   |

#### Statistics Messages

| Message              | Ý nghĩa           | Hướng |
| -------------------- | ----------------- | ----- |
| `LEADERBOARD\|limit` | Lấy bảng xếp hạng | C→S   |
| `USER_STATS`         | Thống kê cá nhân  | C→S   |
| `TEST_HISTORY`       | Lịch sử thi       | C→S   |

#### Server Push Messages (Broadcast)

| Message                            | Ý nghĩa                | Hướng |
| ---------------------------------- | ---------------------- | ----- |
| `ROOM_CREATED\|id\|name\|duration` | Thông báo phòng mới    | S→C   |
| `ROOM_DELETED\|id`                 | Thông báo phòng bị xóa | S→C   |
| `TIME_UPDATE\|room_id\|remaining`  | Cập nhật thời gian     | S→C   |

### 6.4 Luồng giao tiếp chi tiết

#### Login Flow

```plantuml
@startuml
!theme plain
participant Client
participant Server

Client -> Server: LOGIN|username|password\n
Server -> Server: hash_password(password)
Server -> Server: Query DB: SELECT id, role WHERE username AND password
alt Valid credentials
    Server --> Client: LOGIN_OK|user_id|session_token|role\n
else Invalid credentials
    Server --> Client: LOGIN_FAIL|Invalid credentials\n
else Already logged in
    Server --> Client: LOGIN_FAIL|User already logged in\n
end
@enduml
```

#### Tạo phòng thi (Admin)

```plantuml
@startuml
!theme plain
participant Admin as A
participant Server as S
database DB

A -> S: CREATE_ROOM|RoomName|30|5|3|2\n
S -> S: Check role == admin
S -> DB: INSERT INTO rooms
S -> DB: SELECT questions by difficulty (RANDOM)
S -> DB: UPDATE exam_questions SET room_id
S --> A: CREATE_ROOM_OK|room_id|name|time|questions\n
S -> S: broadcast_room_created()
S --> Other: ROOM_CREATED|room_id|name|duration\n
@enduml
```

#### Làm bài thi

```plantuml
@startuml
!theme plain
participant User as U
participant Server as S
database DB

U -> S: BEGIN_EXAM|room_id\n
S -> DB: Get questions for room
S -> DB: UPDATE participants SET start_time
S --> U: EXAM_DATA|questions...\n

loop For each question
    U -> S: SAVE_ANSWER|room|q_id|answer\n
    S -> S: Store in-memory
    alt Every 5 answers
        S -> DB: Flush to exam_answers
    end
    S --> U: SAVE_ANSWER_OK\n
end

U -> S: SUBMIT_TEST|room_id\n
S -> DB: Flush all remaining answers
S -> DB: Calculate score (JOIN answers + questions)
S -> DB: INSERT INTO results
S -> DB: UPDATE has_taken_exam = 1
S --> U: SUBMIT_TEST_OK|8|10|25\n
@enduml
```

---

## 7️⃣ XỬ LÝ SOCKET & LUỒNG (THREAD / CONCURRENCY)

### 7.1 Mô hình server

| Đặc điểm             | Giá trị                                              |
| -------------------- | ---------------------------------------------------- |
| **Mô hình**          | Multi-threaded, 1 thread per client                  |
| **Max clients**      | 100 (MAX_CLIENTS)                                    |
| **Thread creation**  | `pthread_create()` khi accept() thành công           |
| **Thread lifecycle** | `pthread_detach()` - tự động giải phóng khi kết thúc |

**Evidence từ code:**

```c
// server/quiz_server.c:86-94
if (pthread_create(&thread_id, NULL, handle_client, (void *)client_socket) != 0) {
    perror("pthread_create failed");
    close(*client_socket);
    free(client_socket);
    continue;
}
pthread_detach(thread_id);
```

### 7.2 Cách accept kết nối

```c
// server/quiz_server.c:70-84
while (1) {
    client_len = sizeof(client_addr);
    client_socket = malloc(sizeof(int));  // Allocate on heap

    *client_socket = accept(server_socket,
                           (struct sockaddr *)&client_addr,
                           &client_len);  // Blocking call

    // Spawn thread to handle this client
    pthread_create(&thread_id, NULL, handle_client, (void *)client_socket);
    pthread_detach(thread_id);
}
```

### 7.3 Cách đọc/ghi socket

#### Server side (Blocking I/O)

```c
// server/network.c:44-45
int n = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);  // Blocking read

// server/network.c:25
return send(socket_fd, msg, strlen(msg), 0);  // Blocking write
```

#### Client side (với timeout)

```c
// client/net.c:13-20
void net_set_timeout(int sockfd) {
    struct timeval timeout;
    timeout.tv_sec = 5;   // 5 seconds timeout
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
```

### 7.4 Đồng bộ dữ liệu & tránh Race Condition

#### Mutex Lock Pattern

```c
// Mọi thao tác với server_data đều phải khóa mutex
pthread_mutex_lock(&server_data.lock);

// ... Critical section: access/modify shared data ...

pthread_mutex_unlock(&server_data.lock);
```

**Ví dụ trong auth.c:**

```c
void login_user(int socket_fd, char *username, char *password, int *user_id) {
    pthread_mutex_lock(&server_data.lock);  // Lock before DB access

    // ... query database, update user state ...

    pthread_mutex_unlock(&server_data.lock);
    server_send(socket_fd, response);  // Send OUTSIDE lock
}
```

#### Cấu trúc dữ liệu được bảo vệ

| Data Structure                 | Protected By       |
| ------------------------------ | ------------------ |
| `server_data.users[]`          | `server_data.lock` |
| `server_data.rooms[]`          | `server_data.lock` |
| `server_data.practice_rooms[]` | `server_data.lock` |
| SQLite database                | `server_data.lock` |

### 7.5 Thread Model Diagram

```plantuml
@startuml
!theme plain

rectangle "Main Thread" as Main {
    note "socket()\nbind()\nlisten()\nwhile(1) { accept() }" as MainNote
}

rectangle "Client Handler Threads" as Handlers {
    rectangle "Thread 1\nhandle_client()" as T1
    rectangle "Thread 2\nhandle_client()" as T2
    rectangle "Thread N\nhandle_client()" as TN
}

rectangle "Shared Resources" as Shared {
    database "server_data\n(in-memory)" as Data
    database "SQLite DB\n(quiz_app.db)" as DB
}

Main -down-> T1 : pthread_create()
Main -down-> T2 : pthread_create()
Main -down-> TN : pthread_create()

T1 -down-> Shared : mutex_lock()
T2 -down-> Shared : mutex_lock()
TN -down-> Shared : mutex_lock()

note right of Shared
    pthread_mutex_t lock
    protects all access
end note

@enduml
```

---

## 8️⃣ QUẢN LÝ TÀI KHOẢN, PHIÊN & PHÂN QUYỀN

### 8.1 Đăng ký tài khoản

**Flow:**

1. Client gửi: `REGISTER|username|password`
2. Server hash password bằng SHA-256
3. INSERT vào bảng `users`
4. Thêm vào `server_data.users[]` in-memory

**Code:**

```c
// server/auth.c:30-38
void hash_password(const char *password, char *hashed_output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)password, strlen(password), hash);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hashed_output + (i * 2), "%02x", hash[i]);
    }
    hashed_output[64] = '\0';  // 64 hex chars
}
```

### 8.2 Đăng nhập

**Features:**

- Kiểm tra credentials trong SQLite (username + hashed password)
- Sinh session token ngẫu nhiên (63 chars)
- Chống đăng nhập đồng thời (kiểm tra `is_online`)
- Trả về role (admin/user) cho client phân quyền

**Code:**

```c
// server/auth.c:15-24
void generate_session_token(char *token, size_t len) {
    static const char charset[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    srand(time(NULL) + rand());
    for (size_t i = 0; i < len - 1; i++) {
        token[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    token[len - 1] = '\0';
}
```

### 8.3 Session Management

| Field           | Mục đích                                |
| --------------- | --------------------------------------- |
| `session_token` | Token để xác thực các request sau login |
| `is_online`     | Flag đánh dấu user đang online          |
| `socket_fd`     | Socket descriptor của client hiện tại   |
| `last_activity` | Timestamp lần hoạt động cuối            |

### 8.4 Phân quyền (Role-based Access Control)

**Database schema:**

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    username TEXT UNIQUE,
    password TEXT,
    role TEXT DEFAULT 'user'  -- 'user' or 'admin'
);
```

**Kiểm tra quyền admin:**

```c
// server/rooms.c:24-41
char role_query[256];
snprintf(role_query, sizeof(role_query),
         "SELECT role FROM users WHERE id = %d", creator_id);

if (strcmp(role, "admin") != 0) {
    char response[] = "CREATE_ROOM_FAIL|Permission denied: Only admin\n";
    server_send(socket_fd, response);
    return;
}
```

**Client-side:**

```c
// client/ui.c:42
int is_admin = (strcmp(client.role, "admin") == 0);
// Hiển thị menu khác nhau cho admin và user
```

---

## 9️⃣ XỬ LÝ NGOẠI LỆ & ĐỘ TIN CẬY MẠNG

### 9.1 Client mất mạng khi thi

**Server detection và handling:**

```c
// server/network.c:46-63
if (n <= 0) {
    // Detect disconnect
    if (user_id > 0 && current_room_id > 0) {
        // **CRITICAL: Flush tất cả answers vào DB để user có thể RESUME**
        flush_user_answers(user_id, current_room_id);

        // KHÔNG auto-submit để user có thể resume sau
        // auto_submit_on_disconnect(user_id, current_room_id); // DISABLED
    }

    logout_user(user_id, socket_fd);  // Update is_online = 0
    break;
}
```

### 9.2 Resume sau khi reconnect

**Flow:**

1. User đăng nhập lại
2. Gửi `RESUME_EXAM|room_id`
3. Server load đáp án đã lưu từ DB
4. Trả về câu hỏi + đáp án đã chọn trước đó

### 9.3 Timeout handling

**Client socket timeout:**

```c
// client/net.c:13-20
struct timeval timeout;
timeout.tv_sec = 5;  // 5 seconds
setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
```

**Server-side room timeout:**

```c
// server/timer.c:38-56
void check_room_timeouts(void) {
    time_t now = time(NULL);
    for (int i = 0; i < server_data.room_count; i++) {
        if (room->room_status == 1) {  // STARTED
            int remaining = room->time_limit - elapsed;
            if (remaining <= 0) {
                // Auto-submit cho users đang ONLINE
                // Users offline được giữ lại để RESUME
            }
        }
    }
}
```

### 9.4 Reconnect mechanism

```c
// client/net.c:190-218
int reconnect_to_server(void) {
    // Close old socket
    if (client.socket_fd > 0) {
        close(client.socket_fd);
    }

    // Create new socket
    client.socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Connect to server
    if (connect(client.socket_fd, ...) < 0) {
        return -1;
    }
    return 0;
}
```

### 9.5 Connection status check

```c
// client/net.c:221-248
int check_connection(void) {
    // Try to peek at the socket
    char test_byte;
    ssize_t result = recv(client.socket_fd, &test_byte, 1,
                          MSG_PEEK | MSG_DONTWAIT);

    if (result == 0) {
        // Connection closed by server
        close(client.socket_fd);
        client.socket_fd = -1;
        return 0;
    } else if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;  // No data, but connection OK
        }
        return 0;  // Connection error
    }
    return 1;
}
```

### 9.6 Error handling summary

| Tình huống                 | Xử lý                             |
| -------------------------- | --------------------------------- |
| Client disconnect đột ngột | Flush answers to DB, mark offline |
| Hết thời gian thi          | Auto-submit cho online users      |
| Send failed (EPIPE)        | Close socket, show error dialog   |
| Receive timeout            | Retry hoặc hiện thông báo lỗi     |
| Server không khả dụng      | Hiện dialog, không crash          |

---

## 🔟 LƯU TRỮ DỮ LIỆU & THỐNG KÊ

### 10.1 Database Schema

**SQLite Database: `quiz_app.db`**

```sql
-- Users table
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    username TEXT UNIQUE,
    password TEXT,  -- SHA-256 hashed
    role TEXT DEFAULT 'user',
    is_online INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Exam rooms
CREATE TABLE rooms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    host_id INTEGER NOT NULL,
    duration INTEGER DEFAULT 30,  -- minutes
    is_active INTEGER DEFAULT 1,
    room_status INTEGER DEFAULT 0,  -- 0=WAITING, 1=STARTED, 2=ENDED
    exam_start_time INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(host_id) REFERENCES users(id)
);

-- Exam questions (linked to rooms)
CREATE TABLE exam_questions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id INTEGER NOT NULL,
    question_text TEXT NOT NULL,
    option_a TEXT, option_b TEXT, option_c TEXT, option_d TEXT,
    correct_answer INTEGER,  -- 0=A, 1=B, 2=C, 3=D
    difficulty TEXT DEFAULT 'Easy',
    category TEXT DEFAULT 'General',
    FOREIGN KEY(room_id) REFERENCES rooms(id)
);

-- User answers during exam
CREATE TABLE exam_answers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER,
    room_id INTEGER,
    question_id INTEGER,
    selected_answer INTEGER,
    answered_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, room_id, question_id)
);

-- Exam results
CREATE TABLE results (
    id INTEGER PRIMARY KEY,
    user_id INTEGER,
    room_id INTEGER,
    score INTEGER,
    total_questions INTEGER,
    time_taken INTEGER,  -- seconds
    completed_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Activity log for audit
CREATE TABLE activity_log (
    id INTEGER PRIMARY KEY,
    user_id INTEGER,
    action TEXT,
    details TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Practice rooms
CREATE TABLE practice_rooms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    creator_id INTEGER,
    time_limit INTEGER DEFAULT 0,  -- cooldown minutes
    show_answers INTEGER DEFAULT 0,
    is_open INTEGER DEFAULT 1,
    created_at INTEGER
);
```

### 10.2 Định dạng dữ liệu

| Loại       | Format                 | Ví dụ                    |
| ---------- | ---------------------- | ------------------------ |
| Password   | SHA-256 hex (64 chars) | `240be518fabd27...`      |
| Timestamp  | Unix epoch (INTEGER)   | `1704636123`             |
| Answer     | INTEGER (0-3)          | 0=A, 1=B, 2=C, 3=D       |
| Difficulty | TEXT                   | "easy", "medium", "hard" |

### 10.3 Thống kê điểm

**Leaderboard query:**

```c
// server/stats.c:18-23
snprintf(query, sizeof(query),
    "SELECT u.username, COALESCE(SUM(r.score), 0), COUNT(r.id) "
    "FROM users u LEFT JOIN results r ON u.id = r.user_id "
    "WHERE u.role != 'admin' "
    "GROUP BY u.id ORDER BY total_score DESC LIMIT %d", limit);
```

**User statistics:**

```c
// server/stats.c:63-67
snprintf(query, sizeof(query),
    "SELECT COUNT(id), AVG(CAST(score AS FLOAT)/total_questions), "
    "MAX(score), SUM(score) "
    "FROM results WHERE user_id = %d", user_id);
```

---

## 1️⃣1️⃣ GIAO DIỆN ĐỒ HỌA (GUI)

### 11.1 Công nghệ GUI

| Thành phần            | Chi tiết                        |
| --------------------- | ------------------------------- |
| **Framework**         | GTK+ 3.0                        |
| **Language bindings** | C (native)                      |
| **Graphics**          | Cairo (cho custom rendering)    |
| **Event handling**    | GLib main loop                  |
| **Styling**           | Inline CSS via `style_button()` |

### 11.2 Các màn hình chính

| Screen            | File            | Mô tả                                       |
| ----------------- | --------------- | ------------------------------------------- |
| Login             | `auth_ui.c`     | Form đăng nhập/đăng ký                      |
| Main Menu (User)  | `ui.c`          | Test Mode, Practice, Stats, Ranking, Logout |
| Main Menu (Admin) | `ui.c`          | Manage Exam, Manage Practice, Logout        |
| Room List         | `room_ui.c`     | Danh sách phòng thi                         |
| Exam Screen       | `exam_ui.c`     | Câu hỏi, timer, nút chọn đáp án             |
| Admin Panel       | `admin_ui.c`    | Quản lý phòng, import câu hỏi               |
| Stats Screen      | `stats_ui.c`    | Thống kê cá nhân, leaderboard               |
| Practice Screen   | `practice_ui.c` | Luyện tập với feedback ngay                 |

### 11.3 Luồng tương tác

```plantuml
@startuml
!theme plain
start
:Launch Application;
:Connect to Server;

if (Connected?) then (no)
    :Show Error Dialog;
    stop
else (yes)
endif

:Show Login Screen;

if (Login/Register?) then (register)
    :Register Form;
    :Send REGISTER;
else (login)
    :Login Form;
    :Send LOGIN;
endif

if (Success?) then (no)
    :Show Error;
    :Back to Login;
else (yes)
endif

if (Role == admin?) then (yes)
    :Show Admin Menu;
    fork
        :Manage Exam Rooms;
    fork again
        :Manage Practice;
    fork again
        :Change Password;
    end fork
else (no)
    :Show User Menu;
    fork
        :Test Mode;
        :Select Room;
        :Take Exam;
        :Submit;
        :View Score;
    fork again
        :Practice Mode;
    fork again
        :View Statistics;
    fork again
        :View Leaderboard;
    end fork
endif

:Logout;
:Reconnect;
:Back to Login;
stop
@enduml
```

---

## 1️⃣2️⃣ ĐÁNH GIÁ THEO RUBRIC CHẤM ĐIỂM

| Tiêu chí                | Điểm tối đa | Mức đạt | Nhận xét                                                |
| ----------------------- | ----------- | ------- | ------------------------------------------------------- |
| **Socket TCP/IP**       | 2.0         | ✅ 2.0  | Sử dụng POSIX socket (AF_INET, SOCK_STREAM), port 8888  |
| **Multi-threading**     | 2.0         | ✅ 2.0  | pthread cho mỗi client, mutex lock cho shared resources |
| **Protocol Design**     | 1.5         | ✅ 1.5  | Text-based protocol với pipe delimiter, 40+ commands    |
| **Database/Storage**    | 1.0         | ✅ 1.0  | SQLite với 10+ tables, transactions                     |
| **GUI**                 | 1.0         | ✅ 1.0  | GTK+ 3.0 với nhiều screens, responsive layout           |
| **Error Handling**      | 1.0         | ✅ 0.8  | Timeout, reconnect, nhưng thiếu retry logic hoàn chỉnh  |
| **Security**            | 0.5         | ✅ 0.5  | SHA-256 password hashing, session tokens                |
| **Concurrency Control** | 0.5         | ✅ 0.5  | Mutex cho tất cả shared data access                     |
| **Documentation**       | 0.5         | ⚠️ 0.3  | Comments trong code, nhưng thiếu README chi tiết        |
| **TỔNG**                | **10.0**    | **9.6** | Hoàn thành tốt yêu cầu môn học                          |

---

## 1️⃣3️⃣ PHÂN CÔNG CÔNG VIỆC

> **Assumption**: Do không có thông tin về nhóm, giả định nhóm 2-3 thành viên

| Thành viên                    | Công việc                                | Files chính                                     |
| ----------------------------- | ---------------------------------------- | ----------------------------------------------- |
| **Member 1** (Backend Lead)   | Server core, socket, threading, protocol | `quiz_server.c`, `network.c`, `auth.c`, `db.c`  |
| **Member 2** (Business Logic) | Rooms, practice, results, statistics     | `rooms.c`, `practice.c`, `results.c`, `stats.c` |
| **Member 3** (Frontend)       | GTK GUI, client networking               | `main.c`, `net.c`, `*_ui.c` files               |
| **Chung**                     | Testing, documentation, báo cáo          | `Makefile`, data files, slides                  |

---

## 1️⃣4️⃣ KẾT LUẬN & HƯỚNG PHÁT TRIỂN

### 14.1 Điểm mạnh

✅ **Kiến trúc rõ ràng**: Phân chia module hợp lý (auth, rooms, practice, stats)  
✅ **Multi-threading hiệu quả**: 1 thread/client với mutex synchronization  
✅ **Protocol design tốt**: Text-based, dễ debug, extensible  
✅ **Data persistence**: SQLite với auto-save, resume support  
✅ **Xử lý disconnect**: Flush answers trước khi mất kết nối  
✅ **Role-based access**: Phân quyền admin/user rõ ràng

### 14.2 Hạn chế

⚠️ **Không có encryption**: Dữ liệu truyền plaintext qua mạng  
⚠️ **Single server**: Không có load balancing, failover  
⚠️ **Memory limits**: In-memory arrays có giới hạn cố định (MAX_CLIENTS=100)  
⚠️ **Thiếu input validation**: Một số chỗ dùng `snprintf` với user input trực tiếp

### 14.3 Hướng phát triển

| Cải tiến                | Mô tả                              | Độ phức tạp |
| ----------------------- | ---------------------------------- | ----------- |
| **SSL/TLS**             | Mã hóa kết nối với OpenSSL         | Trung bình  |
| **REST API**            | Thêm HTTP endpoints cho mobile app | Cao         |
| **WebSocket**           | Real-time updates thay vì polling  | Trung bình  |
| **Docker**              | Container hóa server               | Thấp        |
| **Thread Pool**         | Tối ưu thread management           | Trung bình  |
| **Prepared Statements** | Chống SQL Injection                | Thấp        |
| **JWT Tokens**          | Stateless authentication           | Trung bình  |
| **Redis Cache**         | Tăng tốc read operations           | Cao         |

---

## 📎 PHỤ LỤC

### A. Hướng dẫn biên dịch

**Server:**

```bash
cd server
make clean
make
./quiz_server
```

**Client:**

```bash
cd client
make clean
make
./quiz_client
```

### B. Cấu hình mạng

| Tham số   | Giá trị               | File                             |
| --------- | --------------------- | -------------------------------- |
| Server IP | 127.0.0.1 (localhost) | `client/include/client_common.h` |
| Port      | 8888                  | `server/include/common.h`        |
| Timeout   | 5 seconds             | `client/net.c`                   |

### C. Tài khoản mặc định

| Username | Password | Role          |
| -------- | -------- | ------------- |
| admin    | admin123 | Administrator |
| admin2   | admin123 | Administrator |

---

**📅 Ngày tạo báo cáo**: 07/01/2026  
**🔧 Công cụ phân tích**: Manual code review + Static analysis
