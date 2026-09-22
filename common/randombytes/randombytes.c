#include "pqcuda_random.h"

#ifdef _WIN32
#include <limits.h>
#include <windows.h>
#include <bcrypt.h>
#else
#include <errno.h>
#include <sys/random.h>
#endif

int pqcuda_random_bytes(uint8_t *output, size_t size) {
    if (output == NULL && size != 0) return 0;
    while (size != 0) {
#ifdef _WIN32
        const ULONG chunk = size > ULONG_MAX ? ULONG_MAX : (ULONG)size;
        if (BCryptGenRandom(NULL, output, chunk,
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            return 0;
        output += chunk;
        size -= chunk;
#else
        const ssize_t received = getrandom(output, size, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) return 0;
        output += (size_t)received;
        size -= (size_t)received;
#endif
    }
    return 1;
}
