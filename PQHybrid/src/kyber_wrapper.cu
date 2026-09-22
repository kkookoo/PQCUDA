#include "pqcuda.h"
#include "benchmark_policy.h"
#include "pqcuda_random.h"

#include "indcpa.h"
#include "params.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

#include "fips202_cpu/fips202.h"

int SELECTED_GPU = GPU_V100;

namespace {

void conditional_move(uint8_t *destination, const uint8_t *source,
                      size_t size, uint8_t condition) {
    const uint8_t mask = static_cast<uint8_t>(-condition);
    for (size_t i = 0; i < size; ++i)
        destination[i] ^= mask & (destination[i] ^ source[i]);
}

struct KyberGpuContext {
    poly_set4 set{};
    cudaStream_t stream{};
    bool initialized{true};

    explicit KyberGpuContext(size_t capacity) {
        auto allocate = [&](auto **pointer, size_t bytes) {
            if (cudaMalloc(reinterpret_cast<void **>(pointer), bytes) != cudaSuccess)
                initialized = false;
        };
        if (cudaStreamCreate(&stream) != cudaSuccess) initialized = false;
        allocate(&set.a, sizeof(poly));
        allocate(&set.b, sizeof(poly));
        allocate(&set.c, sizeof(poly));
        allocate(&set.d, sizeof(poly));
        allocate(&set.av, sizeof(polyvec));
        allocate(&set.bv, sizeof(polyvec));
        allocate(&set.cv, sizeof(polyvec));
        allocate(&set.dv, sizeof(polyvec));
        allocate(&set.ev, sizeof(polyvec));
        allocate(&set.fv, sizeof(polyvec));
        allocate(&set.AV, sizeof(polyvec) * KYBER_K);
        allocate(&set.seed, KYBER_SYMBYTES * 2 * capacity);
        allocate(&set.large_buffer_a, LARGE_BUFFER_SZ * capacity);
        allocate(&set.large_buffer_b, LARGE_BUFFER_SZ * capacity);
    }

    ~KyberGpuContext() {
        cudaFree(set.a); cudaFree(set.b); cudaFree(set.c); cudaFree(set.d);
        cudaFree(set.av); cudaFree(set.bv); cudaFree(set.cv);
        cudaFree(set.dv); cudaFree(set.ev); cudaFree(set.fv);
        cudaFree(set.AV); cudaFree(set.seed);
        cudaFree(set.large_buffer_a); cudaFree(set.large_buffer_b);
        cudaStreamDestroy(stream);
    }

