#pragma once

  #include <stddef.h>
  #include <stdint.h>

  /* Define PQCUDA_STATIC when linking the static archive directly.
   * CMake's PQCUDA::pqcuda_static target supplies this automatically. */
  #if defined(_WIN32) && !defined(PQCUDA_STATIC)
  #  ifdef PQCUDA_BUILD_SHARED
  #    define PQCUDA_API __declspec(dllexport)
  #  else
  #    define PQCUDA_API __declspec(dllimport)
  #  endif
  #else
  #  define PQCUDA_API
  #endif

  #ifdef __cplusplus
  extern "C" {
  #endif

  typedef enum pqcuda_dilithium_mode {
      PQCUDA_DILITHIUM_MODE_2 = 2,
      PQCUDA_DILITHIUM_MODE_3 = 3,
      PQCUDA_DILITHIUM_MODE_5 = 5
  } pqcuda_dilithium_mode;

  #define PQCUDA_DILITHIUM2_PUBLIC_KEY_BYTES 1312u
  #define PQCUDA_DILITHIUM2_SECRET_KEY_BYTES 2528u
  #define PQCUDA_DILITHIUM2_SIGNATURE_BYTES 2420u
  #define PQCUDA_DILITHIUM3_PUBLIC_KEY_BYTES 1952u
  #define PQCUDA_DILITHIUM3_SECRET_KEY_BYTES 4000u
  #define PQCUDA_DILITHIUM3_SIGNATURE_BYTES 3293u
  #define PQCUDA_DILITHIUM5_PUBLIC_KEY_BYTES 2592u
  #define PQCUDA_DILITHIUM5_SECRET_KEY_BYTES 4864u
  #define PQCUDA_DILITHIUM5_SIGNATURE_BYTES 4595u

  #define PQCUDA_KYBER1024_PUBLIC_KEY_BYTES 1568u
  #define PQCUDA_KYBER1024_SECRET_KEY_BYTES 3168u
  #define PQCUDA_KYBER1024_CIPHERTEXT_BYTES 1568u
  #define PQCUDA_KYBER1024_SHARED_SECRET_BYTES 32u

  PQCUDA_API int pqcuda_kyber1024_keypair(uint8_t *pk, uint8_t *sk);

  PQCUDA_API int pqcuda_kyber1024_set_launch_config(
      size_t block_count,
      size_t threads_per_block
  );

  PQCUDA_API size_t pqcuda_kyber1024_max_batch_size(void);
  PQCUDA_API int pqcuda_kyber1024_tune_launch_profile(size_t batch_size);
  PQCUDA_API void pqcuda_kyber1024_print_tuned_kernel_details(void);
  /* Write all measurements from the last tuning run (overwrites path). */
  PQCUDA_API int pqcuda_kyber1024_export_tuning_csv(const char *path);
  /* Restore kernel blocks in tuned_kernel_name order; grid follows batch size. */
  PQCUDA_API int pqcuda_kyber1024_apply_launch_profile(const size_t *blocks, size_t count);
  PQCUDA_API size_t pqcuda_kyber1024_tuned_kernel_count(void);
  PQCUDA_API const char *pqcuda_kyber1024_tuned_kernel_name(size_t kernel_index);
  PQCUDA_API size_t pqcuda_kyber1024_tuned_kernel_threads(size_t kernel_index);

  PQCUDA_API int pqcuda_kyber1024_keypair_batch(
      uint8_t *public_keys,
      uint8_t *secret_keys,
      size_t batch_size
  );

  PQCUDA_API int pqcuda_kyber1024_encapsulate(
      uint8_t *ciphertext,
      uint8_t *shared_secret,
      const uint8_t *public_key
  );

  PQCUDA_API int pqcuda_kyber1024_encapsulate_batch(
      uint8_t *ciphertexts,
      uint8_t *shared_secrets,
      const uint8_t *public_keys,
      size_t batch_size
  );

  PQCUDA_API int pqcuda_kyber1024_decapsulate(
      uint8_t *shared_secret,
      const uint8_t *ciphertext,
      const uint8_t *secret_key
  );

  PQCUDA_API int pqcuda_kyber1024_decapsulate_batch(
      uint8_t *shared_secrets,
      const uint8_t *ciphertexts,
      const uint8_t *secret_keys,
      size_t batch_size
  );

  PQCUDA_API int pqcuda_dilithium_sign_verify(
      const uint8_t *message,
      size_t message_length
  );

  PQCUDA_API int pqcuda_dilithium_sign_verify_mode(
      pqcuda_dilithium_mode mode,
      const uint8_t *message,
      size_t message_length
  );

  /* Snapshot/restore sign variants in tuned_stage_name order. Exact stage count required. */
  PQCUDA_API int pqcuda_dilithium_get_launch_profile(pqcuda_dilithium_mode mode, size_t *variants, size_t count);
  PQCUDA_API int pqcuda_dilithium_apply_launch_profile(pqcuda_dilithium_mode mode, const size_t *variants, size_t count);
  PQCUDA_API size_t pqcuda_dilithium_max_batch_size(void);
  PQCUDA_API int pqcuda_dilithium_tune_sign_kernels(
      pqcuda_dilithium_mode mode,
      size_t batch_size
  );
  PQCUDA_API size_t pqcuda_dilithium_tuned_stage_count(pqcuda_dilithium_mode mode);
  typedef struct pqcuda_launch_config {
      const char *kernel_name;
      size_t grid_x;
      size_t block_x;
      size_t block_y;
  } pqcuda_launch_config;

  /* Snapshot the current sign-stage variant and its launch shape. Repeated
   * rejection stages use the internal execution capacity, not batch_size. */
  PQCUDA_API int pqcuda_dilithium_tuned_launch_config(
      pqcuda_dilithium_mode mode, size_t stage_index, size_t batch_size,
      pqcuda_launch_config *config);
  PQCUDA_API const char *pqcuda_dilithium_tuned_stage_name(
      pqcuda_dilithium_mode mode,
      size_t stage_index
  );
  PQCUDA_API const char *pqcuda_dilithium_tuned_variant_name(
      pqcuda_dilithium_mode mode,
      size_t stage_index
  );

  PQCUDA_API int pqcuda_dilithium_keypair(
      pqcuda_dilithium_mode mode,
      uint8_t *public_key,
      size_t public_key_size,
      uint8_t *secret_key,
      size_t secret_key_size
  );

  PQCUDA_API int pqcuda_dilithium_keypair_batch(
      pqcuda_dilithium_mode mode,
      uint8_t *public_keys,
      size_t public_keys_size,
      uint8_t *secret_keys,
      size_t secret_keys_size,
      size_t batch_size
  );

  PQCUDA_API int pqcuda_dilithium_sign(
      pqcuda_dilithium_mode mode,
      uint8_t *signature,
      size_t signature_capacity,
      size_t *signature_length,
      const uint8_t *message,
      size_t message_length,
      const uint8_t *secret_key,
      size_t secret_key_size
  );

  PQCUDA_API int pqcuda_dilithium_sign_batch(
      pqcuda_dilithium_mode mode,
      uint8_t *signatures,
      size_t signatures_size,
      size_t *signature_length,
      const uint8_t *messages,
      size_t message_length,
      const uint8_t *secret_keys,
      size_t secret_keys_size,
      size_t batch_size
  );

  PQCUDA_API int pqcuda_dilithium_verify(
      pqcuda_dilithium_mode mode,
      const uint8_t *signature,
      size_t signature_length,
      const uint8_t *message,
      size_t message_length,
      const uint8_t *public_key,
      size_t public_key_size
  );

  PQCUDA_API int pqcuda_dilithium_verify_batch(
      pqcuda_dilithium_mode mode,
      const uint8_t *signatures,
      size_t signature_length,
      const uint8_t *messages,
      size_t message_length,
      const uint8_t *public_keys,
      size_t public_keys_size,
      size_t batch_size
  );

  PQCUDA_API size_t pqcuda_dilithium_public_key_bytes(pqcuda_dilithium_mode mode);
  PQCUDA_API size_t pqcuda_dilithium_secret_key_bytes(pqcuda_dilithium_mode mode);
  PQCUDA_API size_t pqcuda_dilithium_signature_bytes(pqcuda_dilithium_mode mode);

  #ifdef __cplusplus
  }
  #endif
