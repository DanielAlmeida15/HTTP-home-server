#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../include/server.h"

resource_t *g_resources     = NULL;
size_t      g_resource_count = 0;

static const struct { const char *str; content_type_t type; } ext_map[] = {
    {"html", HTML}, {"text", TEXT}, {"txt",  TEXT},
    {"js",   JS},   {"css",  CSS},  {"json", JSON},
    {"png",  PNG},  {"svg",  SVG},  {"wasm", WASM},
    {"otf",  OTF},  {"bin",  BIN},  {"frag", BIN}
};

static content_type_t content_type_from_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL) return BIN;
    dot++;
    for (size_t i = 0; i < sizeof(ext_map)/sizeof(ext_map[0]); i++)
        if (strcmp(dot, ext_map[i].str) == 0)
            return ext_map[i].type;
    return BIN;
}

int load_resources(const char *config_path)
{
    FILE *f = fopen(config_path, "r");
    if (f == NULL) return -1;

    char line[512];
    size_t count    = 0;
    size_t capacity = 0;
    resource_t *table = NULL;

    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char name[64], filename[256], ext_str[32], methods_str[64];
        char require_body_str[4] = "0";
        int fields = sscanf(line, "%63s %255s %31s %63s %3s", name, filename, ext_str, methods_str, require_body_str);
        if (fields < 4) continue;

        if (count == capacity)
        {
            size_t new_cap = (capacity == 0) ? 8 : capacity * 2;
            resource_t *tmp = realloc(table, new_cap * sizeof(resource_t));
            if (tmp == NULL) { free(table); fclose(f); return -1; }
            table    = tmp;
            capacity = new_cap;
        }

        strncpy(table[count].name,     name,     sizeof(table[count].name)     - 1);
        table[count].name[sizeof(table[count].name) - 1] = '\0';
        strncpy(table[count].filename, filename, sizeof(table[count].filename) - 1);
        table[count].filename[sizeof(table[count].filename) - 1] = '\0';

        table[count].is_directory  = (strcmp(ext_str, "dir") == 0) ? 1 : 0;
        table[count].require_body  = (strcmp(require_body_str, "1") == 0) ? 1 : 0;
        table[count].extension = HTML;
        if (!table[count].is_directory)
        {
            for (size_t i = 0; i < sizeof(ext_map)/sizeof(ext_map[0]); i++)
            {
                if (strcmp(ext_str, ext_map[i].str) == 0)
                {
                    table[count].extension = ext_map[i].type;
                    break;
                }
            }
        }

        table[count].allowed_methods = 0;
        char *token = strtok(methods_str, ",");
        while (token != NULL)
        {
            for (int j = 0; j < METHOD_COUNT; j++)
            {
                if (strcmp(token, http_methods_name[j]) == 0)
                {
                    table[count].allowed_methods |= (uint8_t)(1 << j);
                    break;
                }
            }
            token = strtok(NULL, ",");
        }

        pthread_rwlock_init(&table[count].rwlock, NULL);
        count++;
    }

    fclose(f);
    free_resources();
    g_resources      = table;
    g_resource_count = count;
    return 0;
}

void free_resources(void)
{
    for (size_t i = 0; i < g_resource_count; i++)
        pthread_rwlock_destroy(&g_resources[i].rwlock);
    free(g_resources);
    g_resources      = NULL;
    g_resource_count = 0;
}

const char *known_headers[HDR_UNKNOWN] = 
{
    [HDR_HOST]             = "host",
    [HDR_CONNECTION]       = "connection",
    [HDR_CONTENT_LENGTH]   = "content-length",
    [HDR_USER_AGENT]       = "user-agent",
    [HDR_CONTENT_TYPE]     = "content-type",
    [HDR_ACCEPT]           = "accept",
    [HDR_ORIGIN]           = "origin",
    [HDR_REFERER]          = "referer",
    [HDR_ACCEPT_ENCODING]  = "accept-encoding",
    [HDR_ACCEPT_LANGUAGE]  = "accept-language"
};

