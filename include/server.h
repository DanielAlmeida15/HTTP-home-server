#include "http.h"
#include <stdlib.h>

#define DEFAULT_RESPONSE "Content-Type: text/plain\r\n"\
                         "Connection: close\r\n"\
                         "\r\n"

void lowercase(char *string, size_t length);

typedef struct methods_alowed_s
{
    const char *filename;
    const char *methods[METHOD_COUNT];
}methods_alowed_t;

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