#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <pthread.h>
#include <regex.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../include/server.h"

#define PORT 8080
#define BUFFER_SIZE         1000000
#define STATUS_LINE_SIZE    50
#define RESPONSE_BODY_SIZE  500

methods_alowed_t methods_allowed_cheatTable[] = {
    {
        "data.txt", 
        {
            [GET]     = "GET",
            [POST]    = "POST",
            [HEAD]    = NULL,
            [PUT]     = NULL,
            [DELETE]  = NULL,
            [CONNECT] = NULL,
            [OPTIONS] = NULL,
            [TRACE]   = NULL
        }
    },
};

char *strstrcpy(const char *src, size_t length)
{
    if (src == NULL)
        return NULL;

    char *dst = malloc(sizeof(char)*(length+1));
    memcpy(dst, src, length);
    dst[length] = '\0';

    return dst;
}

void lowercase(char *string, size_t length)
{
    if(string == NULL)
        return;

    int i = 0;
    while ((string[i] != '\0') && (i<=length))
    {
        if (string[i] >= 'A' && string[i] <= 'Z')
            string[i] += 32;

        i++;
    }
}

http_error_code http_parse_message(const char *message, size_t message_size, http_message_t *parsed_message)
{
    if (message == NULL || parsed_message == NULL)
        return Internal_Server_Error;
    
    http_error_code http_error = Ok;

    /* Get the request line */

    regex_t request_line_regex;
    regcomp(&request_line_regex, "^([A-Z]+) /([^ ]+) HTTP/([0-9]\\.[0-9])\r\n", REG_EXTENDED);
    regmatch_t matches[5];

    if (regexec(&request_line_regex, message, 5, matches, 0) == 0)
    {
        /* Extract method */

        size_t len = matches[1].rm_eo - matches[1].rm_so;
        parsed_message->request_line.method = strstrcpy((message + matches[1].rm_so), len);
        
        /* Extract target resource */

        len = matches[2].rm_eo - matches[2].rm_so;
        parsed_message->request_line.target_resource = strstrcpy((message + matches[2].rm_so), len);
        
        /* Extract HTTP version */

        char *version = malloc(sizeof(char)*2);
        version[1] = '\0';

        memcpy(version, (message + matches[3].rm_so), 1);
        parsed_message->request_line.hhtp_major_version = atoi(version);

        memcpy(version, (message + matches[4].rm_so), 1);
        parsed_message->request_line.http_minor_version = atoi(version);

        free(version);
    }else
    {
        regfree(&request_line_regex);
        return Bad_Request;
    }

    /* Get and parse headers */

    int current_headers = 0, headers_expected = 10, header_length = 0, subString_length = 0;
    headers_t *headers = malloc(sizeof(headers_t) * headers_expected);
    const char *init_pos = message + matches[0].rm_eo, *end_pos = NULL, delimiter = ':', *delimiter_pos = NULL;
    char header[100];

    while (1)
    {
        end_pos = strstr(init_pos, "\r\n");

        if(end_pos == NULL){
            http_error = Bad_Request;
            goto cleanup;

        }else if(end_pos-init_pos < 1){
            break;
        }

        header_length = end_pos-init_pos;
        memcpy(header, init_pos, header_length);
        header[header_length] = '\0';
        delimiter_pos = strchr(header, delimiter);

        if (delimiter_pos == NULL)
        {
            http_error = Bad_Request;
            goto cleanup;
        }
        
        if (current_headers >= (headers_expected-1))
        {
            headers = realloc(headers, sizeof(headers_t) * (current_headers+headers_expected));
        }

        subString_length = delimiter_pos - header;
        headers[current_headers].name = strstrcpy(header, subString_length);
        lowercase(headers[current_headers].name, subString_length+1);
        
        subString_length = header_length - subString_length;
        do
        {
            delimiter_pos++;
            subString_length--;
        } while (*delimiter_pos == ' ');
        
        headers[current_headers].value = strstrcpy(delimiter_pos, subString_length);
        
        //printf("HEADER: %s:%s\n\n", headers[current_headers].name, headers[current_headers].value);

        current_headers++;
        init_pos = end_pos+2;
    }

    HashTable_t *headers_table = hash_create_table(current_headers);
    hash_error error = HASH_OK;

    for (int i = 0; i < current_headers; i++)
    {
        error = hash_insert_entry(headers_table, headers[i].name, headers[i].value, REJECT);

        if(error == ENTRY_REJECTED)
        {
            http_error = Bad_Request;
            free(headers_table);
            goto cleanup;
        }    
    }

    parsed_message->fields = headers_table;

    Entry_t *content_length = hash_search_table(parsed_message->fields, "content-length");
    if (content_length != NULL)
    {
        if ((end_pos+2-message) == message_size)
        {
            /* The user must try to receive another packet with recv() in the attempt to fetch the content */
            http_error = No_Content;
        }

        parsed_message->content = strstrcpy( end_pos+2, atoi(content_length->value));
    }
    
cleanup:
    for (int i = 0; i < current_headers; i++)
    {
        free(headers[i].name);
        free(headers[i].value);
    }
    free(headers);
    
    return http_error;
}

