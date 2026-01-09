#include "include/common.h"
#include "network.h"
#include "db.h"
#include "questions.h"
#include "rooms.h"
#include "practice.h"
#include "timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

// Define globals declared as extern in common.h
ServerData server_data;
sqlite3 *db = NULL;

// Timer thread function - checks room timeouts every 5 seconds
void *timer_thread(void *arg) {
    while (1) {
        sleep(5);
        check_room_timeouts();
    }
    return NULL;
}

int main()
{
    int server_socket, *client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    pthread_t thread_id, timer_tid;

    // Zero initialize server data
    memset(&server_data, 0, sizeof(server_data));
    pthread_mutex_init(&server_data.lock, NULL);

    // Redirect stdout and stderr to server.log
    if (freopen("server.log", "a", stdout) == NULL) {
        perror("Failed to redirect stdout to server.log");
    }
    if (freopen("server.log", "a", stderr) == NULL) {
        perror("Failed to redirect stderr to server.log");
    }
    // Set line buffering for stdout and no buffering for stderr
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Initialize DB and load questions
    init_database();
    load_users_from_db();  // Load users vào in-memory structure
    load_rooms_from_db();  // Load rooms vào in-memory structure
    load_practice_rooms_from_db();  // Load practice rooms
    // load_sample_questions();

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, 5) < 0)
    {
        perror("Listen failed");
        close(server_socket);
        return 1;
    }

    printf("Server started on port %d\n", PORT);
    
    // Start timer thread for room timeout checks
    if (pthread_create(&timer_tid, NULL, timer_thread, NULL) != 0) {
        perror("Failed to create timer thread");
    } else {
        pthread_detach(timer_tid);
    }

    while (1)
    {
        client_len = sizeof(client_addr);
        client_socket = malloc(sizeof(int));
        if (!client_socket)
            continue;

        *client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (*client_socket < 0)
        {
            perror("Accept failed");
            free(client_socket);
            continue;
        }

        if (pthread_create(&thread_id, NULL, handle_client, (void *)client_socket) != 0)
        {
            perror("pthread_create failed");
            close(*client_socket);
            free(client_socket);
            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_socket);
    return 0;
}
