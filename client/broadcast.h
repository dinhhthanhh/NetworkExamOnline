#ifndef BROADCAST_H
#define BROADCAST_H

#include <gtk/gtk.h>

// Start listening for broadcasts from server
void broadcast_start_listener(void);

// Stop listening
void broadcast_stop_listener(void);

// Check if listener is running
int broadcast_is_listening(void);

// Callback types
typedef void (*RoomStartedCallback)(int room_id, long start_time);
typedef void (*RoomCreatedCallback)(int room_id, const char *room_name, int duration);
typedef void (*RoomDeletedCallback)(int room_id);

// Register callbacks
void broadcast_on_room_started(RoomStartedCallback callback);
void broadcast_on_room_created(RoomCreatedCallback callback);
void broadcast_on_room_deleted(RoomDeletedCallback callback);

#endif // BROADCAST_H
