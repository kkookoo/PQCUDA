#ifndef PQCUDA_BENCHMARK_PROFILE_H
#define PQCUDA_BENCHMARK_PROFILE_H
#include <stdio.h>
#include <ctype.h>
#include <stddef.h>

enum { PQCUDA_PROFILE_CAPACITY = 32 };
typedef struct {
    size_t batch, count;
    size_t values[PQCUDA_PROFILE_CAPACITY];
} pqcuda_benchmark_profile;

static int pqcuda_profile_save(const char *path, const pqcuda_benchmark_profile *p) {
    FILE *f;
    size_t i;
    int failed;
    if (!p || !p->batch || p->count > PQCUDA_PROFILE_CAPACITY) return 0;
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "PQCUDA_PROFILE_V1 %zu %zu\n", p->batch, p->count);
    for (i = 0; i < p->count; ++i) fprintf(f, "%zu\n", p->values[i]);
    failed = ferror(f);
    return fclose(f) == 0 && !failed;
}

static int pqcuda_profile_load(const char *path, size_t maximum, size_t expected_count,
                               pqcuda_benchmark_profile *profile) {
    pqcuda_benchmark_profile p = {0};
    FILE *f = fopen(path, "r");
    size_t i;
    int c, ok = 0;
    if (!f) return 0;
    if (fscanf(f, "PQCUDA_PROFILE_V1 %zu %zu", &p.batch, &p.count) != 2 ||
        !p.batch || p.batch > maximum || p.count != expected_count ||
        p.count > PQCUDA_PROFILE_CAPACITY) goto done;
    for (i = 0; i < p.count; ++i)
        if (fscanf(f, "%zu", &p.values[i]) != 1) goto done;
    while ((c = fgetc(f)) != EOF) if (!isspace((unsigned char)c)) goto done;
    if (ferror(f)) goto done;
    *profile = p;
    ok = 1;
done:
    fclose(f);
    return ok;
}
#endif
