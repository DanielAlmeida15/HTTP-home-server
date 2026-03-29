#ifndef UTILS_H
#define UTILS_H

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define TRUE 1
#define FALSE 0

#define PACKED __attribute__((packed))

char *strstrcpy(const char *src, size_t length);

void lowercase(char *string, size_t length);

#endif // UTILS_H