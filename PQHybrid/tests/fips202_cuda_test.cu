#include "fips202_cuda/thread/fips202.cuh"
#include "fips202_cuda/warp/fips202.cuh"
#include "fips202_vectors.h"
#include <cstdio>
#include <vector>
constexpr int batch = 5, pitch = 512;
__global__ void thread_hash(uint8_t *out, uint8_t *in, size_t length, int kind) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= batch) return;
    out += i * pitch;
    in += i * pitch;
    if (kind == 0) sha3_256(out, in, length);
    else if (kind == 1) sha3_512(out, in, length);
    else if (kind == 2) {
        uint64_t state[25];
        shake128_absorb(state, in, static_cast<unsigned>(length));
        shake128_squeezeblocks(out, 3, state);
    } else shake256(out, 337, in, length);
}
#define CUDA_CHECK(x) do { cudaError_t e = (x); if (e != cudaSuccess) { \
    std::fprintf(stderr, "CUDA line %d: %s\n", __LINE__, cudaGetErrorString(e)); return 1; } } while (0)
int main() {
    int devices = 0;
    if (cudaGetDeviceCount(&devices) != cudaSuccess || !devices) {
        std::puts("SKIP: CUDA device unavailable");
        return 77;
    }
    uint8_t *in, *out;
    std::vector<uint8_t> input(batch * pitch), output(batch * pitch);
    for (int b = 0; b < batch; ++b)
        for (int i = 0; i < 337; ++i) input[b * pitch + i] = i % 251;
    CUDA_CHECK(cudaMalloc(&in, input.size()));
    CUDA_CHECK(cudaMalloc(&out, output.size()));
    CUDA_CHECK(cudaMemcpy(in, input.data(), input.size(), cudaMemcpyHostToDevice));
    for (const auto &v : fips202_vectors) {
        for (int backend = 0; backend < 2; ++backend) {
            for (int kind = backend ? 2 : 0; kind < 4; ++kind) {
                CUDA_CHECK(cudaMemset(out, 0, output.size()));
                if (!backend) thread_hash<<<1, 32>>>(out, in, v.length, kind);
                else if (kind == 2)
                    shake_kernel<SHAKE128_RATE><<<2, dim3(32, 4), SHAKE128_RATE * 4>>>(
                        out, pitch, 337, in, pitch, v.length, batch);
                else
                    shake_kernel<SHAKE256_RATE><<<2, dim3(32, 4), SHAKE256_RATE * 4>>>(
                        out, pitch, 337, in, pitch, v.length, batch);
                CUDA_CHECK(cudaGetLastError());
                CUDA_CHECK(cudaMemcpy(output.data(), out, output.size(), cudaMemcpyDeviceToHost));
                const char *expected[] = {v.sha256, v.sha512, v.shake128, v.shake256};
                size_t length = kind == 0 ? 32 : kind == 1 ? 64 : 337;
                for (int b = 0; b < batch; ++b)
                    if (!matches_hex(output.data() + b * pitch, expected[kind], length)) {
                        std::fprintf(stderr, "FAIL backend=%d kind=%d input=%zu batch=%d\n", backend, kind, v.length, b);
                        return 1;
                    }
            }
        }
    }
    CUDA_CHECK(cudaFree(in));
    CUDA_CHECK(cudaFree(out));
    std::puts("PASS: CUDA thread/warp SHA3/SHAKE vectors, partial batch and multi-block output");
    return 0;
}
