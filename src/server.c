#include <regex.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
    size_t count = 0;
    resource_t *table = NULL;

    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char name[64], filename[256], ext_str[32], methods_str[64];
        char require_body_str[4] = "0";
        int fields = sscanf(line, "%63s %255s %31s %63s %3s", name, filename, ext_str, methods_str, require_body_str);
        if (fields < 4) continue;

        resource_t *tmp = realloc(table, (count + 1) * sizeof(resource_t));
        if (tmp == NULL) { free(table); fclose(f); return -1; }
        table = tmp;

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
            if(message->headers.connection != NULL) return Bad_Request;

            message->headers.connection = malloc(sizeof(hdr_connection_t));
            message->headers.connection->keep_alive = (strstr(field, "keep-alive") != NULL) ? TRUE : FALSE;
            message->headers.connection->upgrade    = (strstr(field, "upgrade")    != NULL) ? TRUE : FALSE;
            break;

        case HDR_CONTENT_LENGTH:
        {
            if(message->headers.content_length != NULL) return Bad_Request;

            message->headers.content_length = malloc(sizeof(size_t));
            char *end = NULL;
            *(message->headers.content_length) = strtol(field, &end, 10);
            if(end == field || (end != NULL && *end != '\0')) return Bad_Request;
            break;
        }

        case HDR_USER_AGENT:
            if (message->headers.user_agent != NULL) return Bad_Request;

            message->headers.user_agent = strstrcpy(field, field_size);
            break;

        case HDR_CONTENT_TYPE:
            if(message->headers.content_type != NULL) return Bad_Request;

            for (int i=0; i<(sizeof(MimeType)/sizeof(char*)); i++)
            {
                if(strstr(field, MimeType[i]) != NULL)
                {
                    message->headers.content_type = malloc(sizeof(hdr_content_type_t));
                    message->headers.content_type->content_type = i;
                    // TODO: parse charset
                }
            }
            break;

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

    /* Get the request line */

    regex_t request_line_regex;
    regcomp(&request_line_regex, "^([A-Z]+) /([^ ]*) HTTP/([0-9])\\.([0-9])\r\n", REG_EXTENDED);
    regmatch_t matches[5];

    if (regexec(&request_line_regex, message, 5, matches, 0) == 0)
    {
        regfree(&request_line_regex);

        /* Extract method */

        size_t len = matches[1].rm_eo - matches[1].rm_so;
        parsed_message->request_line.method = strstrcpy((message + matches[1].rm_so), len);

        /* Extract target resource (strip query string) */

        len = matches[2].rm_eo - matches[2].rm_so;
        parsed_message->request_line.target_resource = strstrcpy((message + matches[2].rm_so), len);
        char *qs = strchr(parsed_message->request_line.target_resource, '?');
        if (qs != NULL) *qs = '\0';

        /* Treat bare "/" as "home" so the default page is home.html */
        if (parsed_message->request_line.target_resource[0] == '\0')
        {
            free(parsed_message->request_line.target_resource);
            parsed_message->request_line.target_resource = strstrcpy("home", 4);
        }
        if (g_http_debug)
            printf("Target Resource: %s\n", parsed_message->request_line.target_resource);
        /* Extract HTTP version */

        char version[2];
        version[1] = '\0';

        memcpy(version, (message + matches[3].rm_so), 1);
        parsed_message->request_line.http_major_version = atoi(version);

        memcpy(version, (message + matches[4].rm_so), 1);
        parsed_message->request_line.http_minor_version = atoi(version);

    }else
    {
        regfree(&request_line_regex);
        if (g_http_debug) printf("Bad Request Line\n");
        return Bad_Request;
    }

    /* Get and parse headers */

    int header_length = 0, subString_length = 0;
    const char *init_pos = message + matches[0].rm_eo, *end_pos = NULL, delimiter = ':', *delimiter_pos = NULL;
    char header[256];

    while (1)
    {
        end_pos = strstr(init_pos, "\r\n");

        if(end_pos == NULL)
        {
            if (g_http_debug) printf("Didn't find CRLF at the end of line\n");
            http_error = Bad_Request;
            goto cleanup;

        }else if(end_pos-init_pos < 1)
        {
            break;
        }

        header_length = end_pos-init_pos;
        if (header_length > 256)
        {
            if (g_http_debug) printf("Header length higher than the size supported\n");
            http_error = Internal_Server_Error;
            goto cleanup;
        }
        memcpy(header, init_pos, header_length);
        header[header_length] = '\0';

        delimiter_pos = strchr(header, delimiter);

        if (delimiter_pos == NULL)
        {
            if (g_http_debug) printf("Didn't find %c in %s\n", delimiter, header);
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
        if ((end_pos+2-message) == (ptrdiff_t)message_size)
        {
            /* Body hasn't arrived yet — caller must fetch it with another recv() */
            http_error = No_Content;
        }
        else
        {
            parsed_message->content = strstrcpy(end_pos+2, *(parsed_message->headers.content_length));
        }
    }

cleanup:
    return http_error;
}