    bool valid() const { return initialized; }
};

template <typename T>
bool device_alloc(T **pointer, size_t size) {
    return cudaMalloc(reinterpret_cast<void **>(pointer), size) == cudaSuccess;
}

bool valid_batch(size_t batch) {
    return batch > 0 && batch <= N_TESTS;
}

int cpa_keypair_batch(uint8_t *pk, uint8_t *sk, size_t batch) {
    KyberGpuContext context(batch);
    uint8_t *d_pk = nullptr, *d_sk = nullptr, *d_random = nullptr;
    std::vector<uint8_t> randomness(batch * 2 * KYBER_SYMBYTES);
    if (!context.valid() || !pqcuda_random_bytes(randomness.data(), randomness.size()) ||
        !device_alloc(&d_pk, batch * KYBER_PUBLICKEYBYTES) ||
        !device_alloc(&d_sk, batch * KYBER_INDCPA_SECRETKEYBYTES) ||
        !device_alloc(&d_random, randomness.size())) return -1;
    cudaMemcpyAsync(d_random, randomness.data(), randomness.size(),
                    cudaMemcpyHostToDevice, context.stream);
    indcpa_keypair(static_cast<int>(batch), &context.set, d_pk, d_sk, d_random,
                   context.stream);
    cudaMemcpyAsync(pk, d_pk, batch * KYBER_PUBLICKEYBYTES, cudaMemcpyDeviceToHost,
                    context.stream);
    cudaMemcpyAsync(sk, d_sk, batch * KYBER_INDCPA_SECRETKEYBYTES,
                    cudaMemcpyDeviceToHost, context.stream);
    const cudaError_t result = cudaStreamSynchronize(context.stream);
    cudaFree(d_pk); cudaFree(d_sk); cudaFree(d_random);
    return result == cudaSuccess ? 0 : -1;
}

int cpa_encrypt_batch(uint8_t *ct, const uint8_t *message, const uint8_t *pk,
                      const uint8_t *coins, size_t batch) {
    KyberGpuContext context(batch);
    uint8_t *d_ct = nullptr, *d_message = nullptr, *d_pk = nullptr,
            *d_coins = nullptr;
    if (!context.valid() || !device_alloc(&d_ct, batch * KYBER_CIPHERTEXTBYTES) ||
        !device_alloc(&d_message, batch * KYBER_SYMBYTES) ||
        !device_alloc(&d_pk, batch * KYBER_PUBLICKEYBYTES) ||
        !device_alloc(&d_coins, batch * KYBER_SYMBYTES)) return -1;
    cudaMemcpyAsync(d_message, message, batch * KYBER_SYMBYTES, cudaMemcpyHostToDevice,
                    context.stream);
    cudaMemcpyAsync(d_pk, pk, batch * KYBER_PUBLICKEYBYTES, cudaMemcpyHostToDevice,
                    context.stream);
    cudaMemcpyAsync(d_coins, coins, batch * KYBER_SYMBYTES, cudaMemcpyHostToDevice,
                    context.stream);
    indcpa_enc(static_cast<int>(batch), &context.set, d_ct, d_message, d_pk,
               d_coins, context.stream);
    cudaMemcpyAsync(ct, d_ct, batch * KYBER_CIPHERTEXTBYTES, cudaMemcpyDeviceToHost,
                    context.stream);
    const cudaError_t result = cudaStreamSynchronize(context.stream);
    cudaFree(d_ct); cudaFree(d_message); cudaFree(d_pk); cudaFree(d_coins);
    return result == cudaSuccess ? 0 : -1;
}

int cpa_decrypt_batch(uint8_t *message, const uint8_t *ct, const uint8_t *sk,
                      size_t batch) {
    KyberGpuContext context(batch);
    uint8_t *d_message = nullptr, *d_ct = nullptr, *d_sk = nullptr;
    if (!context.valid() || !device_alloc(&d_message, batch * KYBER_SYMBYTES) ||
        !device_alloc(&d_ct, batch * KYBER_CIPHERTEXTBYTES) ||
        !device_alloc(&d_sk, batch * KYBER_INDCPA_SECRETKEYBYTES)) return -1;
    cudaMemcpyAsync(d_ct, ct, batch * KYBER_CIPHERTEXTBYTES, cudaMemcpyHostToDevice,
                    context.stream);
    cudaMemcpyAsync(d_sk, sk, batch * KYBER_INDCPA_SECRETKEYBYTES,
                    cudaMemcpyHostToDevice, context.stream);
    indcpa_dec(static_cast<int>(batch), &context.set, d_message, d_ct, d_sk,
               context.stream);
    cudaMemcpyAsync(message, d_message, batch * KYBER_SYMBYTES,
                    cudaMemcpyDeviceToHost,
                    context.stream);
    const cudaError_t result = cudaStreamSynchronize(context.stream);
    cudaFree(d_message); cudaFree(d_ct); cudaFree(d_sk);
    return result == cudaSuccess ? 0 : -1;
}

} // namespace

extern "C" int pqcuda_kyber1024_set_launch_config(
    size_t block_count, size_t threads_per_block) {
    if (block_count == 0 || threads_per_block == 0 ||
        threads_per_block > 1024) return -1;
    indcpa_set_launch_config(static_cast<int>(block_count),
                             static_cast<int>(threads_per_block));
    return 0;
}

namespace {

constexpr double KYBER_PRACTICAL_THROUGHPUT_RATIO = PQCUDA_BENCHMARK_THROUGHPUT_RATIO;
constexpr std::array<int, 8> KYBER_TUNING_BLOCK_SIZES = {
    16, 32, 64, 128, 192, 256, 512, 1024};

struct KyberKernelMeasurement {
    int block_size;
    int grid_size;
    int operation_count;
    double cumulative_latency_ms;
    double latency_per_operation_us;
    double throughput;
};

using KyberKernelMeasurementLists =
    std::array<std::vector<KyberKernelMeasurement>, INDCPA_KERNEL_COUNT>;

KyberKernelMeasurementLists last_tuning_measurements;
std::array<KyberKernelMeasurement, INDCPA_KERNEL_COUNT>
    last_practical_best{};
size_t last_tuned_batch = 0;
bool tuning_results_available = false;

struct KyberTuningWorkspace {
    size_t capacity;
    KyberGpuContext gpu;
    uint8_t *d_public_key{};
    uint8_t *d_secret_key{};
    uint8_t *d_ciphertext{};
    uint8_t *d_message{};
    uint8_t *d_decrypted{};
    uint8_t *d_coins{};
    uint8_t *d_randomness{};
    std::vector<uint8_t> message;
    std::vector<uint8_t> decrypted;
    std::vector<uint8_t> coins;
    std::vector<uint8_t> randomness;
    bool initialized{};

