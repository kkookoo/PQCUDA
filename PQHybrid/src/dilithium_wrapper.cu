#include "benchmark_policy.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
  #include <cstdint>
  #include <vector>

  #include <cuda_runtime.h>

  #include "pqcuda.h"
  #include "api.cuh"

  #define PQC_JOIN_MODE_IMPL(prefix, mode, suffix) prefix##mode##suffix
  #define PQC_JOIN_MODE(prefix, mode, suffix) \
      PQC_JOIN_MODE_IMPL(prefix, mode, suffix)
  #ifndef PQC_DILITHIUM_ENTRY
  #define PQC_DILITHIUM_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _sign_verify)
  #endif
  #ifndef PQC_DILITHIUM_KEYPAIR_ENTRY
  #define PQC_DILITHIUM_KEYPAIR_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _keypair)
  #endif
  #ifndef PQC_DILITHIUM_SIGN_ENTRY
  #define PQC_DILITHIUM_SIGN_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _sign)
  #endif
  #ifndef PQC_DILITHIUM_VERIFY_ENTRY
  #define PQC_DILITHIUM_VERIFY_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _verify)
  #endif
  #ifndef PQC_DILITHIUM_KEYPAIR_BATCH_ENTRY
  #define PQC_DILITHIUM_KEYPAIR_BATCH_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _keypair_batch)
  #endif
  #ifndef PQC_DILITHIUM_SIGN_BATCH_ENTRY
  #define PQC_DILITHIUM_SIGN_BATCH_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _sign_batch)
  #endif
  #ifndef PQC_DILITHIUM_VERIFY_BATCH_ENTRY
  #define PQC_DILITHIUM_VERIFY_BATCH_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _verify_batch)
  #endif
  #ifndef PQC_DILITHIUM_TUNE_ENTRY
  #define PQC_DILITHIUM_TUNE_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _tune_sign_kernels)
  #endif
  #ifndef PQC_DILITHIUM_TUNED_STAGE_COUNT_ENTRY
  #define PQC_DILITHIUM_TUNED_STAGE_COUNT_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _tuned_stage_count)
  #endif
  #ifndef PQC_DILITHIUM_TUNED_STAGE_NAME_ENTRY
  #define PQC_DILITHIUM_TUNED_STAGE_NAME_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _tuned_stage_name)
  #endif
  #ifndef PQC_DILITHIUM_TUNED_VARIANT_NAME_ENTRY
  #define PQC_DILITHIUM_TUNED_VARIANT_NAME_ENTRY \
      PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _tuned_variant_name)
  #endif

  namespace {

  constexpr size_t BATCH_SIZE = 1;
  constexpr size_t EXEC_THRESHOLD = 2048;

  bool cuda_ok(cudaError_t error)
  {
      return error == cudaSuccess;
  }

  } // namespace

  extern "C" int PQC_DILITHIUM_ENTRY(const uint8_t *message, size_t message_length)
  {
      int result = -1;
      int verify_result = -1;
      size_t signature_length = 0;

      std::vector<uint8_t> public_key(CRYPTO_PUBLICKEYBYTES);
      std::vector<uint8_t> secret_key(CRYPTO_SECRETKEYBYTES);
      std::vector<uint8_t> signature(CRYPTO_BYTES);

      uint8_t *keypair_pool = nullptr;
      uint8_t *sign_pool = nullptr;
      uint8_t *temp_pool = nullptr;
      uint8_t *verify_pool = nullptr;

      size_t keypair_pitch = 0;
      size_t sign_pitch = 0;
      size_t temp_pitch = 0;
      size_t verify_pitch = 0;

      task_lut lut{};

      do {

      /*
       * Key generation memory
       */
      const size_t keypair_bytes = ALIGN_TO_256_BYTES(CRYPTO_PUBLICKEYBYTES) + ALIGN_TO_256_BYTES(CRYPTO_SECRETKEYBYTES) + DILITHIUM_K * DILITHIUM_N * sizeof(int32_t);

      if (!cuda_ok(cudaMallocPitch(&keypair_pool, &keypair_pitch, keypair_bytes, BATCH_SIZE))) 
      {
          break;
      }

      if (crypto_sign_keypair(public_key.data(), secret_key.data(), keypair_pool, keypair_pitch, BATCH_SIZE) != 0) 
      {
          break;
      }

      if (!cuda_ok(cudaDeviceSynchronize())) 
      {
          break;
      }

      /*
       * Signing memory
       */
      const size_t sign_byte_area =
          ALIGN_TO_256_BYTES(CRYPTO_BYTES) +
          ALIGN_TO_256_BYTES(CRYPTO_SECRETKEYBYTES) +
          ALIGN_TO_256_BYTES(SEEDBYTES) +
          ALIGN_TO_256_BYTES(SEEDBYTES + message_length) +
          ALIGN_TO_256_BYTES(SEEDBYTES + CRHBYTES + DILITHIUM_K * POLYW1_PACKEDBYTES) +
          ALIGN_TO_256_BYTES(CRHBYTES);

      const size_t sign_integer_area =
          DILITHIUM_K * DILITHIUM_L * DILITHIUM_N * sizeof(int32_t)
          +
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_N * sizeof(int32_t);

      if (!cuda_ok(cudaMallocPitch(&sign_pool, &sign_pitch, sign_byte_area + sign_integer_area, BATCH_SIZE))) 
      {
          break;
      }

      /*
       * Temporary signing memory
       */
      lut.sign_lut.resize(BATCH_SIZE);

      if (!cuda_ok(cudaMallocHost(&lut.h_exec_lut, sizeof(uint32_t) * EXEC_THRESHOLD))) 
      {
          break;
      }

      if (!cuda_ok(cudaMallocHost(&lut.h_done_lut, sizeof(uint8_t) * EXEC_THRESHOLD))) 
      {
          break;
      }

      if (!cuda_ok(cudaMallocHost(&lut.h_copy_lut, sizeof(copy_lut_element) * EXEC_THRESHOLD))) 
      {
          break;
      }

      if (!cuda_ok(cudaMalloc(&lut.d_exec_lut, sizeof(uint32_t) * EXEC_THRESHOLD))) 
      {
          break;
      }

      if (!cuda_ok(cudaMalloc(&lut.d_done_lut, sizeof(uint8_t) * EXEC_THRESHOLD))) 
      {
          break;
      }

      if (!cuda_ok(cudaMalloc(&lut.d_copy_lut, sizeof(copy_lut_element) * EXEC_THRESHOLD))) 
      {
          break;
      }

      const size_t temp_byte_area = ALIGN_TO_256_BYTES(CRYPTO_BYTES) + ALIGN_TO_256_BYTES(CRHBYTES + DILITHIUM_K * POLYW1_PACKEDBYTES);

      const size_t temp_integer_area =
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t);

      if (!cuda_ok(cudaMallocPitch(&temp_pool, &temp_pitch, temp_byte_area + temp_integer_area, EXEC_THRESHOLD))) 
      {
          break;
      }

      if (crypto_sign_signature(signature.data(), CRYPTO_BYTES, &signature_length, message, message_length, message_length, secret_key.data(), sign_pool, sign_pitch, temp_pool, temp_pitch, lut, EXEC_THRESHOLD, BATCH_SIZE) != 0) 
      {
          break;
      }

      if (!cuda_ok(cudaDeviceSynchronize())) 
      {
          break;
      }

      /*
       * Verification memory
       */
      const size_t verify_byte_area =
          ALIGN_TO_256_BYTES(CRYPTO_BYTES) +
          ALIGN_TO_256_BYTES(CRYPTO_PUBLICKEYBYTES) +
          ALIGN_TO_256_BYTES(SEEDBYTES + message_length) +
          ALIGN_TO_256_BYTES(CRHBYTES + DILITHIUM_K * POLYW1_PACKEDBYTES) +
          ALIGN_TO_256_BYTES(SEEDBYTES);

      const size_t verify_integer_area =
          DILITHIUM_K * DILITHIUM_L * DILITHIUM_N * sizeof(int32_t)
          +
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_N * sizeof(int32_t) +
          sizeof(int);

      if (!cuda_ok(cudaMallocPitch(&verify_pool, &verify_pitch, verify_byte_area + verify_integer_area, BATCH_SIZE))) 
      {
          break;
      }

      crypto_sign_verify(&verify_result, signature.data(), CRYPTO_BYTES, signature_length, message, message_length, message_length, public_key.data(), verify_pool, verify_pitch, BATCH_SIZE);

      if (!cuda_ok(cudaDeviceSynchronize())) 
      {
          break;
      }

      if (verify_result == 0) 
      {
          result = 0;
      }

      } while (false);
      if (verify_pool != nullptr) 
      {
          cudaFree(verify_pool);
      }

      if (temp_pool != nullptr) 
      {
          cudaFree(temp_pool);
      }

      if (sign_pool != nullptr) 
      {
          cudaFree(sign_pool);
      }

      if (keypair_pool != nullptr) 
      {
          cudaFree(keypair_pool);
      }

      if (lut.d_copy_lut != nullptr) 
      {
          cudaFree(lut.d_copy_lut);
      }

      if (lut.d_done_lut != nullptr) 
      {
          cudaFree(lut.d_done_lut);
      }

      if (lut.d_exec_lut != nullptr) 
      {
          cudaFree(lut.d_exec_lut);
      }

      if (lut.h_copy_lut != nullptr) 
      
      {
          cudaFreeHost(lut.h_copy_lut);
      }

      if (lut.h_done_lut != nullptr) 
      {
          cudaFreeHost(lut.h_done_lut);
      }

      if (lut.h_exec_lut != nullptr) 
      {
          cudaFreeHost(lut.h_exec_lut);
      }

      return result;
  }

  extern "C" int PQC_DILITHIUM_KEYPAIR_BATCH_ENTRY(uint8_t *public_key, uint8_t *secret_key, size_t batch_size) 
  {
      if (public_key == nullptr || secret_key == nullptr || batch_size == 0) 
      {
          return -1;
      }

      uint8_t *pool = nullptr;
      size_t pitch = 0;
      const size_t bytes = ALIGN_TO_256_BYTES(CRYPTO_PUBLICKEYBYTES) + ALIGN_TO_256_BYTES(CRYPTO_SECRETKEYBYTES) + DILITHIUM_K * DILITHIUM_N * sizeof(int32_t);

      if (!cuda_ok(cudaMallocPitch(&pool, &pitch, bytes, batch_size))) 
      {
          return -1;
      }
      const int call_result = crypto_sign_keypair(public_key, secret_key, pool, pitch, batch_size);
      const cudaError_t sync_result = cudaDeviceSynchronize();
      cudaFree(pool);
      return call_result == 0 && cuda_ok(sync_result) ? 0 : -1;
  }

  extern "C" int PQC_DILITHIUM_SIGN_BATCH_ENTRY(
      uint8_t *signature,
      size_t *signature_length,
      const uint8_t *message,
      size_t message_length,
      const uint8_t *secret_key,
      size_t batch_size)
  {
      if (signature == nullptr || signature_length == nullptr ||
          (message == nullptr && message_length != 0) || secret_key == nullptr ||
          batch_size == 0) {
          return -1;
      }

      int result = -1;
      uint8_t *sign_pool = nullptr;
      uint8_t *temp_pool = nullptr;
      size_t sign_pitch = 0;
      size_t temp_pitch = 0;
      task_lut lut{};

      do {
          const size_t sign_byte_area =
              ALIGN_TO_256_BYTES(CRYPTO_BYTES) +
              ALIGN_TO_256_BYTES(CRYPTO_SECRETKEYBYTES) +
              ALIGN_TO_256_BYTES(SEEDBYTES) +
              ALIGN_TO_256_BYTES(SEEDBYTES + message_length) +
              ALIGN_TO_256_BYTES(SEEDBYTES + CRHBYTES +
                                 DILITHIUM_K * POLYW1_PACKEDBYTES) +
              ALIGN_TO_256_BYTES(CRHBYTES);
          const size_t sign_integer_area =
              DILITHIUM_K * DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
              3 * DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
              4 * DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
              DILITHIUM_N * sizeof(int32_t);
          if (!cuda_ok(cudaMallocPitch(&sign_pool, &sign_pitch,
                                       sign_byte_area + sign_integer_area,
                                       batch_size))) break;

          lut.sign_lut.resize(batch_size);
          if (!cuda_ok(cudaMallocHost(&lut.h_exec_lut,
                  sizeof(uint32_t) * EXEC_THRESHOLD))) break;
          if (!cuda_ok(cudaMallocHost(&lut.h_done_lut,
                  sizeof(uint8_t) * EXEC_THRESHOLD))) break;
          if (!cuda_ok(cudaMallocHost(&lut.h_copy_lut,
                  sizeof(copy_lut_element) * EXEC_THRESHOLD))) break;
          if (!cuda_ok(cudaMalloc(&lut.d_exec_lut,
                  sizeof(uint32_t) * EXEC_THRESHOLD))) break;
          if (!cuda_ok(cudaMalloc(&lut.d_done_lut,
                  sizeof(uint8_t) * EXEC_THRESHOLD))) break;
          if (!cuda_ok(cudaMalloc(&lut.d_copy_lut,
                  sizeof(copy_lut_element) * EXEC_THRESHOLD))) break;

          const size_t temp_byte_area =
              ALIGN_TO_256_BYTES(CRYPTO_BYTES) +
              ALIGN_TO_256_BYTES(CRHBYTES +
                                 DILITHIUM_K * POLYW1_PACKEDBYTES);
          const size_t temp_integer_area =
              2 * DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
              3 * DILITHIUM_K * DILITHIUM_N * sizeof(int32_t);
          if (!cuda_ok(cudaMallocPitch(&temp_pool, &temp_pitch,
                                       temp_byte_area + temp_integer_area,
                                       EXEC_THRESHOLD))) break;

          if (crypto_sign_signature(
                  signature, CRYPTO_BYTES, signature_length,
                  message, message_length, message_length, secret_key,
                  sign_pool, sign_pitch, temp_pool, temp_pitch, lut,
                  EXEC_THRESHOLD, batch_size) != 0) break;
          if (!cuda_ok(cudaDeviceSynchronize())) break;
          result = 0;
      } while (false);

      if (temp_pool) cudaFree(temp_pool);
      if (sign_pool) cudaFree(sign_pool);
      if (lut.d_copy_lut) cudaFree(lut.d_copy_lut);
      if (lut.d_done_lut) cudaFree(lut.d_done_lut);
      if (lut.d_exec_lut) cudaFree(lut.d_exec_lut);
      if (lut.h_copy_lut) cudaFreeHost(lut.h_copy_lut);
      if (lut.h_done_lut) cudaFreeHost(lut.h_done_lut);
      if (lut.h_exec_lut) cudaFreeHost(lut.h_exec_lut);
      return result;
  }

  extern "C" int PQC_DILITHIUM_VERIFY_BATCH_ENTRY(
      const uint8_t *signature,
      size_t signature_length,
      const uint8_t *message,
      size_t message_length,
      const uint8_t *public_key,
      size_t batch_size)
  {
      if (signature == nullptr || (message == nullptr && message_length != 0) ||
          public_key == nullptr || signature_length != CRYPTO_BYTES) {
          return -1;
      }

      uint8_t *pool = nullptr;
      size_t pitch = 0;
      if (batch_size == 0) return -1;
      std::vector<int> verify_results(batch_size, -1);
      const size_t byte_area =
          ALIGN_TO_256_BYTES(CRYPTO_BYTES) +
          ALIGN_TO_256_BYTES(CRYPTO_PUBLICKEYBYTES) +
          ALIGN_TO_256_BYTES(SEEDBYTES + message_length) +
          ALIGN_TO_256_BYTES(CRHBYTES + DILITHIUM_K * POLYW1_PACKEDBYTES) +
          ALIGN_TO_256_BYTES(SEEDBYTES);
      const size_t integer_area =
          DILITHIUM_K * DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          3 * DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_N * sizeof(int32_t) + sizeof(int);
      if (!cuda_ok(cudaMallocPitch(&pool, &pitch,
                                   byte_area + integer_area, batch_size))) {
          return -1;
      }
      crypto_sign_verify(verify_results.data(), signature, CRYPTO_BYTES,
                         signature_length, message, message_length,
                         message_length, public_key, pool, pitch, batch_size);
      const cudaError_t sync_result = cudaDeviceSynchronize();
      cudaFree(pool);
      if (!cuda_ok(sync_result)) return -1;
      for (int verify_result : verify_results)
          if (verify_result != 0) return -1;
      return 0;
  }

  extern "C" int PQC_DILITHIUM_KEYPAIR_ENTRY(
      uint8_t *public_key, uint8_t *secret_key) {
      return PQC_DILITHIUM_KEYPAIR_BATCH_ENTRY(public_key, secret_key, 1);
  }

  extern "C" int PQC_DILITHIUM_SIGN_ENTRY(
      uint8_t *signature, size_t *signature_length,
      const uint8_t *message, size_t message_length,
      const uint8_t *secret_key) {
      return PQC_DILITHIUM_SIGN_BATCH_ENTRY(
          signature, signature_length, message, message_length, secret_key, 1);
  }

  extern "C" int PQC_DILITHIUM_VERIFY_ENTRY(
      const uint8_t *signature, size_t signature_length,
      const uint8_t *message, size_t message_length,
      const uint8_t *public_key) {
      return PQC_DILITHIUM_VERIFY_BATCH_ENTRY(
          signature, signature_length, message, message_length, public_key, 1);
  }

  extern "C" int PQC_DILITHIUM_TUNE_ENTRY(size_t batch_size) {
      static const uint8_t tuning_message[] = {
          'P', 'Q', 'C', 'U', 'D', 'A'
      };
      if (batch_size == 0 ||
          batch_size > pqcuda_dilithium_max_batch_size()) return -1;

      std::vector<uint8_t> public_key(
          batch_size * CRYPTO_PUBLICKEYBYTES);
      std::vector<uint8_t> secret_key(
          batch_size * CRYPTO_SECRETKEYBYTES);
      std::vector<uint8_t> signature(batch_size * CRYPTO_BYTES);
      std::vector<uint8_t> tuning_messages(
          batch_size * sizeof(tuning_message));
      size_t signature_length = 0;

      for (size_t batch_index = 0; batch_index < batch_size; ++batch_index) {
          for (size_t byte_index = 0;
               byte_index < sizeof(tuning_message); ++byte_index) {
              tuning_messages[batch_index * sizeof(tuning_message) +
                              byte_index] = tuning_message[byte_index];
          }
      }

      if (PQC_DILITHIUM_KEYPAIR_BATCH_ENTRY(
              public_key.data(), secret_key.data(), batch_size) != 0) {
          return -1;
      }

      for (int stage = 0; stage < DILITHIUM_TUNE_STAGE_COUNT; ++stage) {
          float best_ms = FLT_MAX;
          int best_variant = -1;
          const int variant_count = crypto_sign_kernel_variant_count(stage);

          for (int variant = 0; variant < variant_count; ++variant) {
              if (crypto_sign_set_kernel_variant(stage, variant) != 0) {
                  return -1;
              }

              std::array<float, PQCUDA_BENCHMARK_SAMPLE_RUNS> samples{};
              for (int run = 0;
                   run < PQCUDA_BENCHMARK_WARMUP_RUNS + PQCUDA_BENCHMARK_SAMPLE_RUNS; ++run) {
                  const bool measured = run >= PQCUDA_BENCHMARK_WARMUP_RUNS;
                  if (measured && crypto_sign_tuning_begin(stage) != 0) {
                      return -1;
                  }

                  signature_length = 0;
                  const int sign_result = PQC_DILITHIUM_SIGN_BATCH_ENTRY(
                      signature.data(), &signature_length,
                      tuning_messages.data(), sizeof(tuning_message),
                      secret_key.data(), batch_size);
                  // Always end an active measurement, including failed signs.
                  const float elapsed_ms = measured
                      ? crypto_sign_tuning_end() : 0.0f;

                  if (sign_result != 0 ||
                      (measured && (!std::isfinite(elapsed_ms) ||
                                    elapsed_ms < 0.0f)) ||
                      PQC_DILITHIUM_VERIFY_BATCH_ENTRY(
                          signature.data(), signature_length,
                          tuning_messages.data(), sizeof(tuning_message),
                          public_key.data(), batch_size) != 0) {
                      return -1;
                  }

                  if (measured) {
                      samples[run - PQCUDA_BENCHMARK_WARMUP_RUNS] = elapsed_ms;
                  }
              }

              // Discard warmups and use the median to reduce outlier effects.
              std::sort(samples.begin(), samples.end());
              const float median_ms =
                  (samples[(PQCUDA_BENCHMARK_SAMPLE_RUNS - 1) / 2] +
                   samples[PQCUDA_BENCHMARK_SAMPLE_RUNS / 2]) / 2.0f;
              if (median_ms < best_ms) {
                  best_ms = median_ms;
                  best_variant = variant;
              }
          }

          if (best_variant < 0 ||
              crypto_sign_set_kernel_variant(stage, best_variant) != 0) {
              return -1;
          }
      }

      return 0;
  }

  extern "C" int PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _get_launch_profile)(
      size_t *variants, size_t count) {
      if (!variants || count != DILITHIUM_TUNE_STAGE_COUNT) return -1;
      for (size_t i = 0; i < count; ++i)
          variants[i] = crypto_sign_get_kernel_variant(static_cast<int>(i));
      return 0;
  }

  extern "C" int PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _apply_launch_profile)(
      const size_t *variants, size_t count) {
      if (!variants || count != DILITHIUM_TUNE_STAGE_COUNT) return -1;
      for (size_t i = 0; i < count; ++i)
          if (variants[i] >= static_cast<size_t>(crypto_sign_kernel_variant_count(static_cast<int>(i))))
              return -1;
      for (size_t i = 0; i < count; ++i)
          if (crypto_sign_set_kernel_variant(static_cast<int>(i), static_cast<int>(variants[i])) != 0)
              return -1;
      return 0;
  }

  extern "C" size_t PQC_DILITHIUM_TUNED_STAGE_COUNT_ENTRY(void) {
      return DILITHIUM_TUNE_STAGE_COUNT;
  }

  extern "C" int PQC_JOIN_MODE(pqcuda_dilithium, DILITHIUM_MODE, _tuned_launch_config)(
      size_t stage_index, size_t batch_size, pqcuda_launch_config *config) {
      if (!config || stage_index >= DILITHIUM_TUNE_STAGE_COUNT ||
          batch_size == 0 || batch_size > pqcuda_dilithium_max_batch_size()) return -1;
      const int stage = static_cast<int>(stage_index);
      const int variant = crypto_sign_get_kernel_variant(stage);
      config->kernel_name = crypto_sign_kernel_variant_name(stage, variant);
      config->block_x = 32;
      config->block_y = 1;
      size_t items = batch_size;
      switch (stage) {
      case DILITHIUM_TUNE_COMPUTE_Y:
      case DILITHIUM_TUNE_COMPUTE_CP:
          items = EXEC_THRESHOLD;
          if (variant == 1) config->block_y = 4;
          break;
      case DILITHIUM_TUNE_COMPUTE_W:
      case DILITHIUM_TUNE_REJECTION:
          items = EXEC_THRESHOLD;
          if (variant == 1) config->block_x = 128;
          break;
      case DILITHIUM_TUNE_UNPACK_NTT:
          if (variant != 0) config->block_x = 128;
          break;
      case DILITHIUM_TUNE_SHAKE:
      case DILITHIUM_TUNE_MATRIX_EXPAND:
          if (variant == 1) config->block_y = 4;
          break;
      default: return -1;
      }
      config->grid_x = (items + config->block_y - 1) / config->block_y;
      return config->kernel_name ? 0 : -1;
  }

  extern "C" const char *PQC_DILITHIUM_TUNED_STAGE_NAME_ENTRY(
      size_t stage_index) {
      if (stage_index >= DILITHIUM_TUNE_STAGE_COUNT) return nullptr;
      return crypto_sign_tuning_stage_name(static_cast<int>(stage_index));
  }

  extern "C" const char *PQC_DILITHIUM_TUNED_VARIANT_NAME_ENTRY(
      size_t stage_index) {
      if (stage_index >= DILITHIUM_TUNE_STAGE_COUNT) return nullptr;
      const int stage = static_cast<int>(stage_index);
      return crypto_sign_kernel_variant_name(
          stage, crypto_sign_get_kernel_variant(stage));
  }
