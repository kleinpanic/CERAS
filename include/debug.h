#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

extern int g_debug;  // Global debug flag

#define DEBUG_LOG(fmt, ...)                                      \
    do {                                                         \
        if (g_debug) {                                           \
            fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__);  \
        }                                                        \
    } while (0)

#endif // DEBUG_H

