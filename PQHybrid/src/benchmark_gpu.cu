#include "benchmark_gpu.h"
#include "benchmark_workloads.h"
#include <cuda_runtime.h>
#include <cstdio>

static size_t benchmark_workloads(
    size_t maximum, size_t *counts, size_t capacity, size_t items_per_sm) {
    int device;
    cudaDeviceProp properties{};
    if (!maximum || !counts || cudaGetDevice(&device) != cudaSuccess ||
        cudaGetDeviceProperties(&properties, device) != cudaSuccess) {
        std::fprintf(stderr, "Cannot query the CUDA device.\n");
        return 0;
    }
    std::printf("GPU: %s, SMs=%d, max threads/block=%d\n",
                properties.name, properties.multiProcessorCount,
                properties.maxThreadsPerBlock);
    const size_t sms = static_cast<size_t>(properties.multiProcessorCount);
    std::printf("SM-aligned batch step: %zu (target 1000-2000 items)\n",
                pqcuda_workload_step(sms, items_per_sm));
    return pqcuda_workload_candidates(maximum, counts, capacity, sms, items_per_sm);
}

extern "C" size_t pqcuda_benchmark_workloads(
    size_t maximum, size_t *counts, size_t capacity) {
    return benchmark_workloads(maximum, counts, capacity, 32);
}

extern "C" size_t pqcuda_dilithium_benchmark_workloads(
    size_t maximum, size_t *counts, size_t capacity) {
    // Dilithium maps an item to a warp/block, rather than a single thread.
    return benchmark_workloads(maximum, counts, capacity, 4);
}