http_error_code http_parse_header(http_message_t *message, const char *field, size_t field_size, header_id header_type)
{
    switch (header_type)
    {
        case HDR_HOST:
            if(message->headers.host != NULL) return Bad_Request;

            message->headers.host = strstrcpy(field, field_size);
            break;

        case HDR_CONNECTION:
        {
            if (message->headers.connection != NULL) return Bad_Request;

            /* Header value is case-insensitive — compare on a lowercased copy. */
            char *lower = strstrcpy(field, field_size);
            if (lower == NULL) return Internal_Server_Error;
            lowercase(lower, field_size);

            message->headers.connection = malloc(sizeof(hdr_connection_t));
            if (message->headers.connection == NULL) { free(lower); return Internal_Server_Error; }
            message->headers.connection->keep_alive = (strstr(lower, "keep-alive") != NULL) ? TRUE : FALSE;
            message->headers.connection->upgrade    = (strstr(lower, "upgrade")    != NULL) ? TRUE : FALSE;
            free(lower);
            break;
        }

        case HDR_CONTENT_LENGTH:
        {
            if (message->headers.content_length != NULL) return Bad_Request;

            /* Reject leading sign/whitespace up front so strtoull can't wrap "-1"
               into a huge size_t. RFC 7230: Content-Length is 1*DIGIT only. */
            if (field[0] < '0' || field[0] > '9') return Bad_Request;

            char *end = NULL;
            errno = 0;
            unsigned long long val = strtoull(field, &end, 10);
            if (end == field || *end != '\0' || errno == ERANGE)
                return Bad_Request;
            if (val > (unsigned long long)SIZE_MAX)
                return Content_Too_Large;

            message->headers.content_length = malloc(sizeof(size_t));
            if (message->headers.content_length == NULL)
                return Internal_Server_Error;
            *(message->headers.content_length) = (size_t)val;
            break;
        }

        case HDR_USER_AGENT:
            if (message->headers.user_agent != NULL) return Bad_Request;

            message->headers.user_agent = strstrcpy(field, field_size);
            break;

        case HDR_CONTENT_TYPE:
        {
            if (message->headers.content_type != NULL) return Bad_Request;

            /* Isolate the media-type token: everything up to ';' or whitespace. */
            size_t type_len = 0;
            while (field[type_len] != '\0' &&
                   field[type_len] != ';'  &&
                   field[type_len] != ' '  &&
                   field[type_len] != '\t')
                type_len++;

            for (size_t i = 0; i < MAX_EXTENSION; i++)
            {
                if (MimeType[i] == NULL) continue;
                if (strlen(MimeType[i]) != type_len) continue;
                if (strncmp(field, MimeType[i], type_len) != 0) continue;

                message->headers.content_type = malloc(sizeof(hdr_content_type_t));
                if (message->headers.content_type == NULL) return Internal_Server_Error;
                message->headers.content_type->content_type = (content_type_t)i;
                message->headers.content_type->charset      = NULL;
                /* TODO: parse charset */
                break;
            }
            break;
        }

        default:
            break;
    }

    return Ok;
}

