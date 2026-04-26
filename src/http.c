#include "../include/http.h"

const char *MimeType[MAX_EXTENSION] = 
{
    [HTML]  = "text/html",
    [TEXT]  = "text/plain",
    [JS]    = "application/javascript",
    [CSS]   = "text/css",
    [JSON]  = "application/json",
    [PNG]   = "image/png",
    [SVG]   = "image/svg+xml",
    [WASM]  = "application/wasm",
    [OTF]   = "font/otf",
    [BIN]   = "application/octet-stream"
};

#define X(name, value) {value, #name},
const http_error_t http_errors[] = {HTTP_ERRORS};
#undef X

#define X(method) #method,
const char * const http_methods_name[METHOD_COUNT] = {HTTP_METHODS};
#undef X

const char *get_http_error_name(int code)
{
    switch (code) {
#define X(name, value) case value: return #name;
        HTTP_ERRORS
#undef X
        default: return "UNKNOWN_ERROR";
    }
}