    explicit KyberTuningWorkspace(size_t requested_capacity)
        : capacity(requested_capacity),
          gpu(requested_capacity),
          message(requested_capacity * KYBER_SYMBYTES),
          decrypted(requested_capacity * KYBER_SYMBYTES),
          coins(requested_capacity * KYBER_SYMBYTES),
          randomness(requested_capacity * 2 * KYBER_SYMBYTES) {
        if (!gpu.valid() ||
            !device_alloc(&d_public_key,
                          requested_capacity * KYBER_PUBLICKEYBYTES) ||
            !device_alloc(&d_secret_key,
                          requested_capacity *
                              KYBER_INDCPA_SECRETKEYBYTES) ||
            !device_alloc(&d_ciphertext,
                          requested_capacity * KYBER_CIPHERTEXTBYTES) ||
            !device_alloc(&d_message,
                          requested_capacity * KYBER_SYMBYTES) ||
            !device_alloc(&d_decrypted,
                          requested_capacity * KYBER_SYMBYTES) ||
            !device_alloc(&d_coins,
                          requested_capacity * KYBER_SYMBYTES) ||
            !device_alloc(&d_randomness,
                          requested_capacity * 2 * KYBER_SYMBYTES) ||
            !pqcuda_random_bytes(message.data(), message.size()) ||
            !pqcuda_random_bytes(coins.data(), coins.size()) ||
            !pqcuda_random_bytes(randomness.data(), randomness.size()))
            return;

        if (cudaMemcpyAsync(d_message, message.data(), message.size(),
                            cudaMemcpyHostToDevice, gpu.stream) !=
                cudaSuccess ||
            cudaMemcpyAsync(d_coins, coins.data(), coins.size(),
                            cudaMemcpyHostToDevice, gpu.stream) !=
                cudaSuccess ||
            cudaMemcpyAsync(d_randomness, randomness.data(),
                            randomness.size(), cudaMemcpyHostToDevice,
                            gpu.stream) != cudaSuccess)
            return;
        initialized = cudaStreamSynchronize(gpu.stream) == cudaSuccess;
    }

    ~KyberTuningWorkspace() {
        cudaFree(d_public_key);
        cudaFree(d_secret_key);
        cudaFree(d_ciphertext);
        cudaFree(d_message);
        cudaFree(d_decrypted);
        cudaFree(d_coins);
        cudaFree(d_randomness);
    }

    bool run(int operation_count) {
        if (!initialized || operation_count <= 0 ||
            static_cast<size_t>(operation_count) > capacity)
            return false;
        indcpa_keypair(operation_count, &gpu.set, d_public_key,
                       d_secret_key, d_randomness, gpu.stream);
        indcpa_enc(operation_count, &gpu.set, d_ciphertext, d_message,
                   d_public_key, d_coins, gpu.stream);
        indcpa_dec(operation_count, &gpu.set, d_decrypted, d_ciphertext,
                   d_secret_key, gpu.stream);
        return cudaStreamSynchronize(gpu.stream) == cudaSuccess;
    }

    bool verify(int operation_count) {
        const size_t bytes =
            static_cast<size_t>(operation_count) * KYBER_SYMBYTES;
        if (cudaMemcpyAsync(decrypted.data(), d_decrypted, bytes,
                            cudaMemcpyDeviceToHost, gpu.stream) !=
                cudaSuccess ||
            cudaStreamSynchronize(gpu.stream) != cudaSuccess)
            return false;
        return std::memcmp(message.data(), decrypted.data(), bytes) == 0;
    }
};

bool benchmark_kyber_launch_config(KyberTuningWorkspace &workspace, int operation_count, int block_size, KyberKernelMeasurementLists &measurements) 
{
    const int grid_size = (operation_count + block_size - 1) / block_size;
    std::array<std::array<float, PQCUDA_BENCHMARK_SAMPLE_RUNS>, INDCPA_KERNEL_COUNT> samples{};
    std::array<bool, INDCPA_KERNEL_COUNT> eligible{};

    bool has_eligible_kernel = false;
    for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) 
    {
        const int maximum = indcpa_get_kernel_max_block_size(kernel);
        if (maximum < 0) return false;
        eligible[kernel] = block_size <= maximum;
        has_eligible_kernel = has_eligible_kernel || eligible[kernel];
    }
    if (!has_eligible_kernel) return true;