http_error_code http_parse_message(const char *message, size_t message_size, http_message_t *parsed_message)
{
    if (message == NULL || parsed_message == NULL)
        return Internal_Server_Error;

    memset(parsed_message, 0, sizeof(*parsed_message));

    http_error_code http_error = Ok;

    /* Parse the request line: METHOD SP /TARGET SP HTTP/D.D CRLF */

    const char *p   = message;
    const char *end = message + message_size;

    /* Method: one or more uppercase ASCII letters */
    const char *method_start = p;
    while (p < end && *p >= 'A' && *p <= 'Z') p++;
    if (p == method_start || p >= end || *p != ' ')
    {
        log_write(LOG_DEBUG, "Bad Request Line (method)\n");
        return Bad_Request;
    }
    size_t method_len = (size_t)(p - method_start);
    p++; /* skip SP */

    /* Target: leading '/', then any non-space up to next SP */
    if (p >= end || *p != '/')
    {
        log_write(LOG_DEBUG, "Bad Request Line (target)\n");
        return Bad_Request;
    }
    p++; /* skip leading '/' (not included in captured target) */
    const char *target_start = p;
    while (p < end && *p != ' ' && *p != '\r') p++;
    if (p >= end || *p != ' ')
    {
        log_write(LOG_DEBUG, "Bad Request Line (target terminator)\n");
        return Bad_Request;
    }
    size_t target_len = (size_t)(p - target_start);
    p++; /* skip SP */

    /* Version: literal "HTTP/" D "." D CRLF */
    if ((size_t)(end - p) < 8 || memcmp(p, "HTTP/", 5) != 0)
    {
        log_write(LOG_DEBUG, "Bad Request Line (version prefix)\n");
        return Bad_Request;
    }
    p += 5;
    if (p[0] < '0' || p[0] > '9' || p[1] != '.' || p[2] < '0' || p[2] > '9')
    {
        log_write(LOG_DEBUG, "Bad Request Line (version digits)\n");
        return Bad_Request;
    }
    uint8_t major = (uint8_t)(p[0] - '0');
    uint8_t minor = (uint8_t)(p[2] - '0');
    p += 3;
    if ((size_t)(end - p) < 2 || p[0] != '\r' || p[1] != '\n')
    {
        log_write(LOG_DEBUG, "Bad Request Line (CRLF)\n");
        return Bad_Request;
    }
    p += 2;

    parsed_message->request_line.method              = strstrcpy(method_start, method_len);
    parsed_message->request_line.target_resource    = strstrcpy(target_start, target_len);
    parsed_message->request_line.http_major_version = major;
    parsed_message->request_line.http_minor_version = minor;

    /* Resolve the method to an enum index once — validate/action both use it. */
    parsed_message->request_line.method_code = METHOD_COUNT;
    for (uint8_t j = 0; j < METHOD_COUNT; j++)
    {
        if (strlen(http_methods_name[j]) == method_len &&
            memcmp(method_start, http_methods_name[j], method_len) == 0)
        {
            parsed_message->request_line.method_code = j;
            break;
        }
    }

    /* Strip query string */
    char *qs = strchr(parsed_message->request_line.target_resource, '?');
    if (qs != NULL) *qs = '\0';

    /* Treat bare "/" as "home" so the default page is home.html */
    if (parsed_message->request_line.target_resource[0] == '\0')
    {
        free(parsed_message->request_line.target_resource);
        parsed_message->request_line.target_resource = strstrcpy("home", 4);
    }
    log_write(LOG_DEBUG, "Target Resource: %s\n", parsed_message->request_line.target_resource);

    /* Get and parse headers */

    int header_length = 0, subString_length = 0;
    const char *init_pos = p, *end_pos = NULL, delimiter = ':', *delimiter_pos = NULL;
    char header[256];

    while (1)
    {
        end_pos = strstr(init_pos, "\r\n");

        if(end_pos == NULL)
        {
            log_write(LOG_DEBUG, "Didn't find CRLF at the end of line\n");
            http_error = Bad_Request;
            goto cleanup;

        }else if(end_pos-init_pos < 1)
        {
            break;
        }

        header_length = end_pos-init_pos;
        if ((size_t)header_length >= sizeof(header))
        {
            log_write(LOG_DEBUG, "Header length higher than the size supported\n");
            http_error = Bad_Request;
            goto cleanup;
        }
        memcpy(header, init_pos, header_length);
        header[header_length] = '\0';

        delimiter_pos = strchr(header, delimiter);

        if (delimiter_pos == NULL)
        {
            log_write(LOG_DEBUG, "Didn't find %c in %s\n", delimiter, header);
            http_error = Bad_Request;
            goto cleanup;
        }

        subString_length = delimiter_pos - header;
        header[subString_length] = '\0';
        lowercase(header, subString_length);

        const char *value_pos = delimiter_pos + 1;
        int value_len = header_length - subString_length - 1;
        while (value_len > 0 && *value_pos == ' ') { value_pos++; value_len--; }

        if (strchr(header, ' ') != NULL)
        {
            http_error = Bad_Request;
            goto cleanup;
        }

        for (int i=0; i<HDR_UNKNOWN; i++)
        {
            if (strcmp(header, known_headers[i]) == 0)
            {
                http_parse_header(parsed_message, value_pos, value_len, i);
                break;
            }
        }

        init_pos = end_pos+2;
    }

    if (parsed_message->headers.content_length != NULL &&
        *(parsed_message->headers.content_length) > 0)
    {
        const char *body_start = end_pos + 2;
        size_t body_present    = (size_t)((message + message_size) - body_start);
        size_t content_len     = *(parsed_message->headers.content_length);

        if (body_present >= content_len)
        {
            parsed_message->content = strstrcpy(body_start, content_len);
        }
        else
        {
            /* Full body not here yet — caller must recv() the remaining bytes
               into the buffer (continuing at message + message_size). */
            http_error = No_Content;
        }
    }

cleanup:
    /* Release partial allocations on any error path except No_Content
       (No_Content means the caller will finish the read and retry). */
    if (http_error != Ok && http_error != No_Content)
        http_message_free(parsed_message);
    return http_error;
}