http_error_code http_validate_message(http_message_t *parsed_message)
{
    /*** Validate Request Line ***/

    /* Check HTTP version */

    if (parsed_message->request_line.http_major_version != 1)
        return HTTP_Version_Not_Supported;
    
    /* Check target resource existence, method and if present content type */

    /* Compute method bitmask once */
    uint8_t method_bit = 0;
    for (int j = 0; j < METHOD_COUNT; j++)
    {
        if (strcmp(parsed_message->request_line.method, http_methods_name[j]) == 0)
        {
            method_bit = (uint8_t)(1 << j);
            break;
        }
    }

    http_error_code error = Not_Found;

    /* Pass 1: exact name match (non-directory resources) */
    for (int i = 0; i < (int)g_resource_count && error == Not_Found; i++)
    {
        if (g_resources[i].is_directory) continue;
        if (strcmp(parsed_message->request_line.target_resource, g_resources[i].name) != 0) continue;

        parsed_message->resource_id = i;

        if (strcmp(parsed_message->request_line.method, "POST") == 0)
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
        for (int i = 0; i < (int)g_resource_count; i++)
        {
            if (!g_resources[i].is_directory) continue;

            /* "." means root — matches every path */
            int match = (strcmp(g_resources[i].name, ".") == 0) ? 1
                      : (strncmp(parsed_message->request_line.target_resource,
                                 g_resources[i].name,
                                 strlen(g_resources[i].name)) == 0);
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
        strcmp(parsed_message->request_line.method, "POST") == 0 &&
        (parsed_message->headers.content_length == NULL ||
         *(parsed_message->headers.content_length) == 0))
        return Bad_Request;

    return Ok;
}

char *method_action(http_message_t *parsed_message, size_t *body_size)
{
    if (parsed_message == NULL || body_size == NULL) return NULL;

    uint8_t method_code = METHOD_COUNT;
    for (int i = 0; i < METHOD_COUNT; i++)
    {
        if (strcmp(http_methods_name[i], parsed_message->request_line.method) == 0)
        {
            method_code = i;
            break;
        }
    }

    resource_t *res = &g_resources[parsed_message->resource_id];

    switch (method_code)
    {
    case GET:
    {
        /* Resolve the path and MIME type */
        const char *open_path;
        content_type_t mime;
        char dir_path[512];

        if (res->is_directory)
        {
            size_t prefix_len = (strcmp(res->name, ".") == 0) ? 0 : strlen(res->name);
            const char *suffix = parsed_message->request_line.target_resource + prefix_len;
            if (*suffix == '\0' || strcmp(suffix, "/") == 0) suffix = "/index.html";

            /* Reject path traversal */
            if (strstr(suffix, "..") != NULL) return NULL;

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

        if (g_server_debug)
            printf("Serving: %s  MIME: %s\n", open_path, MimeType[mime]);

        pthread_rwlock_rdlock(&res->rwlock);

        FILE *file_fd = fopen(open_path, "rb");
        if (file_fd == NULL)
        {
            perror(open_path);
            pthread_rwlock_unlock(&res->rwlock);
            return NULL;
        }

        if (fseek(file_fd, 0, SEEK_END) != 0) { fclose(file_fd); pthread_rwlock_unlock(&res->rwlock); return NULL; }
        long file_size = ftell(file_fd);
        if (file_size < 0) { fclose(file_fd); pthread_rwlock_unlock(&res->rwlock); return NULL; }
        rewind(file_fd);

        char *file_data = malloc((size_t)file_size);
        if (file_data == NULL) { fclose(file_fd); pthread_rwlock_unlock(&res->rwlock); return NULL; }

        if (fread(file_data, 1, (size_t)file_size, file_fd) != (size_t)file_size)
        {
            free(file_data); fclose(file_fd); pthread_rwlock_unlock(&res->rwlock); return NULL;
        }
        fclose(file_fd);
        pthread_rwlock_unlock(&res->rwlock);

        hdr_connection_t *conn = parsed_message->headers.connection;
        char headers[512];
        int hlen = snprintf(headers, sizeof(headers),
                            "content-Type: %s\r\n"
                            "content-Length: %ld\r\n"
                            "connection: %s\r\n"
                            "\r\n",
                            MimeType[mime],
                            file_size,
                            (conn != NULL && conn->keep_alive) ? "keep-alive" : "close");

        if (hlen < 0 || hlen >= (int)sizeof(headers)) { free(file_data); return NULL; }

        size_t total = (size_t)hlen + (size_t)file_size;
        char *body = malloc(total);
        if (body == NULL) { free(file_data); return NULL; }

        memcpy(body,         headers,   (size_t)hlen);
        memcpy(body + hlen,  file_data, (size_t)file_size);
        free(file_data);

        *body_size = total;
        return body;
    }

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

size_t http_build_response(http_error_code error, http_message_t *parsed_message, char **response)
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
        body_data = method_action(parsed_message, &body_size);
        if (body_data != NULL) break;
        error = Internal_Server_Error;
        /* fallthrough */
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

    if (g_http_debug)
    {
        printf("Response status line size: %d\n", slen);
        printf("Response body size: %zu\n", body_size);
        printf("Response size: %zu\n", response_size);
    }

    *response = malloc(response_size + 1);
    if (*response == NULL) { free(body_data); return 0; }

    memcpy(*response,               status_line, (size_t)slen);
    memcpy(*response + (size_t)slen, body_data,  body_size);
    (*response)[response_size] = '\0';
    free(body_data);

    return response_size;
}
