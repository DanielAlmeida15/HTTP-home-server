#ifndef CONFIG_H
#define CONFIG_H

#define PORT            8080
#define RESOURCES_CONF  "resources.conf"
#define CONFIG_CONF     "config.conf"

#define N_CONNECTIONS   10      /* Number of concurrent connections to be supported */

extern int g_http_debug;
extern int g_server_debug;

#endif // CONFIG_H