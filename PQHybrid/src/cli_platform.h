#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
typedef LARGE_INTEGER pqcuda_timestamp;
#else
#include <time.h>
typedef struct timespec pqcuda_timestamp;
#endif

/* getline semantics, including the newline and a final unterminated line. */
static inline ptrdiff_t pqcuda_read_line(char **line, size_t *capacity, FILE *input) 
{
    size_t length = 0;
    int c;
    if (*line == NULL || *capacity == 0) 
    {
        char *buffer = (char *)realloc(*line, 128);
        if (buffer == NULL) return -1;
        *line = buffer;
        *capacity = 128;
    }
    while ((c = fgetc(input)) != EOF) 
    {
        if (length == PTRDIFF_MAX) return -1;
        if (length + 1 >= *capacity) 
        {
            size_t next_capacity;
            char *buffer;
            if (*capacity > SIZE_MAX / 2) return -1;
            next_capacity = *capacity * 2;
            buffer = (char *)realloc(*line, next_capacity);
            if (buffer == NULL) return -1;
            *line = buffer;
            *capacity = next_capacity;
        }
        (*line)[length++] = (char)c;
        if (c == '\n') break;
    }
    (*line)[length] = '\0';
    if (ferror(input) || (c == EOF && length == 0)) return -1;
    return (ptrdiff_t)length;
}

static inline int pqcuda_monotonic_now(pqcuda_timestamp *value) 
{
#ifdef _WIN32
    return QueryPerformanceCounter(value) != 0;
#else
    return clock_gettime(CLOCK_MONOTONIC, value) == 0;
#endif
}

static inline double pqcuda_elapsed_ms(pqcuda_timestamp start, pqcuda_timestamp end) 
{
#ifdef _WIN32
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return -1.0;
    return (double)(end.QuadPart - start.QuadPart) * 1000.0 /
           (double)frequency.QuadPart;
#else
    return (double)(end.tv_sec - start.tv_sec) * 1000.0 +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
#endif
}
