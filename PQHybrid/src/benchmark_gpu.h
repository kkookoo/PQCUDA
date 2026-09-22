#ifndef PQCUDA_BENCHMARK_GPU_H
#define PQCUDA_BENCHMARK_GPU_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Returns zero on CUDA discovery failure; candidates are ascending. */
size_t pqcuda_benchmark_workloads(size_t maximum, size_t *counts, size_t capacity);
size_t pqcuda_dilithium_benchmark_workloads(size_t maximum, size_t *counts, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