http_error_code http_validate_message(http_message_t *parsed_message)
{
    /*** Validate Request Line ***/

    /* Check HTTP version */

    if (parsed_message->request_line.http_major_version != 1)
        return HTTP_Version_Not_Supported;
    
    /* Check target resource existence, method and if present content type */

    /* Method index was resolved in http_parse_message. */
    uint8_t method_bit = (parsed_message->request_line.method_code < METHOD_COUNT)
                         ? (uint8_t)(1 << parsed_message->request_line.method_code)
                         : 0;

    http_error_code error = Not_Found;

    /* Pass 1: exact name match (non-directory resources) */
    for (int i = 0; i < (int)g_resource_count && error == Not_Found; i++)
    {
        if (g_resources[i].is_directory) continue;
        if (strcmp(parsed_message->request_line.target_resource, g_resources[i].name) != 0) continue;

        parsed_message->resource_id = i;

        if (parsed_message->request_line.method_code == POST)
        {
            if (parsed_message->headers.content_type != NULL &&
                parsed_message->headers.content_type->content_type != g_resources[i].extension)
            {
                error = Not_Found;
                break;
            }
        }

        error = (method_bit && (g_resources[i].allowed_methods & method_bit))
                ? Ok : Method_Not_Allowed;
    }

    /* Pass 2: directory prefix match */
    if (error == Not_Found)
    {
        const char *target = parsed_message->request_line.target_resource;

        for (int i = 0; i < (int)g_resource_count; i++)
        {
            if (!g_resources[i].is_directory) continue;

            int match;
            if (strcmp(g_resources[i].name, ".") == 0)
            {
                /* "." means root — matches every path */
                match = 1;
            }
            else
            {
                size_t name_len = strlen(g_resources[i].name);
                /* Prefix must end at a component boundary: end-of-string or '/'. */
                match = (strncmp(target, g_resources[i].name, name_len) == 0) &&
                        (target[name_len] == '\0' || target[name_len] == '/');
            }
            if (!match) continue;

            parsed_message->resource_id = i;
            error = (method_bit && (g_resources[i].allowed_methods & method_bit))
                    ? Ok : Method_Not_Allowed;
            break;
        }
    }

    if (error != Ok)
        return error;
    
    /*** Validate Headers Fields ***/

    /* Check for host header field*/

    if (parsed_message->headers.host == NULL)
        return Bad_Request;

    /* Reject body-less POST if resource requires one */
    if (g_resources[parsed_message->resource_id].require_body &&
        parsed_message->request_line.method_code == POST &&
        (parsed_message->headers.content_length == NULL ||
         *(parsed_message->headers.content_length) == 0))
        return Bad_Request;

    return Ok;
}

/* Stream a GET response directly to the client via send() + sendfile().
   Returns 1 on success, 0 on any failure (caller falls back to 500). */
