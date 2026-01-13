#include "broadcast.h"
#include "net.h"
#include "include/client_common.h"
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

extern ClientData client;

// Callbacks
static RoomStartedCallback room_started_callback = NULL;
static RoomCreatedCallback room_created_callback = NULL;
static RoomDeletedCallback room_deleted_callback = NULL;
static RoomEndedCallback room_ended_callback = NULL;
static PracticeClosedCallback practice_closed_callback = NULL;

// Listener state
static guint timer_id = 0;
static int is_listening = 0;
static int current_waiting_room_id = -1;  // Room đang chờ broadcast
static pthread_mutex_t broadcast_mutex = PTHREAD_MUTEX_INITIALIZER;

// Set socket to non-blocking mode
static void set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }
}

// Set socket back to blocking mode
static void set_blocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
    }
}

// Check if message is a broadcast (not a regular response)
static int is_broadcast_message(const char *message) {
    return (strncmp(message, "ROOM_STARTED", 12) == 0 ||
            strncmp(message, "ROOM_CREATED", 12) == 0 ||
            strncmp(message, "ROOM_DELETED", 12) == 0 ||
            strncmp(message, "ROOM_ENDED", 10) == 0 ||
            strncmp(message, "PRACTICE_CLOSED", 15) == 0);
}

// Parse and handle broadcast messages
static void handle_broadcast_message(const char *message) {
    
    // Parse ROOM_STARTED|room_id|start_time
    if (strncmp(message, "ROOM_STARTED", 12) == 0) {
        char msg_copy[512];
        strncpy(msg_copy, message, sizeof(msg_copy) - 1);
        msg_copy[sizeof(msg_copy) - 1] = '\0';
        
        char *ptr = msg_copy;
        strtok(ptr, "|"); // Skip "ROOM_STARTED"
        char *room_id_str = strtok(NULL, "|");
        char *start_time_str = strtok(NULL, "|");
        
        if (room_id_str && start_time_str && room_started_callback) {
            int room_id = atoi(room_id_str);
            long start_time = atol(start_time_str);
            
<<<<<<< HEAD
            // CHỈ xử lý nếu đúng room đang đợi
            pthread_mutex_lock(&broadcast_mutex);
            int waiting_room = current_waiting_room_id;
            pthread_mutex_unlock(&broadcast_mutex);
            
            if (waiting_room == room_id && waiting_room != -1) {
                printf("[BROADCAST] Room %d started at %ld - Processing\n", room_id, start_time);
                room_started_callback(room_id, start_time);
            } else {
                printf("[BROADCAST] Ignored ROOM_STARTED for room %d (waiting for %d)\n", 
                       room_id, waiting_room);
            }
=======
            room_started_callback(room_id, start_time);
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
        }
    }
    // Parse PRACTICE_CLOSED|practice_id|room_name
    else if (strncmp(message, "PRACTICE_CLOSED", 15) == 0) {
        char msg_copy[512];
        strncpy(msg_copy, message, sizeof(msg_copy) - 1);
        msg_copy[sizeof(msg_copy) - 1] = '\0';

        char *ptr = msg_copy;
        strtok(ptr, "|"); // Skip "PRACTICE_CLOSED"
        char *practice_id_str = strtok(NULL, "|");
        char *room_name = strtok(NULL, "|");

        if (practice_id_str && room_name && practice_closed_callback) {
            int practice_id = atoi(practice_id_str);
            practice_closed_callback(practice_id, room_name);
        }
    }
    // Parse ROOM_CREATED|room_id|room_name|duration
    else if (strncmp(message, "ROOM_CREATED", 12) == 0) {
        char msg_copy[512];
        strncpy(msg_copy, message, sizeof(msg_copy) - 1);
        msg_copy[sizeof(msg_copy) - 1] = '\0';
        
        char *ptr = msg_copy;
        strtok(ptr, "|"); // Skip "ROOM_CREATED"
        char *room_id_str = strtok(NULL, "|");
        char *room_name = strtok(NULL, "|");
        char *duration_str = strtok(NULL, "|");
        
        if (room_id_str && room_name && duration_str && room_created_callback) {
            int room_id = atoi(room_id_str);
            int duration = atoi(duration_str);
            
            room_created_callback(room_id, room_name, duration);
        }
    }
    // Parse ROOM_DELETED|room_id
    else if (strncmp(message, "ROOM_DELETED", 12) == 0) {
        char msg_copy[512];
        strncpy(msg_copy, message, sizeof(msg_copy) - 1);
        msg_copy[sizeof(msg_copy) - 1] = '\0';
        
        char *ptr = msg_copy;
        strtok(ptr, "|"); // Skip "ROOM_DELETED"
        char *room_id_str = strtok(NULL, "|");
        
        if (room_id_str && room_deleted_callback) {
            int room_id = atoi(room_id_str);
            
            room_deleted_callback(room_id);
        }
    }
    // Parse ROOM_ENDED|room_id
    else if (strncmp(message, "ROOM_ENDED", 10) == 0) {
        char msg_copy[512];
        strncpy(msg_copy, message, sizeof(msg_copy) - 1);
        msg_copy[sizeof(msg_copy) - 1] = '\0';
        
        char *ptr = msg_copy;
        strtok(ptr, "|"); // Skip "ROOM_ENDED"
        char *room_id_str = strtok(NULL, "|");
        
        if (room_id_str && room_ended_callback) {
            int room_id = atoi(room_id_str);
            
            room_ended_callback(room_id);
        }
    }
}

