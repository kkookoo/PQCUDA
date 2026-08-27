#include "pqcuda.h"

extern "C" int pqcuda_dilithium2_sign_verify(const uint8_t *, size_t);
extern "C" int pqcuda_dilithium3_sign_verify(const uint8_t *, size_t);
extern "C" int pqcuda_dilithium5_sign_verify(const uint8_t *, size_t);

#define DECLARE_MODE_API(mode) \
extern "C" int pqcuda_dilithium##mode##_keypair(uint8_t *, uint8_t *); \
extern "C" int pqcuda_dilithium##mode##_sign( \
    uint8_t *, size_t *, const uint8_t *, size_t, const uint8_t *); \
extern "C" int pqcuda_dilithium##mode##_verify( \
    const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *)

DECLARE_MODE_API(2);
DECLARE_MODE_API(3);
DECLARE_MODE_API(5);

#undef DECLARE_MODE_API

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

extern "C" int pqcuda_dilithium_keypair(
    pqcuda_dilithium_mode mode,
    uint8_t *public_key,
    size_t public_key_size,
    uint8_t *secret_key,
    size_t secret_key_size)
{
    if (public_key == nullptr || secret_key == nullptr ||
        public_key_size != pqcuda_dilithium_public_key_bytes(mode) ||
        secret_key_size != pqcuda_dilithium_secret_key_bytes(mode)) return -1;
    switch (mode) {
    case PQCUDA_DILITHIUM_MODE_2: return pqcuda_dilithium2_keypair(public_key, secret_key);
    case PQCUDA_DILITHIUM_MODE_3: return pqcuda_dilithium3_keypair(public_key, secret_key);
    case PQCUDA_DILITHIUM_MODE_5: return pqcuda_dilithium5_keypair(public_key, secret_key);
    default: return -1;
    }
}

extern "C" int pqcuda_dilithium_sign(
    pqcuda_dilithium_mode mode,
    uint8_t *signature,
    size_t signature_capacity,
    size_t *signature_length,
    const uint8_t *message,
    size_t message_length,
    const uint8_t *secret_key,
    size_t secret_key_size)
{
    const size_t required_signature = pqcuda_dilithium_signature_bytes(mode);
    if (signature == nullptr || signature_length == nullptr ||
        (message == nullptr && message_length != 0) || secret_key == nullptr ||
        signature_capacity < required_signature ||
        secret_key_size != pqcuda_dilithium_secret_key_bytes(mode)) return -1;
    switch (mode) {
    case PQCUDA_DILITHIUM_MODE_2: return pqcuda_dilithium2_sign(signature, signature_length, message, message_length, secret_key);
    case PQCUDA_DILITHIUM_MODE_3: return pqcuda_dilithium3_sign(signature, signature_length, message, message_length, secret_key);
    case PQCUDA_DILITHIUM_MODE_5: return pqcuda_dilithium5_sign(signature, signature_length, message, message_length, secret_key);
    default: return -1;
    }
}

extern "C" int pqcuda_dilithium_verify(
    pqcuda_dilithium_mode mode,
    const uint8_t *signature,
    size_t signature_length,
    const uint8_t *message,
    size_t message_length,
    const uint8_t *public_key,
    size_t public_key_size)
{
    if (signature == nullptr || (message == nullptr && message_length != 0) ||
        public_key == nullptr ||
        signature_length != pqcuda_dilithium_signature_bytes(mode) ||
        public_key_size != pqcuda_dilithium_public_key_bytes(mode)) return -1;
    switch (mode) {
    case PQCUDA_DILITHIUM_MODE_2: return pqcuda_dilithium2_verify(signature, signature_length, message, message_length, public_key);
    case PQCUDA_DILITHIUM_MODE_3: return pqcuda_dilithium3_verify(signature, signature_length, message, message_length, public_key);
    case PQCUDA_DILITHIUM_MODE_5: return pqcuda_dilithium5_verify(signature, signature_length, message, message_length, public_key);
    default: return -1;
    }
}