    // Warm up this candidate at the requested batch without timing it.
    for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) {
        if (eligible[kernel] &&
            indcpa_set_kernel_block_size(kernel, block_size) != 0)
            return false;
    }
    for (int run = 0; run < PQCUDA_BENCHMARK_WARMUP_RUNS; ++run) {
        if (!workspace.run(operation_count) ||
            !workspace.verify(operation_count)) return false;
    }

    for (int trial = 0; trial < PQCUDA_BENCHMARK_SAMPLE_RUNS; ++trial) 
    {
        if (indcpa_tuning_begin(block_size) != 0) return false;
        const bool pipeline_ok = workspace.run(operation_count);
        for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) 
        {
            if (!eligible[kernel]) continue;
            samples[kernel][trial] = indcpa_tuning_total_ms(kernel);
        }
        indcpa_tuning_end();
        if (!pipeline_ok) return false;
        for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) 
        {
            if (eligible[kernel] &&
                (!std::isfinite(samples[kernel][trial]) ||
                 samples[kernel][trial] < 0.0f))
                return false;
        }
        if (!workspace.verify(operation_count)) return false;
    }

    for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) 
    {
        if (!eligible[kernel]) continue;
        std::sort(samples[kernel].begin(), samples[kernel].end());
        const double cumulative_latency_ms =
            (samples[kernel][(PQCUDA_BENCHMARK_SAMPLE_RUNS - 1) / 2] +
             samples[kernel][PQCUDA_BENCHMARK_SAMPLE_RUNS / 2]) / 2.0;
        if (cumulative_latency_ms <= 0.0) return false;
        const double latency_per_operation_us = cumulative_latency_ms * 1000.0 / operation_count;
        const double throughput = static_cast<double>(operation_count) * 1000.0 / cumulative_latency_ms;
        measurements[kernel].push_back({block_size, grid_size, operation_count, cumulative_latency_ms, latency_per_operation_us, throughput});
    }
    return true;
}

} // namespace

extern "C" size_t pqcuda_kyber1024_max_batch_size(void) 
{
    return N_TESTS;
}

extern "C" int pqcuda_kyber1024_tune_launch_profile(size_t batch_size) 
{
    if (!valid_batch(batch_size)) return -1;

    int device = 0;
    cudaDeviceProp properties{};
    if (cudaGetDevice(&device) != cudaSuccess || cudaGetDeviceProperties(&properties, device) != cudaSuccess)
        return -1;

    KyberTuningWorkspace workspace(batch_size);
    if (!workspace.initialized) return -1;

    KyberKernelMeasurementLists measurements;
    tuning_results_available = false;
    last_tuned_batch = 0;
    for (auto &kernel_measurements : last_tuning_measurements)
        kernel_measurements.clear();

    // Disable the legacy all-kernel override while collecting per-kernel data.
    indcpa_set_launch_config(0, 0);
    for (int block_size : KYBER_TUNING_BLOCK_SIZES) 
    {
        if (block_size > properties.maxThreadsPerBlock) continue;
        if (!benchmark_kyber_launch_config(workspace, static_cast<int>(batch_size), block_size, measurements))
            return -1;
    }

    for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) {
        if (measurements[kernel].empty()) return -1;
        std::sort(measurements[kernel].begin(), measurements[kernel].end(),
                  [](const KyberKernelMeasurement &left,
                     const KyberKernelMeasurement &right) {
                      if (left.block_size != right.block_size)
                          return left.block_size < right.block_size;
                      return left.grid_size < right.grid_size;
                  });
        const auto best_it = std::min_element(
            measurements[kernel].begin(), measurements[kernel].end(),
            [](const KyberKernelMeasurement &left,
               const KyberKernelMeasurement &right) {
                return left.cumulative_latency_ms < right.cumulative_latency_ms;
            });
        const KyberKernelMeasurement *practical_best = &*best_it;

        if (indcpa_set_kernel_block_size(kernel, practical_best->block_size) != 0)
            return -1;
        last_practical_best[kernel] = *practical_best;
    }

    last_tuning_measurements = measurements;
    last_tuned_batch = batch_size;
    tuning_results_available = true;
    return 0;
}

