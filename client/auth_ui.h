#ifndef AUTH_UI_H
#define AUTH_UI_H

#include "include/client_common.h"

// Authentication screens
void create_login_screen(void);

// Callbacks
void on_login_clicked(GtkWidget *widget, gpointer data);
void on_register_clicked(GtkWidget *widget, gpointer data);

// Login entry structure
typedef struct
{
    GtkWidget *user_entry;
    GtkWidget *pass_entry;
} LoginEntries;

// External global variables
extern int current_user_id;

#endif // AUTH_UI_H
