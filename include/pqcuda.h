#pragma once

  #include <stddef.h>
  #include <stdint.h>

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

  #define PQCUDA_FRODO976_PUBLIC_KEY_BYTES 15632u
  #define PQCUDA_FRODO976_SECRET_KEY_BYTES 31296u
  #define PQCUDA_FRODO976_CIPHERTEXT_BYTES 15744u
  #define PQCUDA_FRODO976_SHARED_SECRET_BYTES 24u

  int pqcuda_frodo_keypair(uint8_t *pk, uint8_t *sk);

  int pqcuda_frodo_encapsulate(
      uint8_t *ciphertext,
      uint8_t *shared_secret,
      const uint8_t *public_key
  );

  int pqcuda_frodo_decapsulate(
      uint8_t *shared_secret,
      const uint8_t *ciphertext,
      const uint8_t *secret_key
  );

  int pqcuda_dilithium_sign_verify(
      const uint8_t *message,
      size_t message_length
  );

  int pqcuda_dilithium_sign_verify_mode(
      pqcuda_dilithium_mode mode,
      const uint8_t *message,
      size_t message_length
  );

  int pqcuda_dilithium_keypair(
      pqcuda_dilithium_mode mode,
      uint8_t *public_key,
      size_t public_key_size,
      uint8_t *secret_key,
      size_t secret_key_size
  );

  int pqcuda_dilithium_sign(
      pqcuda_dilithium_mode mode,
      uint8_t *signature,
      size_t signature_capacity,
      size_t *signature_length,
      const uint8_t *message,
      size_t message_length,
      const uint8_t *secret_key,
      size_t secret_key_size
  );

  int pqcuda_dilithium_verify(
      pqcuda_dilithium_mode mode,
      const uint8_t *signature,
      size_t signature_length,
      const uint8_t *message,
      size_t message_length,
      const uint8_t *public_key,
      size_t public_key_size
  );

  size_t pqcuda_dilithium_public_key_bytes(pqcuda_dilithium_mode mode);
  size_t pqcuda_dilithium_secret_key_bytes(pqcuda_dilithium_mode mode);
  size_t pqcuda_dilithium_signature_bytes(pqcuda_dilithium_mode mode);

  int pqcuda_hybrid_test(void);

  int pqcuda_hybrid_test_mode(pqcuda_dilithium_mode mode);

  #ifdef __cplusplus
  }
  #endif
