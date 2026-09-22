#include "pqcuda.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    int devices = 0;
    if (cudaGetDeviceCount(&devices) != cudaSuccess || !devices) return 77;
    constexpr size_t batch = 3;
    std::vector<uint8_t> pk(batch * PQCUDA_KYBER1024_PUBLIC_KEY_BYTES);
    std::vector<uint8_t> sk(batch * PQCUDA_KYBER1024_SECRET_KEY_BYTES);
    std::vector<uint8_t> ct(batch * PQCUDA_KYBER1024_CIPHERTEXT_BYTES);
    std::vector<uint8_t> ss(batch * PQCUDA_KYBER1024_SHARED_SECRET_BYTES), recovered(ss.size());
    CHECK(pqcuda_kyber1024_keypair_batch(pk.data(), sk.data(), batch) == 0);
    CHECK(pqcuda_kyber1024_encapsulate_batch(ct.data(), ss.data(), pk.data(), batch) == 0);
    CHECK(pqcuda_kyber1024_decapsulate_batch(recovered.data(), ct.data(), sk.data(), batch) == 0);
    CHECK(ss == recovered);
    ct[0] ^= 1;
    CHECK(pqcuda_kyber1024_decapsulate_batch(recovered.data(), ct.data(), sk.data(), batch) == 0);
    CHECK(ss != recovered);
    for (auto mode : {PQCUDA_DILITHIUM_MODE_2, PQCUDA_DILITHIUM_MODE_3, PQCUDA_DILITHIUM_MODE_5}) {
        const size_t pk_size = pqcuda_dilithium_public_key_bytes(mode) * batch;
        const size_t sk_size = pqcuda_dilithium_secret_key_bytes(mode) * batch;
        const size_t sig_item = pqcuda_dilithium_signature_bytes(mode);
        pk.resize(pk_size); sk.resize(sk_size);
        std::vector<uint8_t> sig(sig_item * batch), messages(169 * batch, 0x42);
        size_t sig_length = 0;
        CHECK(pqcuda_dilithium_keypair_batch(mode, pk.data(), pk.size(), sk.data(), sk.size(), batch) == 0);
        CHECK(pqcuda_dilithium_sign_batch(mode, sig.data(), sig.size(), &sig_length,
            messages.data(), 169, sk.data(), sk.size(), batch) == 0);
        CHECK(sig_length == sig_item);
        CHECK(pqcuda_dilithium_verify_batch(mode, sig.data(), sig_length,
            messages.data(), 169, pk.data(), pk.size(), batch) == 0);
        messages[0] ^= 1;
        CHECK(pqcuda_dilithium_verify_batch(mode, sig.data(), sig_length,
            messages.data(), 169, pk.data(), pk.size(), batch) != 0);
    }
    std::puts("PASS: Kyber KEM and Dilithium 2/3/5 sign/verify, including modified inputs");
    return 0;
}
