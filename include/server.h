#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include "http.h"
#include "utils.h"
#include "config.h"

#define BUFFER_SIZE         1000000
#define STATUS_LINE_SIZE    50
#define RESPONSE_BODY_SIZE  5000

#define DEFAULT_RESPONSE "Content-Type: text/plain\r\n"\
                         "Connection: close\r\n"\
                         "\r\n"

typedef struct resource_s
{
    char              name[64];
    char              filename[256];
    content_type_t    extension;
    uint8_t           allowed_methods;   /* bitmask: (1 << http_methods_code) */
    uint8_t           is_directory;      /* 1 = serve files from directory, name is URL prefix */
    uint8_t           require_body;      /* 1 = POST must have content-length > 0 */
    pthread_rwlock_t  rwlock;
} resource_t;

extern resource_t *g_resources;
extern size_t      g_resource_count;

int  load_resources(const char *config_path);
void free_resources(void);

typedef enum
{
    HDR_HOST = 0,
    HDR_CONNECTION,
    HDR_CONTENT_LENGTH,
    HDR_USER_AGENT,
    HDR_CONTENT_TYPE,
    HDR_ACCEPT,
    HDR_ORIGIN,
    HDR_REFERER,
    HDR_ACCEPT_ENCODING,
    HDR_ACCEPT_LANGUAGE,
    HDR_UNKNOWN
}header_id;

typedef struct hdr_connection_s
{
    uint8_t keep_alive;
    uint8_t upgrade;
}PACKED hdr_connection_t;

typedef struct hdr_content_type_s
{
    content_type_t content_type;
    char*          charset;
}PACKED hdr_content_type_t;

typedef struct hdr_accept_enconding_s
{
    uint8_t gzip;
    uint8_t deflate;
}PACKED hdr_accept_enconding_t;

typedef struct headers_s
{
    char *host;
    char *origin;
    
    hdr_connection_t *connection;

    char *user_agent;

    size_t               *content_length;
    hdr_content_type_t   *content_type;

    hdr_accept_enconding_t *accept_enconding;
    char                   *accept_language;
    char                   *accept;
    
}PACKED headers_t;

typedef struct request_line_s
{
    char    *method;
    char    *target_resource;
    uint8_t http_major_version;
    uint8_t http_minor_version;
    uint8_t method_code;              /* http_methods_code, or METHOD_COUNT if unknown */
}PACKED request_line_t;

typedef struct http_message_s
{
    request_line_t  request_line;
    int             resource_id;
    headers_t       headers;
    const char      *content;
}PACKED http_message_t;

/*----------------------------------------------*/
/*                Functions                     */
/*----------------------------------------------*/

/**
*
*/
http_error_code http_parse_header(http_message_t *message, const char *field, size_t field_size, header_id header_type);

/**
*   @brief
*
*   @param[in]  message         pointer to memory location of the message to be parsed
*   @param[in]  message_size    number of bytes of the message to be parsed
*   @param[out] parsed_message  pointer to memory location where to store the parsed message
*
*   @return
*/
http_error_code http_parse_message(const char *message, size_t message_size, http_message_t *parsed_message);

/**
*   @brief      Validate the HTTP message
*
*   @param[in]  parsed_message  pointer to memory location of the parsed message to be validated
*
*   @return
*/
http_error_code http_validate_message(http_message_t *parsed_message);

/**
*   @brief
*
*   @param[in]  parsed_message  pointer to memory location of the parsed message
*   @param[out] response_body   pointer to memory location to store generated response
*
*   @return     
*/
/* Returns heap-allocated response body (headers + content); caller must free.
   Sets *body_size to the exact byte count (binary-safe). */
char *method_action(http_message_t *parsed_message, size_t *body_size);

/**
*   @brief      Builds an HTTP response based on the given error
*
*   @param[in]  error           HTTP error
*   @param[in]  parsed_message  pointer to memory location of the parsed message
*
*   @return     Size of response message in bytes
*/
size_t http_build_response(http_error_code error, http_message_t *parsed_message, char **response, int client_fd);

/**
*   @brief      Release all heap-owned fields of an http_message_t and zero them.
*               Safe to call repeatedly on the same struct.
*/
void http_message_free(http_message_t *message);

#endif // SERVER_H