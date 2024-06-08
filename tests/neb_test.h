#ifndef NEB_TEST_H
#define NEB_TEST_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define panic(fmt, ...) do { \
    fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, (fmt), ##__VA_ARGS__); \
    exit(1); \
    } while(0)

#define panic_prefix(prefix, fmt, ...) do { \
    fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, (prefix)); \
    fprintf(stderr, fmt, ##__VA_ARGS__); \
    exit(1); \
    } while(0)

#define assert(cond, ...) do { \
    if (!(cond)) { \
        panic_prefix("Assertion failed:",  "%s", #cond); \
    } \
    } while(0)

#endif
