#ifndef NET_H
#define NET_H

#include "include/client_common.h"

void send_message(const char *msg);
ssize_t receive_message(char *buffer, size_t bufsz);
void net_set_timeout(int sockfd);

#endif // NET_H