http_error_code http_validate_message(http_message_t *parsed_message)
{
    /*** Validate Request Line ***/

    /* Check HTTP version */

    int major_version = parsed_message->request_line.hhtp_major_version;

    if (major_version != 1)
        return HTTP_Version_Not_Supported;
    
    /* Check target resource */

    char *target_resource = parsed_message->request_line.target_resource;
    printf("Target Resource: %s\n", target_resource);

    FILE *file_fd = fopen(target_resource, "r");

    if (file_fd == NULL)
        return Not_Found;

    fclose(file_fd);

    /* Check method */

    http_error_code error = Method_Not_Allowed;
    char *method = parsed_message->request_line.method;
    
    for (int i = 0; i < (sizeof(methods_allowed_cheatTable)/sizeof(methods_alowed_t)); i++)
    {
        if (strcmp(target_resource, methods_allowed_cheatTable[i].filename) == 0)
        {
            for (int j = 0; j < METHOD_COUNT; j++)
            {
                if (strcmp(method, methods_allowed_cheatTable[i].methods[j]) == 0)
                {
                    error = Ok;
                    break;
                }
            }
        }
    }

    if (error != Ok)
        return error;
    
    /*** Validate Headers Fields ***/

    /* Check for BWS (Bad White Space) */

    for (int i = 0; i < parsed_message->fields->capacity; i++)
    {
        if (parsed_message->fields->buckets[i] == NULL)
            continue;

        if (strchr(parsed_message->fields->buckets[i]->key, ' ') != NULL)
            return Bad_Request;
        
        // char *ws_pos = strchr(parsed_message->fields->buckets[i]->value, ' ');
        // if ((ws_pos != NULL) && (*(ws_pos-1) != ';'))
        //     return Bad_Request;
    }

    /* Check for host header field*/

    Entry_t *host_header = hash_search_table(parsed_message->fields, "host");
    if (host_header == NULL)
        return Bad_Request;
    
    return Ok;
}

char *method_action(http_message_t *parsed_message)
{
    uint8_t method_code;
    for (int i = 0; i < METHOD_COUNT; i++)
    {
        if(strcmp(http_methods_name[i], parsed_message->request_line.method) == 0)
        {
            method_code = i;
            break;
        }
    }

    char *response_body = NULL;
    const char *filename = parsed_message->request_line.target_resource;
    FILE *file_fd = NULL;

    switch (method_code)
    {
    case GET:
        
        file_fd = fopen(filename, "r");
        if (file_fd == NULL)
            return NULL;
        
        if (fseek(file_fd, 0, SEEK_END) != 0)
        {
            fclose(file_fd);
            return NULL;
        }

        long file_size = ftell(file_fd);
        if (file_size < 0)
        {
            fclose(file_fd);
            return NULL;
        }
        
        rewind(file_fd);

        char *file_content = malloc((file_size + 1)* sizeof(char));
        if (file_content == NULL)
        {
            fclose(file_fd);
            return NULL;
        }
        
        size_t total_bytes_read = fread(file_content, 1, file_size, file_fd);
        if (total_bytes_read != (size_t)file_size)
        {
            free(file_content);
            fclose(file_fd);
            return NULL;
        }

        file_content[file_size] = '\0';
        fclose(file_fd);

        response_body = malloc(sizeof(char) * RESPONSE_BODY_SIZE);
        if (response_body == NULL)
        {
            free(file_content);
            return NULL;
        }
        
        snprintf(response_body, RESPONSE_BODY_SIZE, "content-Type: text/plain\r\n"
                                                    "content-Length: %d\r\n"
                                                    "connection: close\r\n"
                                                    "\r\n"
                                                    "%s",
                                                    (int)file_size, file_content);
        
        free(file_content);
        break;

    case POST:

        file_fd = fopen(filename, "w");
        if (file_fd == NULL)
            return NULL;
        
        Entry_t *content = hash_search_table(parsed_message->fields, "content-length");
        int content_size = atoi(content->value);

        size_t total_written = 0;
        while (total_written != (size_t)content_size)
        {
            size_t written = fwrite(parsed_message->content + total_written, 1, (size_t)content_size - total_written, file_fd);
            if (written == 0) 
            {
                fclose(file_fd);
                return NULL;
            }
            total_written += written;
        }
        fclose(file_fd);

        response_body = DEFAULT_RESPONSE;
        break;

    default:
        return NULL;
        break;
    }

    return response_body;
}

