#include "questions.h"
#include "db.h"
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

extern ServerData server_data;
extern sqlite3 *db;

void handle_get_user_rooms(int client_socket, int user_id)
{
    char query[256];
    snprintf(query, sizeof(query), 
             "SELECT id, name FROM rooms WHERE host_id = %d AND is_active = 1", 
             user_id);
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) {
        send(client_socket, "ERROR|Database error\n", 21, 0);
        return;
    }
    
    char response[4096] = "ROOMS_LIST";
    int has_rooms = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        has_rooms = 1;
        int room_id = sqlite3_column_int(stmt, 0);
        const char *room_name = (const char *)sqlite3_column_text(stmt, 1);
        
        char room_entry[256];
        snprintf(room_entry, sizeof(room_entry), "|%d:%s", room_id, room_name);
        strcat(response, room_entry);
    }
    
    sqlite3_finalize(stmt);
    
    if (!has_rooms) {
        send(client_socket, "NO_ROOMS\n", 9, 0);
    } else {
        strcat(response, "\n");
        send(client_socket, response, strlen(response), 0);
    }
}

void handle_add_question(int client_socket, char *data)
{
    pthread_mutex_lock(&server_data.lock);
    
    // Parse: user_id|room_id|question|opt1|opt2|opt3|opt4
    char *user_id_str = strtok(data, "|");
    if (!user_id_str) {
        send(client_socket, "ERROR|Invalid data format\n", 27, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    int user_id = atoi(user_id_str);
    
    // Kiểm tra role - chỉ admin mới được thêm câu hỏi
    char role_query[256];
    snprintf(role_query, sizeof(role_query),
             "SELECT role FROM users WHERE id = %d", user_id);
    
    sqlite3_stmt *stmt_role;
    if (sqlite3_prepare_v2(db, role_query, -1, &stmt_role, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt_role) == SQLITE_ROW) {
            const char *role = (const char *)sqlite3_column_text(stmt_role, 0);
            if (role && strcmp(role, "admin") != 0) {
                send(client_socket, "ERROR|Permission denied: Only admin can add questions\n", 55, 0);
                sqlite3_finalize(stmt_role);
                pthread_mutex_unlock(&server_data.lock);
                return;
            }
        }
        sqlite3_finalize(stmt_role);
    }
    
    // Parse: room_id|question|opt1|opt2|opt3|opt4|correct|difficulty|category
    char *room_id_str = strtok(NULL, "|");
    char *question = strtok(NULL, "|");
    char *opt1 = strtok(NULL, "|");
    char *opt2 = strtok(NULL, "|");
    char *opt3 = strtok(NULL, "|");
    char *opt4 = strtok(NULL, "|");
    char *correct_str = strtok(NULL, "|");
    char *difficulty = strtok(NULL, "|");
    char *category = strtok(NULL, "|");
    
    if (!room_id_str || !question || !opt1 || !opt2 || !opt3 || !opt4 || !correct_str || !difficulty || !category) {
        send(client_socket, "ERROR|Invalid data\n", 19, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }

    int room_id = atoi(room_id_str);
    int correct_answer = atoi(correct_str);
    
    // Validate correct_answer (0-3)
    if (correct_answer < 0 || correct_answer > 3) {
        send(client_socket, "ERROR|Invalid correct answer\n", 30, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    // Escape strings for SQL safety using sqlite3_mprintf
    char *query = sqlite3_mprintf(
        "INSERT INTO questions (room_id, question_text, option_a, option_b, option_c, option_d, correct_answer, difficulty, category) "
        "VALUES (%d, %Q, %Q, %Q, %Q, %Q, %d, %Q, %Q)",
        room_id, question, opt1, opt2, opt3, opt4, correct_answer, difficulty, category);
    
    if (!query) {
        send(client_socket, "ERROR|Memory allocation failed\n", 32, 0);
        pthread_mutex_unlock(&server_data.lock);
        return;
    }
    
    if (sqlite3_exec(db, query, NULL, NULL, NULL) == SQLITE_OK) {
        send(client_socket, "QUESTION_ADDED\n", 15, 0);
    } else {
        send(client_socket, "ERROR|Failed to insert\n", 23, 0);
    }
    sqlite3_free(query);
    pthread_mutex_unlock(&server_data.lock);
}

// void get_question(int socket_fd, int room_id, int question_num)
// {
//   pthread_mutex_lock(&server_data.lock);

//   if (question_num >= server_data.question_count)
//   {
//     char response[] = "GET_QUESTION_FAIL|Question not found\n";
//     send(socket_fd, response, strlen(response), 0);
//     pthread_mutex_unlock(&server_data.lock);
//     return;
//   }

//   Question *q = &server_data.questions[question_num];
//   char response[4096];

//   snprintf(response, sizeof(response),
//            "GET_QUESTION_OK|%d|%s|%s|%s|%s|%s\n",
//            q->id, q->text, q->options[0], q->options[1], q->options[2], q->options[3]);

//   send(socket_fd, response, strlen(response), 0);
//   pthread_mutex_unlock(&server_data.lock);
// }

// void practice_mode(int socket_fd, int user_id, char *difficulty, char *category, int num_q)
// {
//   pthread_mutex_lock(&server_data.lock);

//   int count = 0;
//   char response[8192];
//   strcpy(response, "PRACTICE_MODE|");

//   for (int i = 0; i < server_data.question_count && count < num_q; i++)
//   {
//     Question *q = &server_data.questions[i];

//     if ((difficulty[0] == '*' || strcmp(q->difficulty, difficulty) == 0) &&
//         (category[0] == '*' || strcmp(q->category, category) == 0))
//     {
//       char q_info[500];
//       snprintf(q_info, sizeof(q_info),
//                "%d|%s|%s|%s|%s|%s|",
//                q->id, q->text, q->options[0], q->options[1],
//                q->options[2], q->options[3]);
//       strcat(response, q_info);
//       count++;
//     }
//   }

//   strcat(response, "\n");
//   send(socket_fd, response, strlen(response), 0);
//   log_activity(user_id, "PRACTICE_MODE", "Entered practice mode");
//   pthread_mutex_unlock(&server_data.lock);
// }

// void load_sample_questions()
// {
//   Question q1 = {1, "2 + 2 = ?", {"3", "4", "5", "6"}, 1, "easy", "math"};
//   Question q2 = {2, "What is capital of France?", {"London", "Paris", "Berlin", "Madrid"}, 1, "easy", "geography"};
//   Question q3 = {3, "What is 15 * 8?", {"100", "110", "120", "130"}, 2, "medium", "math"};
//   Question q4 = {4, "Which planet is closest to sun?", {"Venus", "Mercury", "Earth", "Mars"}, 1, "medium", "science"};
//   Question q5 = {5, "What is the integral of sin(x)?", {"-cos(x)", "cos(x)", "sin(x)", "-sin(x)"}, 0, "hard", "math"};

//   server_data.questions[0] = q1;
//   server_data.questions[1] = q2;
//   server_data.questions[2] = q3;
//   server_data.questions[3] = q4;
//   server_data.questions[4] = q5;

//   server_data.question_count = 5;
//   printf("Sample questions loaded\n");
// }

// void add_question_to_bank(int creator_id, const char *question_text,
//                           const char *opt_a, const char *opt_b,
//                           const char *opt_c, const char *opt_d,
//                           int correct, const char *difficulty,
//                           const char *category)
// {
//   char query[800];
//   snprintf(query, sizeof(query),
//            "INSERT INTO questions (question_text, option_a, option_b, option_c, option_d, correct_answer, difficulty, category) VALUES ('%s', '%s', '%s', '%s', '%s', %d, '%s', '%s');",
//            question_text, opt_a, opt_b, opt_c, opt_d, correct, difficulty, category);

//   char *err_msg = 0;
//   sqlite3_exec(db, query, 0, 0, &err_msg);
// }

// void get_question_bank(int socket_fd, const char *difficulty, const char *category)
// {
//   char query[400];
//   if (strcmp(difficulty, "*") == 0 && strcmp(category, "*") == 0)
//   {
//     strcpy(query, "SELECT id, question_text, difficulty, category FROM questions;");
//   }
//   else if (strcmp(difficulty, "*") == 0)
//   {
//     snprintf(query, sizeof(query),
//              "SELECT id, question_text, difficulty, category FROM questions WHERE category='%s';", category);
//   }
//   else if (strcmp(category, "*") == 0)
//   {
//     snprintf(query, sizeof(query),
//              "SELECT id, question_text, difficulty, category FROM questions WHERE difficulty='%s';", difficulty);
//   }
//   else
//   {
//     snprintf(query, sizeof(query),
//              "SELECT id, question_text, difficulty, category FROM questions WHERE difficulty='%s' AND category='%s';", difficulty, category);
//   }

//   sqlite3_stmt *stmt;
//   if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK)
//   {
//     char response[4096];
//     strcpy(response, "QUESTION_BANK|");

//     while (sqlite3_step(stmt) == SQLITE_ROW)
//     {
//       int id = sqlite3_column_int(stmt, 0);
//       const char *text = (const char *)sqlite3_column_text(stmt, 1);
//       const char *diff = (const char *)sqlite3_column_text(stmt, 2);
//       const char *cat = (const char *)sqlite3_column_text(stmt, 3);

//       char entry[500];
//       snprintf(entry, sizeof(entry), "%d|%s|%s|%s|", id, text, diff, cat);
//       strcat(response, entry);
//     }

//     strcat(response, "\n");
//     send(socket_fd, response, strlen(response), 0);
//   }

//   sqlite3_finalize(stmt);
// }

int import_questions_from_csv(const char *filename, int room_id)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return -1;
    }

    char line[2048];
    int imported = 0;
    
    // Skip header
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }

    pthread_mutex_lock(&server_data.lock);
    
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        
        char q_text[512], opt_a[128], opt_b[128], opt_c[128], opt_d[128];
        char difficulty[32], category[64];
        int correct;
        
        // Parse CSV: question,optA,optB,optC,optD,correct(0-3),difficulty,category
        char *token = strtok(line, ",");
        if (!token) continue;
        strncpy(q_text, token, sizeof(q_text)-1);
        q_text[sizeof(q_text)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(opt_a, token, sizeof(opt_a)-1);
        opt_a[sizeof(opt_a)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(opt_b, token, sizeof(opt_b)-1);
        opt_b[sizeof(opt_b)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(opt_c, token, sizeof(opt_c)-1);
        opt_c[sizeof(opt_c)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(opt_d, token, sizeof(opt_d)-1);
        opt_d[sizeof(opt_d)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        correct = atoi(token);
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(difficulty, token, sizeof(difficulty)-1);
        difficulty[sizeof(difficulty)-1] = '\0';
        
        token = strtok(NULL, ",");
        if (!token) continue;
        strncpy(category, token, sizeof(category)-1);
        category[sizeof(category)-1] = '\0';
        
        // Escape single quotes for SQL safety using sqlite3_mprintf
        char escaped_diff[64], escaped_cat[128];
        sqlite3_snprintf(sizeof(escaped_diff), escaped_diff, "%q", difficulty);
        sqlite3_snprintf(sizeof(escaped_cat), escaped_cat, "%q", category);
        
        char *query = sqlite3_mprintf(
            "INSERT INTO questions (room_id, question_text, option_a, option_b, option_c, option_d, correct_answer, difficulty, category) "
            "VALUES (%d, %Q, %Q, %Q, %Q, %Q, %d, '%s', '%s');",
            room_id, q_text, opt_a, opt_b, opt_c, opt_d, correct, escaped_diff, escaped_cat);
        
        if (!query) continue;
        
        if (sqlite3_exec(db, query, NULL, NULL, NULL) == SQLITE_OK) {
            imported++;
        }
        sqlite3_free(query);
    }
    
    pthread_mutex_unlock(&server_data.lock);
    fclose(fp);
    printf("[INFO] Imported %d questions from %s to room_id=%d\n", imported, filename, room_id);
    return imported;
}

void handle_import_csv(int client_socket, char *data)
{
    // Strip newline characters first
    data[strcspn(data, "\r\n")] = '\0';
    
    // Parse: room_id|filename|file_size
    char *pipe1 = strchr(data, '|');
    if (!pipe1) {
        send(client_socket, "ERROR|Invalid format. Expected: room_id|filename|file_size\n", 62, 0);
        return;
    }
    
    *pipe1 = '\0';
    char *room_id_str = data;
    
    char *pipe2 = strchr(pipe1 + 1, '|');
    if (!pipe2) {
        send(client_socket, "ERROR|Invalid format. Expected: room_id|filename|file_size\n", 62, 0);
        return;
    }
    
    *pipe2 = '\0';
    char *filename = pipe1 + 1;
    char *size_str = pipe2 + 1;
    
    if (strlen(room_id_str) == 0 || strlen(filename) == 0 || strlen(size_str) == 0) {
        send(client_socket, "ERROR|Invalid format\n", 22, 0);
        return;
    }
    
    int room_id = atoi(room_id_str);
    long file_size = atol(size_str);
    
    // Validate file size (max 5MB)
    if (file_size <= 0 || file_size > 5 * 1024 * 1024) {
        send(client_socket, "ERROR|File size invalid (max 5MB)\n", 36, 0);
        return;
    }
    
    printf("[INFO] Receiving CSV upload: %s (%ld bytes) for room_id=%d\n", 
           filename, file_size, room_id);
    
    // Send ACK to client to confirm ready to receive
    char ack[] = "READY\n";
    send(client_socket, ack, strlen(ack), 0);
    
    // Receive file content
    char *file_buffer = malloc(file_size + 1);
    if (!file_buffer) {
        send(client_socket, "ERROR|Memory allocation failed\n", 32, 0);
        return;
    }
    
    ssize_t total_received = 0;
    int retry_count = 0;
    const int MAX_RETRIES = 10;
    
    while (total_received < file_size) {
        ssize_t n = recv(client_socket, file_buffer + total_received, 
                         file_size - total_received, 0);
        if (n > 0) {
            total_received += n;
            printf("[DEBUG] Received %ld bytes (total: %ld/%ld)\n", 
                   n, total_received, file_size);
            retry_count = 0; // Reset retry on success
        } else if (n == 0) {
            printf("[ERROR] Connection closed by client\n");
            free(file_buffer);
            send(client_socket, "ERROR|Connection closed\n", 25, 0);
            return;
        } else {
            // n < 0 - error
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout - retry
                retry_count++;
                if (retry_count > MAX_RETRIES) {
                    printf("[ERROR] Timeout after %d retries\n", MAX_RETRIES);
                    free(file_buffer);
                    send(client_socket, "ERROR|Upload timeout\n", 22, 0);
                    return;
                }
                usleep(100000); // Sleep 100ms before retry
                continue;
            } else {
                printf("[ERROR] recv() failed: %s\n", strerror(errno));
                free(file_buffer);
                send(client_socket, "ERROR|File upload failed\n", 26, 0);
                return;
            }
        }
    }
    file_buffer[file_size] = '\0';
    
    printf("[INFO] Received %ld bytes successfully\n", total_received);
    
    // Create temp file with unique name
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/csv_import_%d_%ld.csv", 
             room_id, (long)time(NULL));
    
    FILE *fp = fopen(temp_path, "w");
    if (!fp) {
        free(file_buffer);
        send(client_socket, "ERROR|Cannot create temp file\n", 32, 0);
        return;
    }
    
    fwrite(file_buffer, 1, file_size, fp);
    fclose(fp);
    free(file_buffer);
    
    printf("[INFO] Saved to temp file: %s\n", temp_path);
    
    // Import from temp file
    int imported = import_questions_from_csv(temp_path, room_id);
    
    // Delete temp file
    unlink(temp_path);
    printf("[INFO] Cleaned up temp file\n");
    
    if (imported < 0) {
        send(client_socket, "ERROR|Import failed\n", 21, 0);
    } else {
        char response[128];
        snprintf(response, sizeof(response), "IMPORT_OK|%d\n", imported);
        send(client_socket, response, strlen(response), 0);
        printf("[INFO] Import completed: %d questions\n", imported);
    }
}