<div align="center">

# 📝 Online Quiz System

### Hệ thống thi trắc nghiệm trực tuyến

[![C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![GTK+](https://img.shields.io/badge/GUI-GTK%2B%203.0-green.svg)](https://www.gtk.org/)
[![SQLite](https://img.shields.io/badge/Database-SQLite-lightgrey.svg)](https://www.sqlite.org/)
[![TCP/IP](https://img.shields.io/badge/Protocol-TCP%2FIP-orange.svg)](https://en.wikipedia.org/wiki/Internet_protocol_suite)

*Đồ án môn Lập trình mạng - Hệ thống thi trắc nghiệm Client-Server đa luồng*

---

</div>

## 🎯 Giới thiệu

**Online Quiz System** là hệ thống thi trắc nghiệm trực tuyến hoàn chỉnh được xây dựng theo mô hình **Client-Server**, sử dụng **TCP/IP Socket** và **Multi-threading**. Hệ thống hỗ trợ nhiều thí sinh thi đồng thời, quản lý phòng thi, chấm điểm tự động và thống kê kết quả.

### ✨ Tính năng nổi bật

| Tính năng | Mô tả |
|-----------|-------|
| 🏠 **Phòng thi trực tuyến** | Tạo và quản lý nhiều phòng thi đồng thời |
| 📚 **Chế độ luyện tập** | Practice mode với hiển thị đáp án ngay lập tức |
| ⏱️ **Quản lý thời gian** | Countdown timer, tự động nộp bài khi hết giờ |
| 📊 **Thống kê & Xếp hạng** | Leaderboard, lịch sử thi, thống kê cá nhân |
| 🔒 **Bảo mật** | Mật khẩu hash SHA-256, Session token, phân quyền Admin/User |
| 🔄 **Tự động khôi phục** | Resume bài thi khi mất kết nối mạng |
| 📝 **Logging** | Tự động ghi log ra file `server.log` và `client.log` để debug |
| 💾 **Dynamic Memory** | Hỗ trợ unlimited câu hỏi với dynamic allocation |

---

## 🏗️ Kiến trúc hệ thống

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           ONLINE QUIZ SYSTEM                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   ┌─────────────┐                              ┌─────────────────────┐  │
│   │   CLIENT    │         TCP/IP               │      SERVER         │  │
│   │             │        Port 8888             │                     │  │
│   │ ┌─────────┐ │                              │  ┌───────────────┐  │  │
│   │ │ GTK+ 3.0│ │  ◄──────────────────────────►│  │ TCP Listener  │  │  │
│   │ │   GUI   │ │    Text-based Protocol       │  │ Multi-thread  │  │  │
│   │ └─────────┘ │    (Pipe-delimited)          │  └───────────────┘  │  │
│   │             │                              │         │           │  │
│   │ ┌─────────┐ │                              │         ▼           │  │
│   │ │ Network │ │                              │  ┌───────────────┐  │  │
│   │ │ Module  │ │                              │  │Business Logic │  │  │
│   │ └─────────┘ │                              │  └───────────────┘  │  │
│   │             │                              │         │           │  │
│   └─────────────┘                              │         ▼           │  │
│         ▲                                      │  ┌───────────────┐  │  │
│         │                                      │  │    SQLite     │  │  │
│         └──────── Multiple Clients ────────────│  │   Database    │  │  │
│                   (Max: 100)                   │  └───────────────┘  │  │
│                                                │                     │  │
│                                                └─────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 🛠️ Công nghệ sử dụng

<table>
  <tr>
    <th>Thành phần</th>
    <th>Công nghệ</th>
    <th>Chi tiết</th>
  </tr>
  <tr>
    <td>🖥️ <b>Ngôn ngữ</b></td>
    <td>C (GNU C99)</td>
    <td>Hiệu năng cao, kiểm soát bộ nhớ</td>
  </tr>
  <tr>
    <td>🌐 <b>Network</b></td>
    <td>POSIX Socket (TCP)</td>
    <td>AF_INET, SOCK_STREAM</td>
  </tr>
  <tr>
    <td>🧵 <b>Threading</b></td>
    <td>pthread</td>
    <td>1 thread per client, mutex sync</td>
  </tr>
  <tr>
    <td>💾 <b>Database</b></td>
    <td>SQLite 3</td>
    <td>Embedded, hỗ trợ transaction</td>
  </tr>
  <tr>
    <td>🎨 <b>GUI</b></td>
    <td>GTK+ 3.0</td>
    <td>Cross-platform, Cairo rendering</td>
  </tr>
  <tr>
    <td>🔐 <b>Security</b></td>
    <td>OpenSSL SHA-256</td>
    <td>Password hashing</td>
  </tr>
</table>

---

## 📁 Cấu trúc thư mục

```
NetworkExamOnline/
├── 📂 server/                          # Server Application
│   ├── 📂 include/
│   │   └── common.h                    # Shared definitions & data structures
│   ├── quiz_server.c                   # Main entry point, TCP listener
│   ├── network.c/.h                    # Protocol handler, message router
│   ├── auth.c/.h                       # Authentication (login, register)
│   ├── db.c/.h                         # Database initialization & queries
│   ├── rooms.c/.h                      # Exam room management
│   ├── practice.c/.h                   # Practice mode logic
│   ├── questions.c/.h                  # Question bank management
│   ├── results.c/.h                    # Answer saving & scoring
│   ├── stats.c/.h                      # Statistics & leaderboard
│   ├── admin.c/.h                      # Admin-specific functions
│   ├── timer.c/.h                      # Room timeout checking
│   └── Makefile
│
├── 📂 client/                          # Client Application
│   ├── 📂 include/
│   │   └── client_common.h             # Client data structures
│   ├── main.c                          # Entry point, GTK init
│   ├── net.c/.h                        # Socket communication
│   ├── ui.c/.h                         # Main menu logic
│   ├── ui_utils.c/.h                   # UI helper functions
│   ├── auth_ui.c/.h                    # Login/Register screens
│   ├── room_ui.c/.h                    # Room list & selection
│   ├── exam_ui.c/.h                    # Exam taking interface
│   ├── practice_ui.c/.h                # Practice mode UI
│   ├── stats_ui.c/.h                   # Statistics display
│   ├── admin_ui.c/.h                   # Admin panel
│   ├── question_ui.c/.h                # Question management UI
│   ├── password_ui.c/.h                # Change password dialog
│   ├── broadcast.c/.h                  # Handle server broadcasts
│   ├── exam_room_creator.c/.h          # Room creation wizard
│   └── Makefile
│
├── 📂 data/                            # Sample data files
│   ├── question_5.csv                  # 5 câu hỏi mẫu (demo)
│   ├── question_30.csv                 # 30 câu hỏi (bài tập)
│   └── question_100.csv                # 100 câu hỏi (thi thật)
│
├── online_exam_system_report.md        # Báo cáo kỹ thuật chi tiết
└── README.md
```

---

## 🚀 Hướng dẫn cài đặt

### Yêu cầu hệ thống

- **OS**: Linux (Ubuntu 18.04+)
- **Compiler**: GCC 7.0+
- **Libraries**: GTK+ 3.0, SQLite 3, OpenSSL

### Cài đặt dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential libgtk-3-dev libsqlite3-dev libssl-dev

# Fedora/RHEL
sudo dnf install gcc gtk3-devel sqlite-devel openssl-devel
```

### Build & Run

#### 1️⃣ Build Server

```bash
cd server
make clean && make
```

#### 2️⃣ Run Server

```bash
./quiz_server
# Server listening on port 8888...
# Logs được ghi vào file server.log
```

#### 3️⃣ Build Client

```bash
cd client
make clean && make
```

#### 4️⃣ Run Client

```bash
./quiz_client
# Kết nối tới localhost:8888
# Logs được ghi vào file client.log
```

### 🔑 Tài khoản demo

| Username | Password | Role | Mô tả |
|----------|----------|------|-------|
| `admin` | `admin123` | Administrator | Tạo/quản lý phòng thi, import câu hỏi |
| `admin2` | `admin123` | Administrator | Tài khoản admin backup |

> 💡 **Tip**: Đăng ký tài khoản mới với role `user` để trải nghiệm tính năng thi thử!

---

## 📋 Giao thức truyền thông

Hệ thống sử dụng giao thức **text-based**, **pipe-delimited** trên nền TCP:

```
COMMAND|arg1|arg2|...|argN\n
```

### Ví dụ các message

| Message | Mô tả | Ví dụ |
|---------|-------|-------|
| `LOGIN` | Đăng nhập | `LOGIN\|john\|123456\n` |
| `REGISTER` | Đăng ký | `REGISTER\|jane\|pass123\n` |
| `JOIN_ROOM` | Vào phòng thi | `JOIN_ROOM\|5\n` |
| `SAVE_ANSWER` | Lưu đáp án | `SAVE_ANSWER\|5\|3\|A\n` |
| `SUBMIT_TEST` | Nộp bài | `SUBMIT_TEST\|5\n` |

---

## 👥 Đội ngũ phát triển

<table align="center">
  <tr>
    <td align="center">
      <img src="https://via.placeholder.com/100" width="100px;" alt=""/>
      <br />
      <sub><b>Nguyễn Hoài Nam</b></sub>
      <br />
      <sub>MSSV: 20225653</sub>
      <br />
      <sub>🔧 Server Core</sub>
    </td>
    <td align="center">
      <img src="https://via.placeholder.com/100" width="100px;" alt=""/>
      <br />
      <sub><b>Nguyễn Đình Thành</b></sub>
      <br />
      <sub>MSSV: 20225670</sub>
      <br />
      <sub>🎨 Client Core</sub>
    </td>
  </tr>
</table>

<div align="center">

**Trường Công nghệ Thông tin và Truyền thông**

**Đại học Bách khoa Hà Nội**

*Hanoi University of Science and Technology (HUST)*

</div>

---

## 📄 Tài liệu

- 📖 [Báo cáo kỹ thuật chi tiết](./online_exam_system_report.md)

---

## 📝 License

Dự án này được phát triển cho mục đích học tập trong khuôn khổ môn **Lập trình mạng**.

---

<div align="center">

**⭐ Nếu dự án hữu ích, hãy cho chúng tôi một Star! ⭐**

Made with ❤️ by Team 8 - SoICT HUST

</div>
