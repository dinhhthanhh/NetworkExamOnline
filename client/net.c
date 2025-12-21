#include "net.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>

#include "include/client_common.h"

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
    
    printf("%s %s\n", direction, debug_msg);
}

void net_set_timeout(int sockfd) {
    struct timeval timeout;
    timeout.tv_sec = 5;   
    timeout.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

extern ClientData client;

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
        printf("[FLUSH] Cleared %d stale buffers\n", flushed);
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
        printf("Cannot send: socket not connected\n");
    }
}

ssize_t receive_message(char *buffer, size_t bufsz) {
    if (client.socket_fd <= 0) {
        if (net_debug_enabled) {
            printf("Cannot receive: socket not connected\n");
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
            printf("Receive: Connection closed by server\n");
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