extern "C" void pqcuda_kyber1024_print_tuned_kernel_details(void) 
{
    if (!tuning_results_available) 
    {
        std::printf("No Kyber kernel tuning results are available.\n");
        return;
    }

    std::printf("\nKyber1024 per-kernel tuning details (batch=%zu)\n", last_tuned_batch);

    for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) 
    {
        const auto maximum_it = std::max_element(
            last_tuning_measurements[kernel].begin(),
            last_tuning_measurements[kernel].end(),
            [](const KyberKernelMeasurement &left,
               const KyberKernelMeasurement &right) {
                return left.throughput < right.throughput;
            });
        const double threshold = maximum_it->throughput * KYBER_PRACTICAL_THROUGHPUT_RATIO;

        std::printf("\n%s: configurations within 95%% of maximum "
                    "throughput (>= %.3f (ops/s))\n",
                    indcpa_get_kernel_name(kernel), threshold);
        std::printf("+--------+--------+-------------+---------------------+"
                    "-----------------+--------------------+\n"
                    "| Grid   | Block  | Batch       | Median total (ms)   |"
                    " Latency/op (us) | Throughput (ops/s) |\n"
                    "+--------+--------+-------------+---------------------+"
                    "-----------------+--------------------+\n");
        for (const KyberKernelMeasurement &measurement :
             last_tuning_measurements[kernel]) {
            if (measurement.throughput < threshold) continue;
            std::printf("| %6d | %6d | %11d | %19.6f | %15.6f |"
                        " %18.3f |\n",
                        measurement.grid_size, measurement.block_size,
                        measurement.operation_count,
                        measurement.cumulative_latency_ms,
                        measurement.latency_per_operation_us,
                        measurement.throughput);
        }
        std::printf("+--------+--------+-------------+---------------------+"
                    "-----------------+--------------------+\n");
        const KyberKernelMeasurement &practical_best =
            last_practical_best[kernel];
        std::printf("BEST MEDIAN: grid=%d, block=%d, batch=%d, "
                    "median cumulative time (ms)=%.6f, throughput (ops/s)=%.3f\n",
                    practical_best.grid_size, practical_best.block_size,
                    practical_best.operation_count,
                    practical_best.cumulative_latency_ms,
                    practical_best.throughput);
    }
}

extern "C" int pqcuda_kyber1024_export_tuning_csv(const char *path) {
    if (!path || !tuning_results_available) return -1;
    FILE *file = std::fopen(path, "w");
    if (!file) return -1;
    std::fprintf(file, "kernel,batch,block,grid,median_total_ms,throughput_ops_s\n");
    for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) {
        for (const auto &m : last_tuning_measurements[kernel]) {
            std::fprintf(file, "%s,%d,%d,%d,%.9f,%.6f\n",
                         indcpa_get_kernel_name(kernel), m.operation_count,
                         m.block_size, m.grid_size, m.cumulative_latency_ms,
                         m.throughput);
        }
    }
    const bool failed = std::ferror(file) != 0;
    return std::fclose(file) == 0 && !failed ? 0 : -1;
}

extern "C" int pqcuda_kyber1024_apply_launch_profile(const size_t *blocks, size_t count) {
    if (!blocks || count != INDCPA_KERNEL_COUNT) return -1;
    for (size_t i = 0; i < count; ++i) {
        const int maximum = indcpa_get_kernel_max_block_size(static_cast<int>(i));
        if (maximum <= 0 || blocks[i] == 0 || blocks[i] > static_cast<size_t>(maximum))
            return -1;
    }
    indcpa_set_launch_config(0, 0);
    for (size_t i = 0; i < count; ++i)
        if (indcpa_set_kernel_block_size(static_cast<int>(i), static_cast<int>(blocks[i])) != 0)
            return -1;
    return 0;
}

extern "C" size_t pqcuda_kyber1024_tuned_kernel_count(void) {
    return INDCPA_KERNEL_COUNT;
}

