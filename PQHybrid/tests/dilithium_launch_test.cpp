#include "pqcuda.h"
#include <cstdio>
#include <cstring>

// Internal selectors are linked statically so all supported variants can be
// checked without running CUDA kernels or exporting test-only setters.
int dilithium2_crypto_sign_set_kernel_variant(int, int);
int dilithium3_crypto_sign_set_kernel_variant(int, int);
int dilithium5_crypto_sign_set_kernel_variant(int, int);

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)

int main() {
    const pqcuda_dilithium_mode modes[] = {
        PQCUDA_DILITHIUM_MODE_2, PQCUDA_DILITHIUM_MODE_3, PQCUDA_DILITHIUM_MODE_5};
    int (*const select[])(int, int) = {
        dilithium2_crypto_sign_set_kernel_variant,
        dilithium3_crypto_sign_set_kernel_variant,
        dilithium5_crypto_sign_set_kernel_variant};
    const int variants[] = {2, 2, 3, 2, 2, 2, 2};
    const size_t workloads[] = {1, 3, 17, 16384, 32768};
    pqcuda_launch_config config{};
    size_t saved[7], restored[7];
    for (size_t m = 0; m < 3; ++m) {
        for (size_t stage = 0; stage < 7; ++stage) {
            for (int variant = 0; variant < variants[stage]; ++variant) {
                CHECK(select[m](static_cast<int>(stage), variant) == 0);
                for (size_t batch : workloads) {
                    CHECK(pqcuda_dilithium_tuned_launch_config(modes[m], stage, batch, &config) == 0);
                    CHECK(std::strcmp(config.kernel_name,
                        pqcuda_dilithium_tuned_variant_name(modes[m], stage)) == 0);
                    const bool four_items = variant == 1 &&
                        (stage == 0 || stage == 1 || stage == 3 || stage == 5);
                    const bool wide = (stage == 2 && variant > 0) ||
                        ((stage == 4 || stage == 6) && variant == 1);
                    CHECK(config.block_x == (wide ? 128u : 32u));
                    CHECK(config.block_y == (four_items ? 4u : 1u));
                    // Retry kernels always launch 2048 attempts, even at batch=1.
                    const size_t items = stage < 3 ? batch : 2048;
                    CHECK(config.grid_x == (items + config.block_y - 1) / config.block_y);
                }
            }
        }
        CHECK(pqcuda_dilithium_get_launch_profile(modes[m], saved, 7) == 0);
        CHECK(select[m](0, 0) == 0);
        CHECK(pqcuda_dilithium_apply_launch_profile(modes[m], saved, 7) == 0);
        CHECK(pqcuda_dilithium_get_launch_profile(modes[m], restored, 7) == 0);
        CHECK(std::memcmp(saved, restored, sizeof(saved)) == 0);
        saved[6] = 999;
        CHECK(pqcuda_dilithium_apply_launch_profile(modes[m], saved, 7) == -1);
        CHECK(pqcuda_dilithium_get_launch_profile(modes[m], saved, 7) == 0);
        CHECK(std::memcmp(saved, restored, sizeof(saved)) == 0);
        CHECK(pqcuda_dilithium_tuned_launch_config(modes[m], 7, 1, &config) == -1);
        CHECK(pqcuda_dilithium_tuned_launch_config(modes[m], 0, 0, &config) == -1);
        CHECK(pqcuda_dilithium_tuned_launch_config(modes[m], 0, 32769, &config) == -1);
        CHECK(pqcuda_dilithium_tuned_launch_config(modes[m], 0, 1, nullptr) == -1);
        CHECK(select[m](0, 0) == 0);
        CHECK(pqcuda_dilithium_tuned_launch_config(modes[m], 0, 3, &config) == 0);
        CHECK(select[m](0, 1) == 0);
        CHECK(config.grid_x == 3 && config.block_y == 1);
        CHECK(std::strcmp(config.kernel_name, "notmp_shake_kernel") == 0);
    }
    CHECK(pqcuda_dilithium_tuned_launch_config(
        static_cast<pqcuda_dilithium_mode>(4), 0, 1, &config) == -1);
    std::puts("PASS: all modes/variants, launch geometry, snapshots and invalid arguments");
    return 0;
}