const char *http_build_response(http_error_code error, http_message_t *parsed_message)
{
    /* Build Response Line */

    char status_line[STATUS_LINE_SIZE];
    snprintf(status_line, STATUS_LINE_SIZE, "HTTP/1.1 %d %s\r\n", error, get_http_error_name(error));

    /* Build Response Body */

    char *response_body = NULL;

    switch (error)
    {
    case Bad_Request:
        response_body = DEFAULT_RESPONSE;
        break;

    case Not_Found:
        response_body = DEFAULT_RESPONSE;
        break;
    
    case Content_Too_Large:
        response_body = DEFAULT_RESPONSE;
        break;
    
    case HTTP_Version_Not_Supported:
        response_body = "Content-Type: text/plain\r\n"
                        "Content-Length: 28\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Versions supported: 1.0, 1.1";
        break;

    case Ok:
        response_body = method_action(parsed_message);
        /* Fallthrough if something went wrong with the generation of the response body */
        if(response_body != NULL)
            break;
    
    case Internal_Server_Error:
    default:
        response_body = DEFAULT_RESPONSE;
        break;
    }
    
   

    size_t status_line_length = strlen(status_line);
    size_t response_body_length = strlen(response_body);
    size_t response_size = status_line_length + response_body_length;

    char *response = malloc((response_size+1) * sizeof(char));
    memcpy(response, status_line, status_line_length);
    memcpy(response+status_line_length, response_body, response_body_length);
    response[response_size] = '\0';

    return (const char *)response;
}

void *handle_client(void *arg)
{
    int client_fd = *((int *)arg);
    char *message = (char *)malloc(BUFFER_SIZE * sizeof(char));

    /* Receive request data from client and store into buffer */
    ssize_t bytes_received = recv(client_fd, message, BUFFER_SIZE, 0);
    if (bytes_received > 0)
    {
        printf("Received: %ld bytes\n%s\n", bytes_received, message);

        http_message_t *parsed_message = malloc(sizeof(http_message_t));

        http_error_code http_error = http_parse_message(message, bytes_received, parsed_message);

        if (http_error == Ok)
        {
            http_error = http_validate_message(parsed_message);

        }else if(http_error == No_Content)
        {
            Entry_t *content_length_header = hash_search_table(parsed_message->fields, "content-length");
            uint32_t content_length = atoi(content_length_header->value);
            uint32_t total_bytes_rcv = 0;

            do {
                bytes_received = recv(client_fd, message,content_length, 0);
                total_bytes_rcv += bytes_received;
            }while (bytes_received!=0 && total_bytes_rcv<content_length);
            
            if(total_bytes_rcv != content_length)
            {
                http_error = Bad_Request;
            }else
            {
                parsed_message->content = strstrcpy(message, content_length);
                printf("Content:\n%s\n", parsed_message->content);
                http_error = http_validate_message(parsed_message);
            }
        }else
        {
            printf("Http error not expected\n");
        }

        const char *response = http_build_response(http_error, parsed_message);
        size_t response_len = strlen(response);

        ssize_t bytes_sent = send(client_fd, response, response_len, 0);
        printf("Sent: %ld bytes\n%s\n", bytes_sent, response);

        free(parsed_message);
        free((void *)response);        
    }
    close(client_fd);
    free(arg);
    free(message);
    return NULL;
}

int main(void)
{
    int server_fd;
    struct sockaddr_in server_addr;

    // create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Config socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, 10) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    while (1)
    {
        // Client info
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));

        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        // Accept client connection
        if (*client_fd < 0)
        {
            perror("accept failed");
            continue;
        }
        
        // Create a new thread to handle client request
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, (void *)client_fd);
        pthread_detach(thread_id);
    }
    
    return 0;
}