extern "C" const char *pqcuda_kyber1024_tuned_kernel_name(
    size_t kernel_index) {
    return kernel_index < INDCPA_KERNEL_COUNT
               ? indcpa_get_kernel_name(static_cast<int>(kernel_index))
               : nullptr;
}

extern "C" size_t pqcuda_kyber1024_tuned_kernel_threads(
    size_t kernel_index) {
    if (kernel_index >= INDCPA_KERNEL_COUNT) return 0;
    const int threads =
        indcpa_get_kernel_block_size(static_cast<int>(kernel_index));
    return threads > 0 ? static_cast<size_t>(threads) : 0;
}

extern "C" int pqcuda_kyber1024_keypair(uint8_t *pk, uint8_t *sk) {
    return pqcuda_kyber1024_keypair_batch(pk, sk, 1);
}

extern "C" int pqcuda_kyber1024_encapsulate(uint8_t *ct, uint8_t *ss,
                                              const uint8_t *pk) {
    return pqcuda_kyber1024_encapsulate_batch(ct, ss, pk, 1);
}

extern "C" int pqcuda_kyber1024_decapsulate(uint8_t *ss, const uint8_t *ct,
                                              const uint8_t *sk) {
    return pqcuda_kyber1024_decapsulate_batch(ss, ct, sk, 1);
}

extern "C" int pqcuda_kyber1024_keypair_batch(uint8_t *pk, uint8_t *sk, size_t batch) 
{
    if (!pk || !sk || !valid_batch(batch)) return -1;
    std::vector<uint8_t> cpa_sk(batch * KYBER_INDCPA_SECRETKEYBYTES);
    std::vector<uint8_t> fallback(batch * KYBER_SYMBYTES);
    if (!pqcuda_random_bytes(fallback.data(), fallback.size()) ||
        cpa_keypair_batch(pk, cpa_sk.data(), batch) != 0) return -1;
    for (size_t i = 0; i < batch; ++i) {
        uint8_t *item_sk = sk + i * KYBER_SECRETKEYBYTES;
        const uint8_t *item_pk = pk + i * KYBER_PUBLICKEYBYTES;
        std::memcpy(item_sk, cpa_sk.data() + i * KYBER_INDCPA_SECRETKEYBYTES,
                    KYBER_INDCPA_SECRETKEYBYTES);
        std::memcpy(item_sk + KYBER_INDCPA_SECRETKEYBYTES, item_pk,
                    KYBER_PUBLICKEYBYTES);
        sha3_256(item_sk + KYBER_SECRETKEYBYTES - 64, item_pk,
                 KYBER_PUBLICKEYBYTES);
        std::memcpy(item_sk + KYBER_SECRETKEYBYTES - 32,
                    fallback.data() + i * KYBER_SYMBYTES, KYBER_SYMBYTES);
    }
    return 0;
}

extern "C" int pqcuda_kyber1024_encapsulate_batch(
    uint8_t *ct, uint8_t *ss, const uint8_t *pk, size_t batch) {
    if (!ct || !ss || !pk || !valid_batch(batch)) return -1;
    std::vector<uint8_t> messages(batch * KYBER_SYMBYTES);
    std::vector<uint8_t> coins(batch * KYBER_SYMBYTES);
    std::vector<uint8_t> prekeys(batch * KYBER_SYMBYTES);
    std::vector<uint8_t> random(batch * KYBER_SYMBYTES);
    if (!pqcuda_random_bytes(random.data(), random.size())) return -1;
    for (size_t i = 0; i < batch; ++i) {
        std::array<uint8_t, 64> buffer{}, kr{};
        sha3_256(buffer.data(), random.data() + i * KYBER_SYMBYTES,
                 KYBER_SYMBYTES);
        sha3_256(buffer.data() + 32, pk + i * KYBER_PUBLICKEYBYTES,
                 KYBER_PUBLICKEYBYTES);
        sha3_512(kr.data(), buffer.data(), buffer.size());
        std::memcpy(prekeys.data() + i * KYBER_SYMBYTES, kr.data(), KYBER_SYMBYTES);
        std::memcpy(messages.data() + i * KYBER_SYMBYTES, buffer.data(),
                    KYBER_SYMBYTES);
        std::memcpy(coins.data() + i * KYBER_SYMBYTES, kr.data() + 32,
                    KYBER_SYMBYTES);
    }
    if (cpa_encrypt_batch(ct, messages.data(), pk, coins.data(), batch) != 0)
        return -1;
    for (size_t i = 0; i < batch; ++i) {
        std::array<uint8_t, 64> kr{};
        std::memcpy(kr.data(), prekeys.data() + i * KYBER_SYMBYTES,
                    KYBER_SYMBYTES);
        std::memcpy(kr.data() + 32, coins.data() + i * KYBER_SYMBYTES,
                    KYBER_SYMBYTES);
        sha3_256(kr.data() + 32, ct + i * KYBER_CIPHERTEXTBYTES,
                 KYBER_CIPHERTEXTBYTES);
        shake256(ss + i * KYBER_SSBYTES, KYBER_SSBYTES, kr.data(), kr.size());
    }
    return 0;
}

