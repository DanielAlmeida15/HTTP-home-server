#include "http.h"

#define X(name, value) {value, #name},
const http_error_t http_errors[] = {HTTP_ERRORS};
#undef X

#define X(method) #method,
const char * const http_methods_name[METHOD_COUNT] = {HTTP_METHODS};
#undef X

const char *get_http_error_name(int code)
{
    for (int i = 0; i < sizeof(http_errors) / sizeof(http_error_t); i++) {
        if (http_errors[i].code == code)
            return http_errors[i].name;
    }
    return "UNKNOWN_ERROR";
}