#include "fips202_cpu/fips202.h"
#include "fips202_vectors.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d, input=%zu\n", __LINE__, n); return 1; } } while (0)
int main(void) {
    uint8_t input[337], out[337], incremental[337];
    for (size_t i = 0; i < sizeof(input); ++i) input[i] = (uint8_t)(i % 251);
    for (size_t v = 0; v < sizeof(fips202_vectors)/sizeof(fips202_vectors[0]); ++v) {
        size_t n = fips202_vectors[v].length;
        keccak_state state;
        sha3_256(out, input, n);
        CHECK(matches_hex(out, fips202_vectors[v].sha256, 32));
        sha3_512(out, input, n);
        CHECK(matches_hex(out, fips202_vectors[v].sha512, 64));
        shake128(out, sizeof(out), input, n);
        CHECK(matches_hex(out, fips202_vectors[v].shake128, sizeof(out)));
        shake128_init(&state);
        shake128_absorb(&state, input, n / 2);
        shake128_absorb(&state, input + n / 2, n - n / 2);
        shake128_finalize(&state);
        shake128_squeeze(incremental, 1, &state);
        shake128_squeeze(incremental + 1, sizeof(incremental) - 1, &state);
        CHECK(memcmp(out, incremental, sizeof(out)) == 0);
        shake256(out, sizeof(out), input, n);
        CHECK(matches_hex(out, fips202_vectors[v].shake256, sizeof(out)));
        shake256_init(&state);
        shake256_absorb(&state, input, n / 2);
        shake256_absorb(&state, input + n / 2, n - n / 2);
        shake256_finalize(&state);
        shake256_squeeze(incremental, 1, &state);
        shake256_squeeze(incremental + 1, sizeof(incremental) - 1, &state);
        CHECK(memcmp(out, incremental, sizeof(out)) == 0);
    }
    puts("PASS: CPU SHA3/SHAKE vectors and incremental absorb/squeeze");
    return 0;
}