extern "C" int pqcuda_kyber1024_decapsulate_batch(
    uint8_t *ss, const uint8_t *ct, const uint8_t *sk, size_t batch) {
    if (!ss || !ct || !sk || !valid_batch(batch)) return -1;
    std::vector<uint8_t> cpa_sk(batch * KYBER_INDCPA_SECRETKEYBYTES);
    std::vector<uint8_t> public_keys(batch * KYBER_PUBLICKEYBYTES);
    std::vector<uint8_t> messages(batch * KYBER_SYMBYTES);
    std::vector<uint8_t> coins(batch * KYBER_SYMBYTES);
    std::vector<uint8_t> prekeys(batch * KYBER_SYMBYTES);
    std::vector<uint8_t> comparisons(batch * KYBER_CIPHERTEXTBYTES);
    for (size_t i = 0; i < batch; ++i) {
        const uint8_t *item_sk = sk + i * KYBER_SECRETKEYBYTES;
        std::memcpy(cpa_sk.data() + i * KYBER_INDCPA_SECRETKEYBYTES, item_sk,
                    KYBER_INDCPA_SECRETKEYBYTES);
        std::memcpy(public_keys.data() + i * KYBER_PUBLICKEYBYTES,
                    item_sk + KYBER_INDCPA_SECRETKEYBYTES, KYBER_PUBLICKEYBYTES);
    }
    if (cpa_decrypt_batch(messages.data(), ct, cpa_sk.data(), batch) != 0)
        return -1;
    for (size_t i = 0; i < batch; ++i) {
        std::array<uint8_t, 64> buffer{}, kr{};
        std::memcpy(buffer.data(), messages.data() + i * KYBER_SYMBYTES,
                    KYBER_SYMBYTES);
        std::memcpy(buffer.data() + 32,
                    sk + i * KYBER_SECRETKEYBYTES + KYBER_SECRETKEYBYTES - 64,
                    KYBER_SYMBYTES);
        sha3_512(kr.data(), buffer.data(), buffer.size());
        std::memcpy(prekeys.data() + i * KYBER_SYMBYTES, kr.data(), KYBER_SYMBYTES);
        std::memcpy(coins.data() + i * KYBER_SYMBYTES, kr.data() + 32,
                    KYBER_SYMBYTES);
    }
    if (cpa_encrypt_batch(comparisons.data(), messages.data(),
                          public_keys.data(), coins.data(), batch) != 0) return -1;
    for (size_t i = 0; i < batch; ++i) {
        std::array<uint8_t, 64> kr{};
        std::memcpy(kr.data(), prekeys.data() + i * KYBER_SYMBYTES,
                    KYBER_SYMBYTES);
        std::memcpy(kr.data() + 32, coins.data() + i * KYBER_SYMBYTES,
                    KYBER_SYMBYTES);
        uint8_t different = 0;
        for (size_t j = 0; j < KYBER_CIPHERTEXTBYTES; ++j)
            different |= comparisons[i * KYBER_CIPHERTEXTBYTES + j] ^
                         ct[i * KYBER_CIPHERTEXTBYTES + j];
        different = static_cast<uint8_t>(
            (static_cast<uint16_t>(different) + 255) >> 8);
        sha3_256(kr.data() + 32, ct + i * KYBER_CIPHERTEXTBYTES,
                 KYBER_CIPHERTEXTBYTES);
        conditional_move(kr.data(),
                         sk + i * KYBER_SECRETKEYBYTES + KYBER_SECRETKEYBYTES - 32,
                         KYBER_SYMBYTES, different);
        shake256(ss + i * KYBER_SSBYTES, KYBER_SSBYTES, kr.data(), kr.size());
    }
    return 0;
}
