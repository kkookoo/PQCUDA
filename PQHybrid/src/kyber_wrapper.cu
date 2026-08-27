#include "pqcuda.h"

#include "indcpa.h"
#include "params.h"

#include <cuda_runtime.h>
#include <sys/random.h>

#include <array>
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

    KyberGpuContext() {
        cudaStreamCreate(&stream);
        cudaMalloc(&set.a, sizeof(poly));
        cudaMalloc(&set.b, sizeof(poly));
        cudaMalloc(&set.c, sizeof(poly));
        cudaMalloc(&set.d, sizeof(poly));
        cudaMalloc(&set.av, sizeof(polyvec));
        cudaMalloc(&set.bv, sizeof(polyvec));
        cudaMalloc(&set.cv, sizeof(polyvec));
        cudaMalloc(&set.dv, sizeof(polyvec));
        cudaMalloc(&set.ev, sizeof(polyvec));
        cudaMalloc(&set.fv, sizeof(polyvec));
        cudaMalloc(&set.AV, sizeof(polyvec) * KYBER_K);
        cudaMalloc(&set.seed, KYBER_SYMBYTES * 2);
        cudaMalloc(&set.large_buffer_a, LARGE_BUFFER_SZ);
        cudaMalloc(&set.large_buffer_b, LARGE_BUFFER_SZ);
    }

    ~KyberGpuContext() {
        cudaFree(set.a); cudaFree(set.b); cudaFree(set.c); cudaFree(set.d);
        cudaFree(set.av); cudaFree(set.bv); cudaFree(set.cv);
        cudaFree(set.dv); cudaFree(set.ev); cudaFree(set.fv);
        cudaFree(set.AV); cudaFree(set.seed);
        cudaFree(set.large_buffer_a); cudaFree(set.large_buffer_b);
        cudaStreamDestroy(stream);
    }

    bool valid() const { return cudaGetLastError() == cudaSuccess; }
};

template <typename T>
bool device_alloc(T **pointer, size_t size) {
    return cudaMalloc(reinterpret_cast<void **>(pointer), size) == cudaSuccess;
}

int cpa_keypair(uint8_t *pk, uint8_t *sk) {
    KyberGpuContext context;
    uint8_t *d_pk = nullptr, *d_sk = nullptr, *d_random = nullptr;
    std::array<uint8_t, 2 * KYBER_SYMBYTES> randomness{};
    if (!context.valid() || !random_bytes(randomness.data(), randomness.size()) ||
        !device_alloc(&d_pk, KYBER_PUBLICKEYBYTES) ||
        !device_alloc(&d_sk, KYBER_INDCPA_SECRETKEYBYTES) ||
        !device_alloc(&d_random, randomness.size())) return -1;
    cudaMemcpyAsync(d_random, randomness.data(), randomness.size(),
                    cudaMemcpyHostToDevice, context.stream);
    indcpa_keypair(1, &context.set, d_pk, d_sk, d_random, context.stream);
    cudaMemcpyAsync(pk, d_pk, KYBER_PUBLICKEYBYTES, cudaMemcpyDeviceToHost,
                    context.stream);
    cudaMemcpyAsync(sk, d_sk, KYBER_INDCPA_SECRETKEYBYTES,
                    cudaMemcpyDeviceToHost, context.stream);
    const cudaError_t result = cudaStreamSynchronize(context.stream);
    cudaFree(d_pk); cudaFree(d_sk); cudaFree(d_random);
    return result == cudaSuccess ? 0 : -1;
}

int cpa_encrypt(uint8_t *ct, const uint8_t *message, const uint8_t *pk,
                const uint8_t *coins) {
    KyberGpuContext context;
    uint8_t *d_ct = nullptr, *d_message = nullptr, *d_pk = nullptr,
            *d_coins = nullptr;
    if (!context.valid() || !device_alloc(&d_ct, KYBER_CIPHERTEXTBYTES) ||
        !device_alloc(&d_message, KYBER_SYMBYTES) ||
        !device_alloc(&d_pk, KYBER_PUBLICKEYBYTES) ||
        !device_alloc(&d_coins, KYBER_SYMBYTES)) return -1;
    cudaMemcpyAsync(d_message, message, KYBER_SYMBYTES, cudaMemcpyHostToDevice,
                    context.stream);
    cudaMemcpyAsync(d_pk, pk, KYBER_PUBLICKEYBYTES, cudaMemcpyHostToDevice,
                    context.stream);
    cudaMemcpyAsync(d_coins, coins, KYBER_SYMBYTES, cudaMemcpyHostToDevice,
                    context.stream);
    indcpa_enc(1, &context.set, d_ct, d_message, d_pk, d_coins, context.stream);
    cudaMemcpyAsync(ct, d_ct, KYBER_CIPHERTEXTBYTES, cudaMemcpyDeviceToHost,
                    context.stream);
    const cudaError_t result = cudaStreamSynchronize(context.stream);
    cudaFree(d_ct); cudaFree(d_message); cudaFree(d_pk); cudaFree(d_coins);
    return result == cudaSuccess ? 0 : -1;
}

