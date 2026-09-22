#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 after filling the entire buffer, or 0 on failure. */
int pqcuda_random_bytes(uint8_t *output, size_t size);

#ifdef __cplusplus
}
#endif
