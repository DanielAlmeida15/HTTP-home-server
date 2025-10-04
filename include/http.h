#ifndef HTTP_H
#define HTTP_H

#include "hash.h"

typedef enum{
    Continue                      = 100,
    Switching_Protocols           = 101,
    Ok                            = 200,
    Created                       = 201,
    Accepted                      = 202,
    Non_Authoritative_Information = 203,
    No_Content                    = 204,
    Reset_Content                 = 205,
    Partial_Content               = 206,
    Multiple_Choices              = 300,
    Moved_Permanently             = 301,
    Found                         = 302,
    See_Other                     = 303,
    Not_Modified                  = 304,
    Use_Proxy                     = 305,
    RESERVED                      = 306,
    Temporary_Redirect            = 307,
    Permanent_Redirect            = 308,
    Bad_Request                   = 400,
    Unauthorized                  = 401,
    Payment_Required              = 402,
    Forbidden                     = 403,
    Not_Found                     = 404,
    Method_Not_Allowed            = 405,
    Not_Acceptable                = 406,
    Proxy_Authentication_Required = 407,
    Request_Timeout               = 408,
    Conflict                      = 409,
    Gone                          = 410,
    Length_Required               = 411,
    Precondition_Failed           = 412,
    Content_Too_Large             = 413,
    URI_Too_Long                  = 414,
    Unsupported_Media_Type        = 415,
    Range_Not_Satisfiable         = 416,
    Expectation_Failed            = 417,
    RESERVED_2                    = 418,
    Misdirected_Request           = 421,
    Unprocessable_Content         = 422,
    Upgrade_Required              = 426,
    Internal_Server_Error         = 500,
    Not_Implemented               = 501,
    Bad_Gateway                   = 502,
    Service_Unavailable           = 503,
    Gateway_Timeout               = 504,
    HTTP_Version_Not_Supported    = 505

}http_error_code;

#define HTTP_ERRORS \
    X(Continue,                      Continue)\
    X(Switching_Protocols,           Switching_Protocols)\
    X(Ok,                            Ok)\
    X(Created,                       Created)\
    X(Accepted,                      Accepted)\
    X(Non_Authoritative_Information, Non_Authoritative_Information)\
    X(No_Content,                    No_Content)\
    X(Reset_Content,                 Reset_Content)\
    X(Partial_Content,               Partial_Content)\
    X(Multiple_Choices,              Multiple_Choices)\
    X(Moved_Permanently,             Moved_Permanently)\
    X(Found,                         Found)\
    X(See_Other,                     See_Other)\
    X(Not_Modified,                  Not_Modified)\
    X(Use_Proxy,                     Use_Proxy)\
    X(RESERVED,                      RESERVED)\
    X(Temporary_Redirect,            Temporary_Redirect)\
    X(Permanent_Redirect,            Permanent_Redirect)\
    X(Bad_Request,                   Bad_Request)\
    X(Unauthorized,                  Unauthorized)\
    X(Payment_Required,              Payment_Required)\
    X(Forbidden,                     Forbidden)\
    X(Not_Found,                     Not_Found)\
    X(Method_Not_Allowed,            Method_Not_Allowed)\
    X(Not_Acceptable,                Not_Acceptable)\
    X(Proxy_Authentication_Required, Proxy_Authentication_Required)\
    X(Request_Timeout,               Request_Timeout)\
    X(Conflict,                      Conflict)\
    X(Gone,                          Gone)\
    X(Length_Required,               Length_Required)\
    X(Precondition_Failed,           Precondition_Failed)\
    X(Content_Too_Large,             Content_Too_Large)\
    X(URI_Too_Long,                  URI_Too_Long)\
    X(Unsupported_Media_Type,        Unsupported_Media_Type)\
    X(Range_Not_Satisfiable,         Range_Not_Satisfiable)\
    X(Expectation_Failed,            Expectation_Failed)\
    X(RESERVED_2,                    RESERVED_2)\
    X(Misdirected_Request,           Misdirected_Request)\
    X(Unprocessable_Content,         Unprocessable_Content)\
    X(Upgrade_Required,              Upgrade_Required)\
    X(Internal_Server_Error,         Internal_Server_Error)\
    X(Not_Implemented,               Not_Implemented)\
    X(Bad_Gateway,                   Bad_Gateway)\
    X(Service_Unavailable,           Service_Unavailable)\
    X(Gateway_Timeout,               Gateway_Timeout)\
    X(HTTP_Version_Not_Supported,    HTTP_Version_Not_Supported)

typedef struct http_error_s
{
    int code;
    const char *name;
}http_error_t;

extern const http_error_t http_errors[];

const char *get_http_error_name(int code);

#define HTTP_METHODS \
    X(GET)\
    X(POST)\
    X(HEAD)\
    X(PUT)\
    X(DELETE)\
    X(CONNECT)\
    X(OPTIONS)\
    X(TRACE)

#define X(method) method,
typedef enum {
    HTTP_METHODS
    METHOD_COUNT
}http_methods_code;
#undef X

extern const char * const http_methods_name[METHOD_COUNT];

typedef struct request_line_s
{
    char *method;
    char *target_resource;
    int hhtp_major_version;
    int http_minor_version;
}request_line_t;

typedef struct headers_s
{
    char *name;
    char *value;
}headers_t;

typedef struct http_message_s {
    request_line_t  request_line;
    HashTable_t     *fields;
    const char      *content;
}http_message_t;

#endif