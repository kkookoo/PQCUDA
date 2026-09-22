#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "cli_platform.h"
#include "pqcuda_random.h"
#include <math.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void) {
    FILE *input = tmpfile();
    char *line = NULL;
    size_t capacity = 0;
    size_t i;
    uint8_t first[4096], second[4096], zero[4096] = {0};
    pqcuda_timestamp start, end;
    double duration;
    CHECK(input != NULL);
    CHECK(pqcuda_read_line(&line, &capacity, input) == -1);
    rewind(input);
    CHECK(fwrite("\nhello\r\n", 1, 8, input) == 8);
    for (i = 0; i < 65536; ++i) CHECK(fputc('a', input) != EOF);
    CHECK(fwrite("\nlast", 1, 5, input) == 5);
    rewind(input);
    CHECK(pqcuda_read_line(&line, &capacity, input) == 1);
    CHECK(strcmp(line, "\n") == 0);
    CHECK(pqcuda_read_line(&line, &capacity, input) == 7);
    CHECK(strcmp(line, "hello\r\n") == 0);
    CHECK(pqcuda_read_line(&line, &capacity, input) == 65537);
    for (i = 0; i < 65536; ++i) CHECK(line[i] == 'a');
    CHECK(line[65536] == '\n' && line[65537] == '\0');
    CHECK(pqcuda_read_line(&line, &capacity, input) == 4);
    CHECK(strcmp(line, "last") == 0);
    CHECK(pqcuda_read_line(&line, &capacity, input) == -1);
    free(line);
    CHECK(fclose(input) == 0);

    CHECK(pqcuda_random_bytes(NULL, 0));
    CHECK(!pqcuda_random_bytes(NULL, 1));
    CHECK(pqcuda_monotonic_now(&start));
    CHECK(pqcuda_random_bytes(first, sizeof(first)));
    CHECK(pqcuda_random_bytes(second, sizeof(second)));
    CHECK(pqcuda_monotonic_now(&end));
    CHECK(memcmp(first, zero, sizeof(first)) != 0);
    CHECK(memcmp(first, second, sizeof(first)) != 0);
    duration = pqcuda_elapsed_ms(start, end);
    CHECK(isfinite(duration) && duration >= 0.0);
    CHECK(pqcuda_elapsed_ms(start, start) == 0.0);
#ifdef _WIN32
    {
        LARGE_INTEGER frequency;
        CHECK(QueryPerformanceFrequency(&frequency));
        end.QuadPart = start.QuadPart + frequency.QuadPart;
    }
#else
    end = start;
    ++end.tv_sec;
#endif
    CHECK(fabs(pqcuda_elapsed_ms(start, end) - 1000.0) < 0.000001);
    puts("PASS: line input, OS randomness, and monotonic timing");
    return 0;
}
