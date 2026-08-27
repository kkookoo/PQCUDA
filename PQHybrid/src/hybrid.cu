#include <cstdint>
  #include <cstdio>
  #include <cstring>
  #include <vector>

  #include "pqcuda.h"

  namespace {

  } // namespace

  int pqcuda_hybrid_test_mode(pqcuda_dilithium_mode mode)
  {
      std::vector<uint8_t> kyber_pk(PQCUDA_KYBER1024_PUBLIC_KEY_BYTES);
      std::vector<uint8_t> kyber_sk(PQCUDA_KYBER1024_SECRET_KEY_BYTES);
      std::vector<uint8_t> ciphertext(PQCUDA_KYBER1024_CIPHERTEXT_BYTES);
      std::vector<uint8_t> encapsulated_secret(
          PQCUDA_KYBER1024_SHARED_SECRET_BYTES);
      std::vector<uint8_t> decapsulated_secret(
          PQCUDA_KYBER1024_SHARED_SECRET_BYTES);

      if (pqcuda_kyber1024_keypair(
              kyber_pk.data(),
              kyber_sk.data()) != 0) {
          std::fprintf(stderr, "Kyber1024 key generation: FAILED\n");
          return -1;
      }

      if (pqcuda_kyber1024_encapsulate(
              ciphertext.data(),
              encapsulated_secret.data(),
              kyber_pk.data()) != 0) {
          std::fprintf(stderr, "Kyber1024 encapsulation: FAILED\n");
          return -1;
      }

      std::printf("Kyber1024 encapsulation: PASSED\n");

      if (pqcuda_dilithium_sign_verify_mode(
              mode,
              ciphertext.data(),
              ciphertext.size()) != 0) {
          std::fprintf(stderr, "Dilithium verification: FAILED\n");
          return -1;
      }

      std::printf("Dilithium-%d signing: PASSED\n", (int)mode);
      std::printf("Dilithium-%d verification: PASSED\n", (int)mode);

      if (pqcuda_kyber1024_decapsulate(
              decapsulated_secret.data(),
              ciphertext.data(),
              kyber_sk.data()) != 0) {
          std::fprintf(stderr, "Kyber1024 decapsulation: FAILED\n");
          return -1;
      }

      if (std::memcmp(
              encapsulated_secret.data(),
              decapsulated_secret.data(),
              PQCUDA_KYBER1024_SHARED_SECRET_BYTES) != 0) {
          std::fprintf(stderr, "Shared secret comparison: FAILED\n");
          return -1;
      }

      std::printf("Shared secret comparison: PASSED\n");
      return 0;
  }

  int pqcuda_hybrid_test(void)
  {
      return pqcuda_hybrid_test_mode(PQCUDA_DILITHIUM_MODE_2);
  }
