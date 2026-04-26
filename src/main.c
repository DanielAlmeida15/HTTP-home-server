#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include "../include/server.h"
#include "../include/thread_pool.h"

#define WORKER_COUNT     16
#define QUEUE_CAPACITY   64

log_level_t  g_log_level = LOG_ERROR;
FILE        *g_log_file  = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_write(log_level_t level, const char *fmt, ...)
{
    if (level > g_log_level) return;

    static const char *level_str[] = { "ERROR", "INFO ", "DEBUG" };

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) return;

    pthread_mutex_lock(&g_log_mutex);
    fprintf(stdout, "[%s] [%s] %s", ts, level_str[level], msg);
    if (g_log_file != NULL)
    {
        fprintf(g_log_file, "[%s] [%s] %s", ts, level_str[level], msg);
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_log_mutex);
}

static int load_config(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char key[64];
        char sval[256];
        int  ival;

        int mi = (sscanf(line, "%63s = %d",   key, &ival) == 2);
        int ms = (sscanf(line, "%63s = %255s", key, sval) == 2);

        if (mi)
        {
            if (strcmp(key, "log_level") == 0) g_log_level = (log_level_t)ival;
        }
        if (ms)
        {
            if (strcmp(key, "log_file") == 0)
            {
                g_log_file = fopen(sval, "a");
            }
        }
    }

    fclose(f);
    return 0;
}

void handle_client(int client_fd)
{
    char *message = malloc(BUFFER_SIZE);
    if (message == NULL) { close(client_fd); return; }

    do
    {
        http_message_t parsed_message;
        memset(&parsed_message, 0, sizeof(parsed_message));
        char *response = NULL;
        int keep_alive = 0;

        ssize_t bytes_received = recv(client_fd, message, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0)
        {
            message[bytes_received] = '\0';
            log_write(LOG_DEBUG, "Received: %ld bytes\n%s\n", bytes_received, message);

            http_error_code http_error = http_parse_message(message, bytes_received, &parsed_message);
            log_write(LOG_DEBUG, "Parsed message with error %s\n", get_http_error_name(http_error));

            if (http_error == Ok)
            {
                http_error = http_validate_message(&parsed_message);
                log_write(LOG_DEBUG, "Validated message with error %s\n", get_http_error_name(http_error));

            }else if(http_error == No_Content)
            {
                char  *headers_end = strstr(message, "\r\n\r\n");
                size_t body_offset = (headers_end != NULL)
                                     ? (size_t)(headers_end + 4 - message)
                                     : (size_t)bytes_received;
                size_t body_present = (size_t)bytes_received - body_offset;

                if (parsed_message.headers.content_length == NULL || headers_end == NULL)
                {
                    http_error = Bad_Request;
                }
                else if ((*parsed_message.headers.content_length) >
                         ((size_t)(BUFFER_SIZE - 1) - body_offset))
                {
                    http_error = Content_Too_Large;
                }
                else
                {
                    size_t content_length  = *parsed_message.headers.content_length;
                    size_t total_bytes_rcv = body_present;

                    while (total_bytes_rcv < content_length)
                    {
                        ssize_t n = recv(client_fd,
                                         message + body_offset + total_bytes_rcv,
                                         content_length - total_bytes_rcv, 0);
                        if (n <= 0) break;
                        total_bytes_rcv += (size_t)n;
                    }

                    if (total_bytes_rcv != content_length)
                    {
                        http_error = Bad_Request;
                    }
                    else
                    {
                        parsed_message.content = strstrcpy(message + body_offset, content_length);
                        log_write(LOG_DEBUG, "Content:\n%s\n", parsed_message.content);
                        http_error = http_validate_message(&parsed_message);
                    }
                }
            }

            size_t response_len = http_build_response(http_error, &parsed_message, &response, client_fd);
            if (response != NULL && response_len > 0)
            {
                ssize_t bytes_sent = send(client_fd, response, response_len, 0);
                log_write(LOG_DEBUG, "Sent: %ld bytes\n", bytes_sent);
            }

            keep_alive = (parsed_message.headers.connection != NULL && parsed_message.headers.connection->keep_alive);

            free(response);
            http_message_free(&parsed_message);
        }
        else if(bytes_received == 0)
        {
            log_write(LOG_DEBUG, "Closing connection as requested\n");
            break;
        }else
        {
            log_write(LOG_DEBUG, "Error on recv\n");
            break;
        }

        if (!keep_alive) break;

    }while (1);

    free(message);
    close(client_fd);
}

int main(void)
{
    load_config(CONFIG_CONF);

    if (load_resources(RESOURCES_CONF) != 0)
    {
        log_write(LOG_ERROR, "Failed to load resources from: %s\n", RESOURCES_CONF);
        return EXIT_FAILURE;
    }

    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        log_write(LOG_ERROR, "socket creation failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        log_write(LOG_ERROR, "bind failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, N_CONNECTIONS) < 0)
    {
        log_write(LOG_ERROR, "listen failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    thread_pool_t pool;
    if (thread_pool_init(&pool, WORKER_COUNT, QUEUE_CAPACITY, handle_client) != 0)
    {
        log_write(LOG_ERROR, "thread_pool_init failed\n");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0)
        {
            log_write(LOG_ERROR, "accept failed: %s\n", strerror(errno));
            continue;
        }

        if (thread_pool_submit(&pool, client_fd) != 0)
        {
            /* Pool full — connection was closed by submit. Silent drop. */
            log_write(LOG_INFO, "Pool full, dropping connection\n");
        }
    }

    return 0;
}