int cpa_decrypt(uint8_t *message, const uint8_t *ct, const uint8_t *sk) {
    KyberGpuContext context;
    uint8_t *d_message = nullptr, *d_ct = nullptr, *d_sk = nullptr;
    if (!context.valid() || !device_alloc(&d_message, KYBER_SYMBYTES) ||
        !device_alloc(&d_ct, KYBER_CIPHERTEXTBYTES) ||
        !device_alloc(&d_sk, KYBER_INDCPA_SECRETKEYBYTES)) return -1;
    cudaMemcpyAsync(d_ct, ct, KYBER_CIPHERTEXTBYTES, cudaMemcpyHostToDevice,
                    context.stream);
    cudaMemcpyAsync(d_sk, sk, KYBER_INDCPA_SECRETKEYBYTES,
                    cudaMemcpyHostToDevice, context.stream);
    indcpa_dec(1, &context.set, d_message, d_ct, d_sk, context.stream);
    cudaMemcpyAsync(message, d_message, KYBER_SYMBYTES, cudaMemcpyDeviceToHost,
                    context.stream);
    const cudaError_t result = cudaStreamSynchronize(context.stream);
    cudaFree(d_message); cudaFree(d_ct); cudaFree(d_sk);
    return result == cudaSuccess ? 0 : -1;
}

} // namespace

extern "C" int pqcuda_kyber1024_keypair(uint8_t *pk, uint8_t *sk) {
    if (!pk || !sk || cpa_keypair(pk, sk) != 0) return -1;
    std::memcpy(sk + KYBER_INDCPA_SECRETKEYBYTES, pk, KYBER_PUBLICKEYBYTES);
    sha3_256(sk + KYBER_SECRETKEYBYTES - 64, pk, KYBER_PUBLICKEYBYTES);
    return random_bytes(sk + KYBER_SECRETKEYBYTES - 32, 32) ? 0 : -1;
}

extern "C" int pqcuda_kyber1024_encapsulate(uint8_t *ct, uint8_t *ss,
                                              const uint8_t *pk) {
    if (!ct || !ss || !pk) return -1;
    std::array<uint8_t, 64> buffer{}, kr{};
    if (!random_bytes(buffer.data(), 32)) return -1;
    sha3_256(buffer.data(), buffer.data(), 32);
    sha3_256(buffer.data() + 32, pk, KYBER_PUBLICKEYBYTES);
    sha3_512(kr.data(), buffer.data(), buffer.size());
    if (cpa_encrypt(ct, buffer.data(), pk, kr.data() + 32) != 0) return -1;
    sha3_256(kr.data() + 32, ct, KYBER_CIPHERTEXTBYTES);
    shake256(ss, KYBER_SSBYTES, kr.data(), kr.size());
    return 0;
}

extern "C" int pqcuda_kyber1024_decapsulate(uint8_t *ss, const uint8_t *ct,
                                              const uint8_t *sk) {
    if (!ss || !ct || !sk) return -1;
    std::array<uint8_t, 64> buffer{}, kr{};
    std::array<uint8_t, KYBER_CIPHERTEXTBYTES> comparison{};
    const uint8_t *pk = sk + KYBER_INDCPA_SECRETKEYBYTES;
    if (cpa_decrypt(buffer.data(), ct, sk) != 0) return -1;
    std::memcpy(buffer.data() + 32, sk + KYBER_SECRETKEYBYTES - 64, 32);
    sha3_512(kr.data(), buffer.data(), buffer.size());
    if (cpa_encrypt(comparison.data(), buffer.data(), pk, kr.data() + 32) != 0)
        return -1;
    uint8_t different = 0;
    for (size_t i = 0; i < comparison.size(); ++i)
        different |= comparison[i] ^ ct[i];
    different = static_cast<uint8_t>((static_cast<uint16_t>(different) + 255) >> 8);
    sha3_256(kr.data() + 32, ct, KYBER_CIPHERTEXTBYTES);
    conditional_move(kr.data(), sk + KYBER_SECRETKEYBYTES - 32, 32, different);
    shake256(ss, KYBER_SSBYTES, kr.data(), kr.size());
    return 0;
}
