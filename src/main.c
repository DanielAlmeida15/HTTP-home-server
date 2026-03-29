#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include "../include/server.h"

int g_http_debug   = 0;
int g_server_debug = 0;

static void load_config(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return;

    char line[128];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char key[64];
        int  val;
        if (sscanf(line, "%63s = %d", key, &val) != 2) continue;

        if      (strcmp(key, "http_debug")   == 0) g_http_debug   = val;
        else if (strcmp(key, "server_debug") == 0) g_server_debug = val;
    }

    fclose(f);
}

void *handle_client(void *arg)
{
    int client_fd = *((int *)arg);
    free(arg);

    char message[BUFFER_SIZE];

    do
    {
        http_message_t parsed_message;
        memset(&parsed_message, 0, sizeof(parsed_message));
        char *response = NULL;
        int keep_alive = 0;

        /* Receive request data from client and store into buffer */
        ssize_t bytes_received = recv(client_fd, message, BUFFER_SIZE, 0);
        if (bytes_received > 0)
        {
            message[bytes_received] = '\0';
            if (g_server_debug)
                printf("Received: %ld bytes\n%s\n", bytes_received, message);

            http_error_code http_error = http_parse_message(message, bytes_received, &parsed_message);

            if (g_server_debug)
                printf("Parsed message with error %s\n", get_http_error_name(http_error));

            if (http_error == Ok)
            {
                http_error = http_validate_message(&parsed_message);
                if (g_server_debug)
                    printf("Validated message with error %s\n", get_http_error_name(http_error));

            }else if(http_error == No_Content)
            {
                if (parsed_message.headers.content_length == NULL)
                {
                    http_error = Bad_Request;
                }
                else
                {
                    uint32_t content_length = (uint32_t)*(parsed_message.headers.content_length);
                    uint32_t total_bytes_rcv = 0;

                    do {
                        bytes_received = recv(client_fd,
                                             message + total_bytes_rcv,
                                             content_length - total_bytes_rcv, 0);
                        if (bytes_received <= 0) break;
                        total_bytes_rcv += bytes_received;
                    }while (total_bytes_rcv < content_length);

                    if(total_bytes_rcv != content_length)
                    {
                        http_error = Bad_Request;
                    }else
                    {
                        parsed_message.content = strstrcpy(message, content_length);
                        if (g_server_debug)
                            printf("Content:\n%s\n", parsed_message.content);
                        http_error = http_validate_message(&parsed_message);
                    }
                }
            }

            size_t response_len = http_build_response(http_error, &parsed_message, &response);

            ssize_t bytes_sent = send(client_fd, response, response_len, 0);

            if (g_server_debug)
                printf("Sent: %ld bytes\n%s\n", bytes_sent, response);

            keep_alive = (parsed_message.headers.connection != NULL && parsed_message.headers.connection->keep_alive);

            /* Cleanup allocations */
            free(response);
            free(parsed_message.headers.host);
            free(parsed_message.headers.connection);
            free(parsed_message.headers.content_length);
            free(parsed_message.headers.user_agent);
            free(parsed_message.headers.content_type);
            free((void *)parsed_message.content);
            free(parsed_message.request_line.method);
            free(parsed_message.request_line.target_resource);
        }
        else if(bytes_received == 0)
        {
            if (g_server_debug) printf("Closing connection as requested\n");
            break;
        }else
        {
            if (g_server_debug) printf("Error on recv\n");
            break;
        }

        if (!keep_alive) break;

    }while (1);

    close(client_fd);
    return NULL;
}

int main(void)
{
    load_config(CONFIG_CONF);

    if (load_resources(RESOURCES_CONF) != 0)
    {
        fprintf(stderr, "Failed to load resources from: %s\n", RESOURCES_CONF);
        return EXIT_FAILURE;
    }

    int server_fd;
    struct sockaddr_in server_addr;

    // create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        if (g_server_debug) perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Allow immediate reuse of the port after restart
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Config socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        if (g_server_debug) perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, N_CONNECTIONS) < 0)
    {
        if (g_server_debug) perror("listen failed");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        // Client info
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int *client_fd = malloc(sizeof(int));
        if (client_fd == NULL)
            continue;

        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        // Accept client connection
        if (*client_fd < 0)
        {
            if (g_server_debug) perror("accept failed");
            free(client_fd);
            continue;
        }

        // Create a new thread to handle client request
        if (g_server_debug) printf("Creating thread\n");
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_fd) != 0)
        {
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(thread_id);
    }

    return 0;
}
