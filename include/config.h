#ifndef CONFIG_H
#define CONFIG_H

#define PORT            8080
#define RESOURCES_CONF  "resources.conf"
#define CONFIG_CONF     "config.conf"

#define N_CONNECTIONS   10      /* Number of concurrent connections to be supported */

#include <stdio.h>

typedef enum {
    LOG_ERROR = 0,
    LOG_INFO  = 1,
    LOG_DEBUG = 2
} log_level_t;

extern log_level_t  g_log_level;
extern FILE        *g_log_file;

void log_write(log_level_t level, const char *fmt, ...);

#endif // CONFIG_H