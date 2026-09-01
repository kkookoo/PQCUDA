#include "pqcuda.h"

#include "indcpa.h"
#include "params.h"

#include <cuda_runtime.h>
#include <sys/random.h>

#include <array>
#include <cfloat>
#include <cstring>
#include <vector>

extern "C" {
void shake256(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen);
void sha3_256(uint8_t out[32], const uint8_t *in, size_t inlen);
void sha3_512(uint8_t out[64], const uint8_t *in, size_t inlen);
}

int SELECTED_GPU = GPU_V100;

namespace {

bool random_bytes(uint8_t *out, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t result = getrandom(out + offset, size - offset, 0);
        if (result <= 0) return false;
        offset += static_cast<size_t>(result);
    }
    return true;
}

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
    if (!context.valid() || !random_bytes(randomness.data(), randomness.size()) ||
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

extern "C" int pqcuda_kyber1024_tune_launch_profile(void) {
    static const int candidate_block_sizes[] = {32, 64, 128, 192, 256};
    float measurements[INDCPA_KERNEL_COUNT]
                      [sizeof(candidate_block_sizes) /
                       sizeof(candidate_block_sizes[0])] = {};
    uint8_t pk[PQCUDA_KYBER1024_PUBLIC_KEY_BYTES];
    uint8_t sk[PQCUDA_KYBER1024_SECRET_KEY_BYTES];
    uint8_t ct[PQCUDA_KYBER1024_CIPHERTEXT_BYTES];
    uint8_t encapsulated[PQCUDA_KYBER1024_SHARED_SECRET_BYTES];
    uint8_t decapsulated[PQCUDA_KYBER1024_SHARED_SECRET_BYTES];

    // Disable the legacy all-kernel override while collecting a per-kernel
    // profile. Each candidate run executes a complete, valid KEM pipeline.
    indcpa_set_launch_config(0, 0);
    for (size_t candidate = 0;
         candidate < sizeof(candidate_block_sizes) /
                         sizeof(candidate_block_sizes[0]);
         ++candidate) {
        if (indcpa_tuning_begin(candidate_block_sizes[candidate]) != 0)
            return -1;
        const int result =
            pqcuda_kyber1024_keypair(pk, sk) != 0 ||
            pqcuda_kyber1024_encapsulate(ct, encapsulated, pk) != 0 ||
            pqcuda_kyber1024_decapsulate(decapsulated, ct, sk) != 0 ||
            std::memcmp(encapsulated, decapsulated,
                        sizeof(encapsulated)) != 0;
        indcpa_tuning_end();
        if (result) return -1;
        for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) {
            measurements[kernel][candidate] =
                indcpa_tuning_average_ms(kernel);
            if (measurements[kernel][candidate] < 0.0f) return -1;
        }
    }

    for (int kernel = 0; kernel < INDCPA_KERNEL_COUNT; ++kernel) {
        float best_time = FLT_MAX;
        int best_block_size = 0;
        for (size_t candidate = 0;
             candidate < sizeof(candidate_block_sizes) /
                             sizeof(candidate_block_sizes[0]);
             ++candidate) {
            if (measurements[kernel][candidate] < best_time) {
                best_time = measurements[kernel][candidate];
                best_block_size = candidate_block_sizes[candidate];
            }
        }
        if (indcpa_set_kernel_block_size(kernel, best_block_size) != 0)
            return -1;
    }
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

extern "C" int pqcuda_kyber1024_keypair_batch(uint8_t *pk, uint8_t *sk,
                                                size_t batch) {
    if (!pk || !sk || !valid_batch(batch)) return -1;
    std::vector<uint8_t> cpa_sk(batch * KYBER_INDCPA_SECRETKEYBYTES);
    std::vector<uint8_t> fallback(batch * KYBER_SYMBYTES);
    if (!random_bytes(fallback.data(), fallback.size()) ||
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
    std::vector<uint8_t> random(batch * KYBER_SYMBYTES);
    if (!random_bytes(random.data(), random.size())) return -1;
    for (size_t i = 0; i < batch; ++i) {
        std::array<uint8_t, 64> buffer{}, kr{};
        sha3_256(buffer.data(), random.data() + i * KYBER_SYMBYTES,
                 KYBER_SYMBYTES);
        sha3_256(buffer.data() + 32, pk + i * KYBER_PUBLICKEYBYTES,
                 KYBER_PUBLICKEYBYTES);
        sha3_512(kr.data(), buffer.data(), buffer.size());
        std::memcpy(messages.data() + i * KYBER_SYMBYTES, buffer.data(),
                    KYBER_SYMBYTES);
        std::memcpy(coins.data() + i * KYBER_SYMBYTES, kr.data() + 32,
                    KYBER_SYMBYTES);
    }
    if (cpa_encrypt_batch(ct, messages.data(), pk, coins.data(), batch) != 0)
        return -1;
    for (size_t i = 0; i < batch; ++i) {
        std::array<uint8_t, 64> kr{};
        std::memcpy(kr.data(), messages.data() + i * KYBER_SYMBYTES,
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
        std::memcpy(coins.data() + i * KYBER_SYMBYTES, kr.data() + 32,
                    KYBER_SYMBYTES);
    }
    if (cpa_encrypt_batch(comparisons.data(), messages.data(),
                          public_keys.data(), coins.data(), batch) != 0) return -1;
    for (size_t i = 0; i < batch; ++i) {
        std::array<uint8_t, 64> kr{};
        std::memcpy(kr.data(), messages.data() + i * KYBER_SYMBYTES,
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
