#ifndef PQCUDA_BENCHMARK_WORKLOADS_H
#define PQCUDA_BENCHMARK_WORKLOADS_H
#include <stddef.h>

/* Target ~1500 items per step, preserving an integer multiple of the SM count.
 * ML-KEM uses 32 items/SM (one warp of independent items); ML-DSA uses 4
 * items/SM (its widest item grouping per block). Reduce the grouping on large
 * GPUs to avoid steps above 2000 when possible. */
static inline size_t pqcuda_workload_step(size_t sms, size_t items_per_sm) {
    size_t unit, multiple;
    if (!sms || !items_per_sm) return 0;
    while (items_per_sm > 1 && sms > 2000 / items_per_sm)
        items_per_sm /= 2;
    unit = sms * items_per_sm;
    multiple = 1500 / unit;
    if (1500 % unit >= (unit / 2 + unit % 2)) ++multiple;
    if (!multiple) multiple = 1;
    return unit * multiple;
}

/* Small latency probes and the exact maximum are exceptions to SM alignment. */
static inline size_t pqcuda_workload_candidates(size_t maximum, size_t *counts,
                                               size_t capacity, size_t sms, size_t items_per_sm) {
    static const size_t small[] = {1, 32, 128, 512};
    size_t count = 0, i, n;
    const size_t step = pqcuda_workload_step(sms, items_per_sm);
    if (!maximum || !counts || !step) return 0;
#define ADD_WORKLOAD(value) do { \
    if (count >= capacity) return 0; \
    counts[count++] = (value); \
} while (0)
    for (i = 0; i < sizeof(small) / sizeof(small[0]); ++i)
        if (small[i] < maximum) ADD_WORKLOAD(small[i]);
    for (n = step; n < maximum; n += step) {
        ADD_WORKLOAD(n);
        if (maximum - n <= step) break;
    }
    ADD_WORKLOAD(maximum);
#undef ADD_WORKLOAD
    return count;
}
#endif
