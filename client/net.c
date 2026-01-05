#include "net.h"
#include "ui_utils.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>

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
    // Check connection first
    if (client.socket_fd <= 0 || !check_connection()) {
        if (net_debug_enabled) {
            printf("Cannot send: not connected to server\n");
        }
        show_connection_lost_dialog();
        return;
    }
    
    // Debug: in message gửi đi
    if (net_debug_enabled) {
        print_debug("Send:", msg, 1);
    }
    
    ssize_t s = send(client.socket_fd, msg, strlen(msg), 0);
    if (s < 0) {
        if (net_debug_enabled) {
            perror("Send failed");
        }
        // Connection might be lost
        if (errno == EPIPE || errno == ECONNRESET) {
            close(client.socket_fd);
            client.socket_fd = -1;
        }
    }
}

ssize_t receive_message(char *buffer, size_t bufsz) {
    if (client.socket_fd <= 0) {
        if (net_debug_enabled) {
            printf("Cannot receive: socket not connected\n");
        }
        show_connection_lost_dialog();
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
        // Close socket to prevent further use
        if (client.socket_fd > 0) {
            close(client.socket_fd);
            client.socket_fd = -1;
        }
        show_connection_lost_dialog();
    } else {
        // n < 0: lỗi
        if (net_debug_enabled) {
            perror("Cannot receive: Receive failed");
        }
        // Check if connection was lost
        if (errno == ECONNRESET || errno == EPIPE) {
            if (client.socket_fd > 0) {
                close(client.socket_fd);
                client.socket_fd = -1;
            }
            show_connection_lost_dialog();
        }
    }
    
    return n;
}

// Receive complete message for large responses (handles TCP fragmentation)
ssize_t receive_complete_message(char *buffer, size_t bufsz, int max_attempts) {
    if (client.socket_fd <= 0) {
        if (net_debug_enabled) {
            printf("Cannot receive: socket not connected\n");
        }
        return -1;
    }
    
    memset(buffer, 0, bufsz);
    size_t total_received = 0;
    int attempts = 0;
    
    // Set short timeout for additional reads
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100ms between attempts
    setsockopt(client.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (attempts < max_attempts && total_received < bufsz - 1) {
        ssize_t n = recv(client.socket_fd, buffer + total_received, 
                         bufsz - total_received - 1, 0);
        
        if (n > 0) {
            total_received += n;
            buffer[total_received] = '\0';
            attempts = 0; // Reset on successful read
            
            // Check if we got a complete message (ends with newline)
            if (total_received > 0 && buffer[total_received - 1] == '\n') {
                break;
            }
        } else if (n == 0) {
            // Connection closed
            if (net_debug_enabled) {
                printf("Connection closed while receiving\n");
            }
            break;
        } else {
            // Error or timeout
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout - check if we have any data
                if (total_received > 0) {
                    // We have partial data, increment attempt counter
                    attempts++;
                } else {
                    // No data yet, keep trying
                    attempts++;
                }
            } else {
                // Real error
                if (net_debug_enabled) {
                    perror("Error receiving");
                }
                break;
            }
        }
    }
    
    // Restore normal timeout
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    if (total_received > 0 && net_debug_enabled) {
        printf("[RECEIVE_COMPLETE] Got %zu bytes total\n", total_received);
        print_debug("Receive:", buffer, 1);
    }
    
    return total_received;
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

// Reconnect to server after logout
int reconnect_to_server(void) {
    struct sockaddr_in server_addr;

    // Close old socket if still open
    if (client.socket_fd > 0) {
        close(client.socket_fd);
    }

    // Create new socket
    client.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client.socket_fd < 0) {
        perror("socket()");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_PORT);

    if (!inet_aton(SERVER_IP, &server_addr.sin_addr)) {
        fprintf(stderr, "Invalid server IP address.\n");
        return -1;
    }

    if (connect(client.socket_fd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect()");
        return -1;
    }

    printf("[INFO] Reconnected to server\n");
    return 0;
}

// Check if connection is still alive
int check_connection(void) {
    if (client.socket_fd <= 0) {
        return 0; // Not connected
    }
    
    // Try to peek at the socket
    char test_byte;
    ssize_t result = recv(client.socket_fd, &test_byte, 1, MSG_PEEK | MSG_DONTWAIT);
    
    if (result == 0) {
        // Connection closed
        close(client.socket_fd);
        client.socket_fd = -1;
        return 0;
    } else if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available, but connection is OK
            return 1;
        } else {
            // Connection error
            close(client.socket_fd);
            client.socket_fd = -1;
            return 0;
        }
    }
    
    return 1; // Connected
}