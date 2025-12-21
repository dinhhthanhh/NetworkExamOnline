#ifndef NET_H
#define NET_H
#include<sys/types.h>
#include "include/client_common.h"

void flush_socket_buffer(int sockfd);
void send_message(const char *msg);
ssize_t receive_message(char *buffer, size_t bufsz);
void net_set_timeout(int sockfd);
void net_set_debug(int enabled);
ssize_t receive_message_timeout(char *buffer, size_t bufsz, int timeout_sec);

#endif // NET_H
