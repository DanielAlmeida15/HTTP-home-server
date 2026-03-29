#include "../include/utils.h"

char *strstrcpy(const char *src, size_t length)
{
    if (src == NULL)
        return NULL;

    char *dst = malloc(sizeof(char)*(length+1));
    memcpy(dst, src, length);
    dst[length] = '\0';

    return dst;
}

void lowercase(char *string, size_t length)
{
    if(string == NULL)
        return;

    int i = 0;
    while ((string[i] != '\0') && (i<length))
    {
        if (string[i] >= 'A' && string[i] <= 'Z')
            string[i] += 32;

        i++;
    }
}