// GTK timeout callback - periodically polls the socket for broadcast messages using non-blocking I/O
static gboolean poll_broadcasts(gpointer user_data) {
    if (!is_listening) {
        return FALSE; // Stop timer
    }
    
    // Check if socket is valid
    if (client.socket_fd <= 0) {
        return TRUE; // Continue timer but skip this iteration
    }
    
    // Set socket to non-blocking temporarily
    set_nonblocking(client.socket_fd);
    
    char buffer[BUFFER_SIZE];
    
    // First, PEEK at the data without removing it
    ssize_t n = recv(client.socket_fd, buffer, sizeof(buffer) - 1, MSG_PEEK | MSG_DONTWAIT);
    
    if (n > 0) {
        buffer[n] = '\0';
        
        // Strip newline for checking
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
            buffer[len-1] = '\0';
            len--;
        }
        
        // Check if this is a broadcast message
        if (is_broadcast_message(buffer)) {
            // It's a broadcast - now actually read and remove it
            n = recv(client.socket_fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
            if (n > 0) {
                buffer[n] = '\0';
                
                // Strip newline
                len = strlen(buffer);
                while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
                    buffer[len-1] = '\0';
                    len--;
                }
                
                // Handle the broadcast
                handle_broadcast_message(buffer);
            }
        }
        // If not a broadcast, leave it in buffer for request-response code
    }
    
    // Restore blocking mode
    set_blocking(client.socket_fd);
    
    return TRUE; // Continue timer
}

<<<<<<< HEAD
// Start broadcast listener for specific room (waiting mode)
void broadcast_start_listener_for_room(int room_id) {
    // Stop any existing listener first
    broadcast_stop_listener();
    
    pthread_mutex_lock(&broadcast_mutex);
    current_waiting_room_id = room_id;
    is_listening = 1;
    pthread_mutex_unlock(&broadcast_mutex);
    
    // Poll every 200ms using GTK timer
    timer_id = g_timeout_add(200, poll_broadcasts, NULL);
    
    printf("[BROADCAST] Listener started for room %d (polling every 200ms)\n", room_id);
}

// Start broadcast listener (general - for room list updates)
=======
// Start periodic listener that polls for ROOM_* broadcasts from the server
>>>>>>> 4a33a224791759951890cbd929b10a252b027d31
void broadcast_start_listener(void) {
    if (is_listening) {
        return;
    }
    
    pthread_mutex_lock(&broadcast_mutex);
    current_waiting_room_id = -1;  // No specific room
    is_listening = 1;
    pthread_mutex_unlock(&broadcast_mutex);
    
    // Poll every 200ms using GTK timer
    timer_id = g_timeout_add(200, poll_broadcasts, NULL);
    
}

// Stop the broadcast listener and restore blocking mode on the socket
void broadcast_stop_listener(void) {
    if (!is_listening) {
        return;
    }
    
    pthread_mutex_lock(&broadcast_mutex);
    is_listening = 0;
    current_waiting_room_id = -1;  // Clear waiting room
    pthread_mutex_unlock(&broadcast_mutex);
    
    if (timer_id > 0) {
        g_source_remove(timer_id);
        timer_id = 0;
    }
    
    // Restore blocking mode on socket
    set_blocking(client.socket_fd);
    
}

// Check if the broadcast listener is currently running
int broadcast_is_listening(void) {
    return is_listening;
}

// Register callback for ROOM_STARTED
void broadcast_on_room_started(RoomStartedCallback callback) {
    room_started_callback = callback;
}

// Register callback for ROOM_CREATED
void broadcast_on_room_created(RoomCreatedCallback callback) {
    room_created_callback = callback;
}

// Register callback for ROOM_DELETED
void broadcast_on_room_deleted(RoomDeletedCallback callback) {
    room_deleted_callback = callback;
}

void broadcast_on_practice_closed(PracticeClosedCallback callback) {
    practice_closed_callback = callback;
}

void broadcast_on_room_ended(RoomEndedCallback callback) {
    room_ended_callback = callback;
}
