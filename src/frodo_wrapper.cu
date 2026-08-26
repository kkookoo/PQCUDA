#include "pqcuda.h"
  #include "api.h"

  int pqcuda_frodo_keypair(uint8_t *pk, uint8_t *sk)
  {
      return crypto_kem_keypair_gpu(pk, sk);
  }

  int pqcuda_frodo_encapsulate(
      uint8_t *ciphertext,
      uint8_t *shared_secret,
      const uint8_t *public_key)
  {
      return crypto_kem_enc(ciphertext, shared_secret, public_key);
  }

  int pqcuda_frodo_decapsulate(
      uint8_t *shared_secret,
      const uint8_t *ciphertext,
      const uint8_t *secret_key)
  {
      return crypto_kem_dec(shared_secret, ciphertext, secret_key);
  }