static int http_send_get(http_message_t *parsed_message, int client_fd)
{
    resource_t *res = &g_resources[parsed_message->resource_id];

    /* Resolve path and MIME type */
    const char *open_path;
    content_type_t mime;
    char dir_path[512];

    if (res->is_directory)
    {
        size_t prefix_len = (strcmp(res->name, ".") == 0) ? 0 : strlen(res->name);
        const char *suffix = parsed_message->request_line.target_resource + prefix_len;
        if (*suffix == '\0' || strcmp(suffix, "/") == 0) suffix = "/index.html";

        /* Reject path traversal: any component that is literally ".." */
        const char *c = suffix;
        while (*c != '\0')
        {
            if (*c == '/') c++;
            const char *seg = c;
            while (*c != '\0' && *c != '/') c++;
            size_t seg_len = (size_t)(c - seg);
            if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') return 0;
        }

        if (*suffix == '/') suffix++;
        snprintf(dir_path, sizeof(dir_path), "%s%s", res->filename, suffix);
        open_path = dir_path;
        mime      = content_type_from_path(suffix);
    }
    else
    {
        open_path = res->filename;
        mime      = res->extension;
    }

    log_write(LOG_DEBUG, "Serving: %s  MIME: %s\n", open_path, MimeType[mime]);

    pthread_rwlock_rdlock(&res->rwlock);

    int file_fd = open(open_path, O_RDONLY);
    if (file_fd < 0)
    {
        log_write(LOG_ERROR, "%s: %s\n", open_path, strerror(errno));
        pthread_rwlock_unlock(&res->rwlock);
        return 0;
    }

    struct stat st;
    if (fstat(file_fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        close(file_fd);
        pthread_rwlock_unlock(&res->rwlock);
        return 0;
    }
    off_t file_size = st.st_size;

    hdr_connection_t *conn = parsed_message->headers.connection;
    char head[512];
    int hlen = snprintf(head, sizeof(head),
                        "HTTP/1.1 200 Ok\r\n"
                        "content-Type: %s\r\n"
                        "content-Length: %lld\r\n"
                        "connection: %s\r\n"
                        "\r\n",
                        MimeType[mime],
                        (long long)file_size,
                        (conn != NULL && conn->keep_alive) ? "keep-alive" : "close");
    if (hlen < 0 || hlen >= (int)sizeof(head))
    {
        close(file_fd);
        pthread_rwlock_unlock(&res->rwlock);
        return 0;
    }

    /* Send headers */
    size_t head_sent = 0;
    while (head_sent < (size_t)hlen)
    {
        ssize_t n = send(client_fd, head + head_sent, (size_t)hlen - head_sent, MSG_NOSIGNAL);
        if (n <= 0) { close(file_fd); pthread_rwlock_unlock(&res->rwlock); return 0; }
        head_sent += (size_t)n;
    }

    /* Stream body via sendfile — no userspace copy. */
    off_t offset = 0;
    while (offset < file_size)
    {
        ssize_t n = sendfile(client_fd, file_fd, &offset, (size_t)(file_size - offset));
        if (n <= 0) { close(file_fd); pthread_rwlock_unlock(&res->rwlock); return 0; }
    }

    close(file_fd);
    pthread_rwlock_unlock(&res->rwlock);
    return 1;
}

char *method_action(http_message_t *parsed_message, size_t *body_size)
{
    if (parsed_message == NULL || body_size == NULL) return NULL;

    uint8_t method_code = parsed_message->request_line.method_code;
    resource_t *res = &g_resources[parsed_message->resource_id];

    switch (method_code)
    {
    case POST:
    {
        pthread_rwlock_wrlock(&res->rwlock);

        FILE *file_fd = fopen(res->filename, "wb");
        if (file_fd == NULL) { pthread_rwlock_unlock(&res->rwlock); return NULL; }

        if (parsed_message->headers.content_length == NULL) { fclose(file_fd); pthread_rwlock_unlock(&res->rwlock); return NULL; }
        size_t content_size = *(parsed_message->headers.content_length);

        /* content_size == 0 is valid — fopen "wb" already truncated the file */
        size_t total_written = 0;
        while (total_written < content_size)
        {
            size_t written = fwrite(parsed_message->content + total_written, 1,
                                    content_size - total_written, file_fd);
            if (written == 0) { fclose(file_fd); pthread_rwlock_unlock(&res->rwlock); return NULL; }
            total_written += written;
        }
        fclose(file_fd);
        pthread_rwlock_unlock(&res->rwlock);

        size_t dlen = strlen(DEFAULT_RESPONSE);
        char *body = malloc(dlen + 1);
        if (body == NULL) return NULL;
        memcpy(body, DEFAULT_RESPONSE, dlen + 1);
        *body_size = dlen;
        return body;
    }

    default:
        return NULL;
    }
}

size_t http_build_response(http_error_code error, http_message_t *parsed_message, char **response, int client_fd)
{
    static const char hvsb[] = "Content-Type: text/plain\r\n"
                               "Content-Length: 28\r\n"
                               "Connection: close\r\n"
                               "\r\n"
                               "Versions supported: 1.0, 1.1";

    char  *body_data = NULL;
    size_t body_size = 0;

    switch (error)
    {
    case HTTP_Version_Not_Supported:
        body_size = sizeof(hvsb) - 1;
        body_data = malloc(body_size);
        if (body_data) memcpy(body_data, hvsb, body_size);
        break;

    case Ok:
        /* GET streams its own status line + headers + body via sendfile.
           If it succeeds, there's nothing left for us to send. */
        if (parsed_message->request_line.method_code == GET)
        {
            if (http_send_get(parsed_message, client_fd))
            {
                *response = NULL;
                return 0;
            }
            error = Internal_Server_Error;
            goto build_error_body;
        }
        body_data = method_action(parsed_message, &body_size);
        if (body_data != NULL) break;
        error = Internal_Server_Error;
        /* fallthrough */
    build_error_body:
    case Bad_Request:
    case Not_Found:
    case Method_Not_Allowed:
    case Content_Too_Large:
    case Internal_Server_Error:
    default:
        body_size = strlen(DEFAULT_RESPONSE);
        body_data = malloc(body_size + 1);
        if (body_data) memcpy(body_data, DEFAULT_RESPONSE, body_size + 1);
        break;
    }

    if (body_data == NULL) { *response = NULL; return 0; }

    /* Build status line + body into a single allocation */
    char status_line[STATUS_LINE_SIZE];
    int slen = snprintf(status_line, STATUS_LINE_SIZE,
                        "HTTP/1.1 %d %s\r\n", error, get_http_error_name(error));
    if (slen < 0) slen = 0;

    size_t response_size = (size_t)slen + body_size;

    log_write(LOG_DEBUG, "Response status line size: %d\n", slen);
    log_write(LOG_DEBUG, "Response body size: %zu\n", body_size);
    log_write(LOG_DEBUG, "Response size: %zu\n", response_size);

    *response = malloc(response_size + 1);
    if (*response == NULL) { free(body_data); return 0; }

    memcpy(*response,               status_line, (size_t)slen);
    memcpy(*response + (size_t)slen, body_data,  body_size);
    (*response)[response_size] = '\0';
    free(body_data);

    return response_size;
}

void http_message_free(http_message_t *message)
{
    if (message == NULL) return;

    free(message->request_line.method);
    free(message->request_line.target_resource);
    message->request_line.method          = NULL;
    message->request_line.target_resource = NULL;

    free(message->headers.host);
    free(message->headers.connection);
    free(message->headers.content_length);
    free(message->headers.user_agent);
    if (message->headers.content_type != NULL)
    {
        free(message->headers.content_type->charset);
        free(message->headers.content_type);
    }
    free(message->headers.accept_enconding);
    free(message->headers.accept_language);
    free(message->headers.accept);
    free(message->headers.origin);
    message->headers.host             = NULL;
    message->headers.connection       = NULL;
    message->headers.content_length   = NULL;
    message->headers.user_agent       = NULL;
    message->headers.content_type     = NULL;
    message->headers.accept_enconding = NULL;
    message->headers.accept_language  = NULL;
    message->headers.accept           = NULL;
    message->headers.origin           = NULL;

    free((void *)message->content);
    message->content = NULL;
}
