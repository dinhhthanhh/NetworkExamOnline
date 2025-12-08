#include "net.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "include/client_common.h"

void net_set_timeout(int sockfd) {
    struct timeval timeout;
    timeout.tv_sec = 5;   // 5 giây (có thể chỉnh theo ý bạn)
    timeout.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

extern ClientData client;

void send_message(const char *msg) {
    if (client.socket_fd > 0) {
        ssize_t s = send(client.socket_fd, msg, strlen(msg), 0);
        (void)s;
    }
}

ssize_t receive_message(char *buffer, size_t bufsz) {
    if (client.socket_fd <= 0) return -1;
    ssize_t n = recv(client.socket_fd, buffer, bufsz - 1, 0);
    if (n > 0) buffer[n] = '\0';
    else if (n == 0) buffer[0] = '\0';
    return n;
}
