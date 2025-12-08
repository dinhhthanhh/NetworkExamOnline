#ifndef DB_H
#define DB_H

#include "common.h"

void init_database(void);
void log_activity(int user_id, const char *action, const char *details);

#endif
