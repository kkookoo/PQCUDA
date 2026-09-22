#include "benchmark_policy.h"
#include "benchmark_workloads.h"
#include "benchmark_profile.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL: %s\n", #x); return 1; \
} } while (0)

int main(void) {
    size_t counts[64], i, count;
    pqcuda_benchmark_profile original = {12000, 3, {32, 64, 128}}, loaded = {0};
    CHECK(pqcuda_workload_step(15, 32) == 1440);
    CHECK(pqcuda_workload_step(20, 32) == 1280);
    CHECK(pqcuda_workload_step(15, 4) == 1500);
    count = pqcuda_workload_candidates(32768, counts, 64, 15, 32);
    CHECK(count == 27 && counts[0] == 1 && counts[count - 1] == 32768);
    CHECK(counts[4] == 1440 && counts[count - 2] == 31680);
    for (i = 1; i < count; ++i) CHECK(counts[i] > counts[i - 1]);
    {
        const size_t sm_counts[] = {1, 15, 20, 40, 80, 108, 132, 256};
        size_t gpu, grouping;
        for (gpu = 0; gpu < sizeof(sm_counts) / sizeof(sm_counts[0]); ++gpu) {
            for (grouping = 4; grouping <= 32; grouping *= 8) {
                size_t step = pqcuda_workload_step(sm_counts[gpu], grouping);
                CHECK(step >= 1000 && step <= 2000);
                count = pqcuda_workload_candidates(32768, counts, 64, sm_counts[gpu], grouping);
                CHECK(count > 4 && counts[count - 1] == 32768);
                for (i = 4; i + 1 < count; ++i) {
                    CHECK(counts[i] % sm_counts[gpu] == 0);
                    CHECK(counts[i] % step == 0);
                    if (i > 4) CHECK(counts[i] - counts[i - 1] == step);
                }
            }
        }
    }
    CHECK(pqcuda_workload_candidates(32768, counts, 4, 15, 32) == 0);
    CHECK(pqcuda_workload_candidates(1, counts, 64, 15, 32) == 1 && counts[0] == 1);
    CHECK(pqcuda_workload_candidates(0, counts, 64, 15, 32) == 0);
    CHECK(pqcuda_workload_candidates(32768, counts, 64, 0, 32) == 0);
    CHECK(pqcuda_workload_candidates(32768, counts, 64, 15, 0) == 0);
    CHECK(pqcuda_profile_save("profile_test.tmp", &original));
    CHECK(pqcuda_profile_load("profile_test.tmp", 32768, 3, &loaded));
    CHECK(loaded.batch == 12000 && loaded.values[2] == 128);
    CHECK(!pqcuda_profile_load("profile_test.tmp", 1000, 3, &loaded));
    CHECK(!pqcuda_profile_load("profile_test.tmp", 32768, 2, &loaded));
    /* A subsequent benchmark replaces the old batch and complete profile,
       including truncation when the replacement has fewer entries. */
    original.batch = 14400;
    original.count = 2;
    original.values[0] = 256;
    original.values[1] = 512;
    CHECK(pqcuda_profile_save("profile_test.tmp", &original));
    CHECK(pqcuda_profile_load("profile_test.tmp", 32768, 2, &loaded));
    CHECK(loaded.batch == 14400 && loaded.count == 2);
    CHECK(loaded.values[0] == 256 && loaded.values[1] == 512);
    {
        FILE *f = fopen("profile_test.tmp", "w");
        CHECK(f != NULL);
        fputs("PQCUDA_PROFILE_V1 12000 3\n32\n", f);
        CHECK(fclose(f) == 0);
        CHECK(!pqcuda_profile_load("profile_test.tmp", 32768, 3, &loaded));
    }
    CHECK(remove("profile_test.tmp") == 0);
    /* A smaller workload can be faster to finish while remaining near peak. */
    CHECK(pqcuda_benchmark_prefer(995.0, 10.0, 1000.0, 20.0));
    CHECK(pqcuda_benchmark_prefer(980.0, 1.0, 1000.0, 20.0));
    CHECK(pqcuda_benchmark_prefer(950.0, 1.0, 1000.0, 20.0));
    CHECK(!pqcuda_benchmark_prefer(949.9, 1.0, 1000.0, 20.0));
    CHECK(!pqcuda_benchmark_prefer(1000.0, 20.0, 1000.0, 10.0));
    CHECK(pqcuda_benchmark_prefer(990.0, 10.0, 1000.0, 0.0));
    CHECK(!pqcuda_benchmark_prefer(1000.0, 10.0, 1000.0, 10.0));
    CHECK(!pqcuda_benchmark_prefer(1000.0, 0.0, 1000.0, 0.0));
    puts("PASS: near-peak throughput and total-latency selection");
    return 0;
}
