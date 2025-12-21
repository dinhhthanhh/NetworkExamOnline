#ifndef PRACTICE_UI_H
#define PRACTICE_UI_H

#include "include/client_common.h"

// Practice mode screen
void create_practice_screen(void);

// Practice thread
gpointer practice_thread_func(gpointer data);
gboolean update_practice_text(gpointer data);

// External global variables
extern GtkTextBuffer *g_practice_buffer;

#endif // PRACTICE_UI_H
