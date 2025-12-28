#include "net.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>

#include "include/client_common.h"
#include "cJSON.h"
#include <pthread.h>
#include <glib.h>
#include "ui.h"
#include "exam_ui.h"
#include "ui_utils.h"

// Enable network debug by default so send/receive are visible in terminal
static int net_debug_enabled = 1;  // 1: bật, 0: tắt

// Hàm bật/tắt debug
void net_set_debug(int enabled) {
    net_debug_enabled = enabled;
}

static void print_debug(const char *direction, const char *message, int strip_newline) {
    if (!net_debug_enabled) return;
    
    // Tạo bản sao để không làm hỏng message gốc
    char debug_msg[1024];
    strncpy(debug_msg, message, sizeof(debug_msg) - 1);
    debug_msg[sizeof(debug_msg) - 1] = '\0';
    
    // Loại bỏ newline nếu cần
    if (strip_newline) {
        size_t len = strlen(debug_msg);
        while (len > 0 && (debug_msg[len - 1] == '\n' || debug_msg[len - 1] == '\r')) {
            debug_msg[len - 1] = '\0';
            len--;
        }
    }
    
    fprintf(stderr, "%s %s\n", direction, debug_msg);
}

void net_set_timeout(int sockfd) {
    struct timeval timeout;
    timeout.tv_sec = 5;   
    timeout.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

extern ClientData client;

typedef struct {
    int room_id;
    int question_id;
    int selected;
    time_t ts;
} PendingAnswer;

static PendingAnswer pending_answers[256];
static int pending_count = 0;
static pthread_mutex_t pending_lock = PTHREAD_MUTEX_INITIALIZER;

int send_json_message(const char *json) {
    if (client.socket_fd > 0) {
        // validate JSON before sending
        cJSON *root = cJSON_Parse(json);
        if (!root) {
            if (net_debug_enabled) fprintf(stderr, "Invalid JSON, not sent\n");
            return -1;
        }
        char *out = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!out) return -1;
        if (net_debug_enabled) print_debug("SendJSON:", out, 1);
        ssize_t s = send(client.socket_fd, out, strlen(out), 0);
        free(out);
        return (s > 0) ? 1 : -1;
    }
    return -1;
}

void add_pending_answer(int room_id, int question_id, int selected) {
    pthread_mutex_lock(&pending_lock);
    if (pending_count < (int)(sizeof(pending_answers)/sizeof(pending_answers[0]))) {
        pending_answers[pending_count].room_id = room_id;
        pending_answers[pending_count].question_id = question_id;
        pending_answers[pending_count].selected = selected;
        pending_answers[pending_count].ts = time(NULL);
        pending_count++;
        if (net_debug_enabled) fprintf(stderr, "[PENDING] Stored answer r:%d q:%d s:%d (count=%d)\n", room_id, question_id, selected, pending_count);
    }
    pthread_mutex_unlock(&pending_lock);
}

void flush_pending_answers(void) {
    pthread_mutex_lock(&pending_lock);
    if (pending_count == 0) { pthread_mutex_unlock(&pending_lock); return; }
    if (client.socket_fd <= 0) { pthread_mutex_unlock(&pending_lock); return; }

    for (int i = 0; i < pending_count; ) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "SAVE_ANSWER");
        cJSON_AddNumberToObject(root, "user_id", client.user_id);
        cJSON_AddNumberToObject(root, "room_id", pending_answers[i].room_id);
        cJSON_AddNumberToObject(root, "question_id", pending_answers[i].question_id);
        cJSON_AddNumberToObject(root, "selected", pending_answers[i].selected);
        cJSON_AddNumberToObject(root, "ts", (long)pending_answers[i].ts);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        int ok = send_json_message(json);
        free(json);
        if (ok > 0) {
            // remove this entry by shifting
            for (int j = i; j < pending_count - 1; j++) pending_answers[j] = pending_answers[j+1];
            pending_count--;
            if (net_debug_enabled) fprintf(stderr, "[PENDING] Flushed one pending answer (remaining=%d)\n", pending_count);
        } else {
            // stop trying if send fails
            break;
        }
    }
    pthread_mutex_unlock(&pending_lock);
}

// Flush any pending data in socket buffer to avoid stale responses
void flush_socket_buffer(int sockfd) {
    if (sockfd <= 0) return;
    
    // Set socket to non-blocking temporarily
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // 10ms timeout
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    char trash[4096];
    int flushed = 0;
    while (recv(sockfd, trash, sizeof(trash), 0) > 0) {
        flushed++;
        if (flushed > 10) break; // Safety limit
    }
    
    // Restore normal timeout
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    if (flushed > 0 && net_debug_enabled) {
        fprintf(stderr, "[FLUSH] Cleared %d stale buffers\n", flushed);
    }
}

void send_message(const char *msg) {
    if (client.socket_fd > 0) {
        // Debug: in message gửi đi
        if (net_debug_enabled) {
            print_debug("Send:", msg, 1);
        }
        
        ssize_t s = send(client.socket_fd, msg, strlen(msg), 0);
        (void)s;
    } else if (net_debug_enabled) {
        fprintf(stderr, "Cannot send: socket not connected\n");
    }
}

