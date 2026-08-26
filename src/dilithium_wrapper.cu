#include <cstddef>
  #include <cstdint>
  #include <vector>

  #include <cuda_runtime.h>

  #include "pqcuda.h"
  #include "api.cuh"

  #ifndef PQC_DILITHIUM_ENTRY
  #define PQC_DILITHIUM_ENTRY pqcuda_dilithium2_sign_verify
  #endif

  namespace {

  constexpr size_t BATCH_SIZE = 1;
  constexpr size_t EXEC_THRESHOLD = 2048;

  bool cuda_ok(cudaError_t error)
  {
      return error == cudaSuccess;
  }

  } // namespace

  extern "C" int PQC_DILITHIUM_ENTRY(
      const uint8_t *message,
      size_t message_length)
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
      const size_t keypair_bytes =
          ALIGN_TO_256_BYTES(CRYPTO_PUBLICKEYBYTES) +
          ALIGN_TO_256_BYTES(CRYPTO_SECRETKEYBYTES) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t);

      if (!cuda_ok(cudaMallocPitch(
              &keypair_pool,
              &keypair_pitch,
              keypair_bytes,
              BATCH_SIZE))) {
          break;
      }

      if (crypto_sign_keypair(
              public_key.data(),
              secret_key.data(),
              keypair_pool,
              keypair_pitch,
              BATCH_SIZE) != 0) {
          break;
      }

      if (!cuda_ok(cudaDeviceSynchronize())) {
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
          ALIGN_TO_256_BYTES(
              SEEDBYTES + CRHBYTES +
              DILITHIUM_K * POLYW1_PACKEDBYTES) +
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

      if (!cuda_ok(cudaMallocPitch(
              &sign_pool,
              &sign_pitch,
              sign_byte_area + sign_integer_area,
              BATCH_SIZE))) {
          break;
      }

      /*
       * Temporary signing memory
       */
      lut.sign_lut.resize(BATCH_SIZE);

      if (!cuda_ok(cudaMallocHost(
              &lut.h_exec_lut,
              sizeof(uint32_t) * EXEC_THRESHOLD))) {
          break;
      }

      if (!cuda_ok(cudaMallocHost(
              &lut.h_done_lut,
              sizeof(uint8_t) * EXEC_THRESHOLD))) {
          break;
      }

      if (!cuda_ok(cudaMallocHost(
              &lut.h_copy_lut,
              sizeof(copy_lut_element) * EXEC_THRESHOLD))) {
          break;
      }

      if (!cuda_ok(cudaMalloc(
              &lut.d_exec_lut,
              sizeof(uint32_t) * EXEC_THRESHOLD))) {
          break;
      }

      if (!cuda_ok(cudaMalloc(
              &lut.d_done_lut,
              sizeof(uint8_t) * EXEC_THRESHOLD))) {
          break;
      }

      if (!cuda_ok(cudaMalloc(
              &lut.d_copy_lut,
              sizeof(copy_lut_element) * EXEC_THRESHOLD))) {
          break;
      }

      const size_t temp_byte_area =
          ALIGN_TO_256_BYTES(CRYPTO_BYTES) +
          ALIGN_TO_256_BYTES(
              CRHBYTES + DILITHIUM_K * POLYW1_PACKEDBYTES);

      const size_t temp_integer_area =
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_L * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t) +
          DILITHIUM_K * DILITHIUM_N * sizeof(int32_t);

      if (!cuda_ok(cudaMallocPitch(
              &temp_pool,
              &temp_pitch,
              temp_byte_area + temp_integer_area,
              EXEC_THRESHOLD))) {
          break;
      }

      if (crypto_sign_signature(
              signature.data(),
              CRYPTO_BYTES,
              &signature_length,
              message,
              message_length,
              message_length,
              secret_key.data(),
              sign_pool,
              sign_pitch,
              temp_pool,
              temp_pitch,
              lut,
              EXEC_THRESHOLD,
              BATCH_SIZE) != 0) {
          break;
      }

      if (!cuda_ok(cudaDeviceSynchronize())) {
          break;
      }

      /*
       * Verification memory
       */
      const size_t verify_byte_area =
          ALIGN_TO_256_BYTES(CRYPTO_BYTES) +
          ALIGN_TO_256_BYTES(CRYPTO_PUBLICKEYBYTES) +
          ALIGN_TO_256_BYTES(SEEDBYTES + message_length) +
          ALIGN_TO_256_BYTES(
              CRHBYTES + DILITHIUM_K * POLYW1_PACKEDBYTES) +
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

      if (!cuda_ok(cudaMallocPitch(
              &verify_pool,
              &verify_pitch,
              verify_byte_area + verify_integer_area,
              BATCH_SIZE))) {
          break;
      }

      crypto_sign_verify(
          &verify_result,
          signature.data(),
          CRYPTO_BYTES,
          signature_length,
          message,
          message_length,
          message_length,
          public_key.data(),
          verify_pool,
          verify_pitch,
          BATCH_SIZE
      );

      if (!cuda_ok(cudaDeviceSynchronize())) {
          break;
      }

      if (verify_result == 0) {
          result = 0;
      }

      } while (false);
      if (verify_pool != nullptr) {
          cudaFree(verify_pool);
      }

      if (temp_pool != nullptr) {
          cudaFree(temp_pool);
      }

      if (sign_pool != nullptr) {
          cudaFree(sign_pool);
      }

      if (keypair_pool != nullptr) {
          cudaFree(keypair_pool);
      }

      if (lut.d_copy_lut != nullptr) {
          cudaFree(lut.d_copy_lut);
      }

      if (lut.d_done_lut != nullptr) {
          cudaFree(lut.d_done_lut);
      }

      if (lut.d_exec_lut != nullptr) {
          cudaFree(lut.d_exec_lut);
      }

      if (lut.h_copy_lut != nullptr) {
          cudaFreeHost(lut.h_copy_lut);
      }

      if (lut.h_done_lut != nullptr) {
          cudaFreeHost(lut.h_done_lut);
      }

      if (lut.h_exec_lut != nullptr) {
          cudaFreeHost(lut.h_exec_lut);
      }

      return result;
  }
