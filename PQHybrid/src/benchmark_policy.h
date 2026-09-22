#ifndef PQCUDA_BENCHMARK_POLICY_H
#define PQCUDA_BENCHMARK_POLICY_H

/* Shared by kernel tuning and the final API benchmarks. */
enum {
    PQCUDA_BENCHMARK_WARMUP_RUNS = 3,
    PQCUDA_BENCHMARK_SAMPLE_RUNS = 10
};

#define PQCUDA_BENCHMARK_THROUGHPUT_RATIO 0.95

/* Compare total completion latency, not amortized latency per operation. */
static inline int pqcuda_benchmark_prefer(double throughput, double total_ms, double maximum_throughput, double best_total_ms) 
{
    return throughput >= maximum_throughput * PQCUDA_BENCHMARK_THROUGHPUT_RATIO && total_ms > 0.0 && (best_total_ms <= 0.0 || total_ms < best_total_ms);
}

#endif