ssize_t receive_message(char *buffer, size_t bufsz) {
    if (client.socket_fd <= 0) {
        if (net_debug_enabled) {
            fprintf(stderr, "Cannot receive: socket not connected\n");
        }
        return -1;
    }
    
    memset(buffer, 0, bufsz);
    ssize_t n = recv(client.socket_fd, buffer, bufsz - 1, 0);
    
    if (n > 0) {
        buffer[n] = '\0';
        
        // Debug: in message nhận được
        if (net_debug_enabled) {
            print_debug("Receive:", buffer, 1);
        }
    } else if (n == 0) {
        buffer[0] = '\0';
        if (net_debug_enabled) {
            fprintf(stderr, "Receive: Connection closed by server\n");
        }
    } else {
        // n < 0: lỗi
        if (net_debug_enabled) {
            perror("Cannot receive: Receive failed");
        }
    }
    
    return n;
}

ssize_t receive_message_timeout(char *buffer, size_t bufsz, int timeout_sec) {
    if (client.socket_fd <= 0) return -1;
    
    // Lưu timeout cũ
    struct timeval old_timeout;
    socklen_t len = sizeof(old_timeout);
    getsockopt(client.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &old_timeout, &len);
    
    // Set timeout mới
    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;
    setsockopt(client.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    ssize_t n = receive_message(buffer, bufsz);
    
    // Khôi phục timeout cũ
    setsockopt(client.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &old_timeout, sizeof(old_timeout));
    
    return n;
}

// Background listener to receive server pushes (START_EXAM, EXAM_FINISHED, TIME_UPDATE)
static pthread_t server_listener_thread;

static gboolean idle_resume_ui(gpointer data) {
    char *resume_data = (char*)data;
    // Use selected_room_id from UI
    extern int selected_room_id;
    create_exam_page_from_resume(selected_room_id, resume_data);
    free(resume_data);
    return FALSE;
}

static gboolean idle_exam_finished(gpointer data) {
    (void)data;
    show_info_dialog("Exam Finished", "Time's up. The exam has been submitted by server.");
    create_test_mode_screen();
    return FALSE;
}

static void *server_listener(void *arg) {
    (void)arg;
    char buffer[BUFFER_SIZE * 2];
    while (client.socket_fd > 0) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = recv(client.socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        print_debug("Push:", buffer, 1);

        if (strncmp(buffer, "START_EXAM|", 11) == 0) {
            // Server indicates exam start; request resume payload
            extern int selected_room_id;
            if (selected_room_id <= 0) continue;
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "RESUME_EXAM|%d\n", selected_room_id);
            send_message(cmd);
            // Expect RESUME_EXAM_OK response
            char rbuf[BUFFER_SIZE * 2];
            ssize_t rn = receive_message(rbuf, sizeof(rbuf));
            if (rn > 0 && strncmp(rbuf, "RESUME_EXAM_OK", 14) == 0) {
                char *dup = strdup(rbuf);
                g_idle_add(idle_resume_ui, dup);
                // Flush any locally-stored answers now that we have reconnected/resumed
                flush_pending_answers();
            }
        }
        else if (strncmp(buffer, "EXAM_FINISHED", 13) == 0) {
            g_idle_add(idle_exam_finished, NULL);
        }
        else if (strncmp(buffer, "TIME_UPDATE|", 12) == 0) {
            // Optionally ignore or display
        }
        else if (buffer[0] == '{') {
            cJSON *root = cJSON_Parse(buffer);
            if (root) {
                cJSON *type = cJSON_GetObjectItem(root, "type");
                if (cJSON_IsString(type) && type->valuestring) {
                    if (strcmp(type->valuestring, "START_EXAM") == 0) {
                        cJSON *jend = cJSON_GetObjectItem(root, "end_time");
                        cJSON *jserver = cJSON_GetObjectItem(root, "server_ts");
                        if (cJSON_IsNumber(jend) && cJSON_IsNumber(jserver)) {
                            // Request resume to get full payload
                            extern int selected_room_id;
                            if (selected_room_id > 0) {
                                char cmd[128];
                                snprintf(cmd, sizeof(cmd), "RESUME_EXAM|%d\n", selected_room_id);
                                send_message(cmd);
                                char rbuf[BUFFER_SIZE * 2];
                                ssize_t rn = receive_message(rbuf, sizeof(rbuf));
                                if (rn > 0 && strncmp(rbuf, "RESUME_EXAM_OK", 14) == 0) {
                                    char *dup = strdup(rbuf);
                                    g_idle_add(idle_resume_ui, dup);
                                    flush_pending_answers();
                                }
                            }
                        }
                    } else if (strcmp(type->valuestring, "EXAM_FINISHED") == 0) {
                        g_idle_add(idle_exam_finished, NULL);
                    }
                }
                cJSON_Delete(root);
            }
        }
    }
    return NULL;
}

void start_server_listener(void) {
    if (pthread_create(&server_listener_thread, NULL, server_listener, NULL) == 0) {
        pthread_detach(server_listener_thread);
    }
}