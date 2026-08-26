#include <cstdio>
#include <cstdlib>

  #include "pqcuda.h"

  int main(int argc, char **argv)
  {
      int requested_mode = 2;
      if (argc > 2) {
          std::fprintf(stderr, "Usage: %s [2|3|5]\n", argv[0]);
          return 2;
      }
      if (argc == 2) {
          requested_mode = std::atoi(argv[1]);
      }
      if (requested_mode != 2 && requested_mode != 3 && requested_mode != 5) {
          std::fprintf(stderr, "Invalid Dilithium mode: %d (use 2, 3, or 5)\n",
                       requested_mode);
          return 2;
      }

      const pqcuda_dilithium_mode mode =
          static_cast<pqcuda_dilithium_mode>(requested_mode);
      std::printf("Running hybrid test with Dilithium-%d\n", requested_mode);

      if (pqcuda_hybrid_test_mode(mode) != 0) {
          std::fprintf(stderr, "Hybrid test: FAILED\n");
          return 1;
      }

      std::printf("Hybrid test: PASSED\n");
      return 0;
  }
