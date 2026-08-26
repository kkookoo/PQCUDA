#include "pqcuda.h"

extern "C" int pqcuda_dilithium2_sign_verify(const uint8_t *, size_t);
extern "C" int pqcuda_dilithium3_sign_verify(const uint8_t *, size_t);
extern "C" int pqcuda_dilithium5_sign_verify(const uint8_t *, size_t);

extern "C" int pqcuda_dilithium_sign_verify_mode(
    pqcuda_dilithium_mode mode,
    const uint8_t *message,
    size_t message_length)
{
    if (message == nullptr && message_length != 0) {
        return -1;
    }

    switch (mode) {
    case PQCUDA_DILITHIUM_MODE_2:
        return pqcuda_dilithium2_sign_verify(message, message_length);
    case PQCUDA_DILITHIUM_MODE_3:
        return pqcuda_dilithium3_sign_verify(message, message_length);
    case PQCUDA_DILITHIUM_MODE_5:
        return pqcuda_dilithium5_sign_verify(message, message_length);
    default:
        return -1;
    }
}

extern "C" int pqcuda_dilithium_sign_verify(
    const uint8_t *message,
    size_t message_length)
{
    return pqcuda_dilithium_sign_verify_mode(
        PQCUDA_DILITHIUM_MODE_2, message, message_length);
}

extern "C" size_t pqcuda_dilithium_public_key_bytes(
    pqcuda_dilithium_mode mode)
{
    switch (mode) {
    case PQCUDA_DILITHIUM_MODE_2: return PQCUDA_DILITHIUM2_PUBLIC_KEY_BYTES;
    case PQCUDA_DILITHIUM_MODE_3: return PQCUDA_DILITHIUM3_PUBLIC_KEY_BYTES;
    case PQCUDA_DILITHIUM_MODE_5: return PQCUDA_DILITHIUM5_PUBLIC_KEY_BYTES;
    default: return 0;
    }
}

extern "C" size_t pqcuda_dilithium_secret_key_bytes(
    pqcuda_dilithium_mode mode)
{
    switch (mode) {
    case PQCUDA_DILITHIUM_MODE_2: return PQCUDA_DILITHIUM2_SECRET_KEY_BYTES;
    case PQCUDA_DILITHIUM_MODE_3: return PQCUDA_DILITHIUM3_SECRET_KEY_BYTES;
    case PQCUDA_DILITHIUM_MODE_5: return PQCUDA_DILITHIUM5_SECRET_KEY_BYTES;
    default: return 0;
    }
}

extern "C" size_t pqcuda_dilithium_signature_bytes(
    pqcuda_dilithium_mode mode)
{
    switch (mode) {
    case PQCUDA_DILITHIUM_MODE_2: return PQCUDA_DILITHIUM2_SIGNATURE_BYTES;
    case PQCUDA_DILITHIUM_MODE_3: return PQCUDA_DILITHIUM3_SIGNATURE_BYTES;
    case PQCUDA_DILITHIUM_MODE_5: return PQCUDA_DILITHIUM5_SIGNATURE_BYTES;
    default: return 0;
    }
}
