#include <cstdint>
  #include <cstdio>
  #include <cstring>
  #include <vector>

  #include "pqcuda.h"

  namespace {

  constexpr size_t FRODO_PUBLIC_KEY_BYTES = 15632;
  constexpr size_t FRODO_SECRET_KEY_BYTES = 31296;
  constexpr size_t FRODO_CIPHERTEXT_BYTES = 15744;
  constexpr size_t FRODO_SHARED_SECRET_BYTES = 24;

  } // namespace

  int pqcuda_hybrid_test_mode(pqcuda_dilithium_mode mode)
  {
      std::vector<uint8_t> frodo_pk(FRODO_PUBLIC_KEY_BYTES);
      std::vector<uint8_t> frodo_sk(FRODO_SECRET_KEY_BYTES);
      std::vector<uint8_t> ciphertext(FRODO_CIPHERTEXT_BYTES);
      std::vector<uint8_t> encapsulated_secret(
          FRODO_SHARED_SECRET_BYTES);
      std::vector<uint8_t> decapsulated_secret(
          FRODO_SHARED_SECRET_BYTES);

      if (pqcuda_frodo_keypair(
              frodo_pk.data(),
              frodo_sk.data()) != 0) {
          std::fprintf(stderr, "FrodoKEM key generation: FAILED\n");
          return -1;
      }

      if (pqcuda_frodo_encapsulate(
              ciphertext.data(),
              encapsulated_secret.data(),
              frodo_pk.data()) != 0) {
          std::fprintf(stderr, "FrodoKEM encapsulation: FAILED\n");
          return -1;
      }

      std::printf("FrodoKEM encapsulation: PASSED\n");

      if (pqcuda_dilithium_sign_verify_mode(
              mode,
              ciphertext.data(),
              ciphertext.size()) != 0) {
          std::fprintf(stderr, "Dilithium verification: FAILED\n");
          return -1;
      }

      std::printf("Dilithium-%d signing: PASSED\n", (int)mode);
      std::printf("Dilithium-%d verification: PASSED\n", (int)mode);

      if (pqcuda_frodo_decapsulate(
              decapsulated_secret.data(),
              ciphertext.data(),
              frodo_sk.data()) != 0) {
          std::fprintf(stderr, "FrodoKEM decapsulation: FAILED\n");
          return -1;
      }

      if (std::memcmp(
              encapsulated_secret.data(),
              decapsulated_secret.data(),
              FRODO_SHARED_SECRET_BYTES) != 0) {
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
