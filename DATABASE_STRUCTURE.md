# Cấu trúc Database mới - NetworkExamOnline

## Tóm tắt thay đổi

Database đã được cấu trúc lại để **tách biệt hoàn toàn** câu hỏi của Exam và Practice mode:

### Trước đây (Cũ):
- **1 bảng duy nhất**: `questions` - chứa tất cả câu hỏi cho cả exam và practice
- **Vấn đề**: Khi sửa/xóa câu hỏi ở một mode, có thể ảnh hưởng đến mode kia

### Bây giờ (Mới):
- **2 bảng riêng biệt**:
  - `exam_questions` - Chỉ cho exam rooms
  - `practice_questions` - Chỉ cho practice rooms
- **Lợi ích**: Sửa/xóa câu hỏi ở Exam không ảnh hưởng Practice và ngược lại

---

## Chi tiết cấu trúc

### 1. Bảng `exam_questions` (Câu hỏi Exam)
```sql
CREATE TABLE exam_questions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id INTEGER DEFAULT 0,              -- Nếu 0: chưa gán phòng, nếu >0: đã gán vào room
    question_text TEXT NOT NULL,
    option_a TEXT,
    option_b TEXT,
    option_c TEXT,
    option_d TEXT,
    correct_answer INTEGER,                 -- 0-3 tương ứng A-D
    difficulty TEXT,                        -- 'easy', 'medium', 'hard'
    category TEXT,
    FOREIGN KEY(room_id) REFERENCES rooms(id)
);
```

**Được sử dụng bởi**:
- `rooms.c` - Tạo exam room, assign questions, start exam
- `questions.c` - Import CSV, add questions cho exam
- Khi admin tạo exam room, questions được chọn từ bảng này (WHERE room_id = 0)

### 2. Bảng `practice_questions` (Câu hỏi Practice)
```sql
CREATE TABLE practice_questions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    question_text TEXT NOT NULL,
    option_a TEXT,
    option_b TEXT,
    option_c TEXT,
    option_d TEXT,
    correct_answer INTEGER,                 -- 0-3 tương ứng A-D
    difficulty TEXT,
    category TEXT
);
```

**Được sử dụng bởi**:
- `practice.c` - Tạo practice room, add/edit/delete practice questions
- Practice questions được link với practice rooms qua bảng `practice_room_questions`

### 3. Bảng mapping `practice_room_questions`
```sql
CREATE TABLE practice_room_questions (
    practice_id INTEGER,
    question_id INTEGER,
    question_order INTEGER,
    PRIMARY KEY(practice_id, question_id),
    FOREIGN KEY(practice_id) REFERENCES practice_rooms(id),
    FOREIGN KEY(question_id) REFERENCES practice_questions(id)
);
```

---

## Luồng hoạt động

### Exam Room:
1. Admin tạo exam room → chọn số câu hỏi easy/medium/hard
2. Server SELECT từ `exam_questions` WHERE room_id = 0 (chưa được dùng)
3. UPDATE `exam_questions` SET room_id = [room_id] (assign vào room)
4. Khi exam kết thúc, có thể giữ nguyên hoặc reset room_id = 0

### Practice Room:
1. Admin tạo practice room → add questions
2. Questions được INSERT vào `practice_questions`
3. Mapping được INSERT vào `practice_room_questions`
4. Khi user vào practice, SELECT từ `practice_questions` JOIN với mapping

---

## Migration từ database cũ

Nếu bạn có database cũ với bảng `questions`:

```sql
-- Backup bảng cũ
CREATE TABLE questions_backup AS SELECT * FROM questions;

-- Migrate exam questions (những câu đã gán room_id)
INSERT INTO exam_questions 
SELECT * FROM questions WHERE room_id > 0;

-- Migrate practice questions (nếu có)
-- Cần xác định câu nào là practice dựa vào logic cũ
-- Hoặc import lại từ CSV

-- Sau khi migrate xong, có thể DROP bảng questions cũ (cẩn thận!)
-- DROP TABLE questions;
```

---

## API Changes (Server-side)

### Files đã được cập nhật:

1. **server/practice.c**
   - `init_practice_tables()` - Tạo bảng practice_questions
   - `get_practice_questions()` - SELECT từ practice_questions
   - `update_practice_question()` - UPDATE practice_questions
   - `delete_practice_room()` - DELETE từ practice_questions và mapping

2. **server/rooms.c**
   - Tất cả queries đã đổi từ `questions` → `exam_questions`
   - `assign_random_questions()` - SELECT/UPDATE exam_questions
   - `get_room_questions()` - SELECT từ exam_questions
   - `update_room_question()` - UPDATE exam_questions

3. **server/questions.c**
   - `add_question()` - INSERT vào exam_questions
   - `import_questions_from_csv()` - INSERT vào exam_questions

4. **server/db.c**
   - `init_database()` - Tạo cả 2 bảng exam_questions và practice_questions

---

## Client UI (Admin)

Giao diện admin đã được đơn giản hóa:

### Exam Management:
- ✅ **Create Room** - Tạo phòng thi mới
- ✅ **Manage Room** - Xem/sửa/xóa exam rooms

### Practice Management:
- ✅ **Create Practice** - Tạo phòng luyện tập mới
- ✅ **Manage Practice** - Xem/sửa/xóa practice rooms

### Tính năng bổ sung:
- ✅ **Change Password** - Đổi mật khẩu cho admin/user
- ✅ **View/Edit Questions** - Sửa câu hỏi trong exam/practice
- ✅ **Delete Practice Room** - Xóa practice room
- ✅ **View Exam Students** - Xem trạng thái học sinh trong exam

---

## Compilation Status

✅ **Server**: Compiled successfully
- 2 warnings (unused variables) - không ảnh hưởng chức năng

✅ **Client**: Compiled successfully  
- Một số warnings về format truncation - không ảnh hưởng chức năng

---

## Testing Checklist

Sau khi cập nhật, cần test:

- [ ] Tạo exam room mới và assign questions từ exam_questions
- [ ] Tạo practice room mới và add questions vào practice_questions
- [ ] Sửa câu hỏi trong exam room → chỉ ảnh hưởng exam_questions
- [ ] Sửa câu hỏi trong practice room → chỉ ảnh hưởng practice_questions
- [ ] Xóa practice room → questions trong practice_questions bị xóa theo
- [ ] Import CSV vào exam room → INSERT vào exam_questions
- [ ] Start exam và kiểm tra questions hiển thị đúng
- [ ] Join practice room và kiểm tra questions hiển thị đúng

---

**Cập nhật lần cuối**: 2024
**Phiên bản**: 2.0 (Database restructure)
