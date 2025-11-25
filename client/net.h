#ifndef NET_H
#define NET_H

#include "include/client_common.h"

void send_message(const char *msg);
ssize_t receive_message(char *buffer, size_t bufsz);

#endif // NET_H
