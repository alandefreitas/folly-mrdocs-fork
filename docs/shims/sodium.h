// Minimal libsodium stand-in for MrDocs parsing of Folly's crypto headers.
//
// Not real libsodium. Declares just the blake2b state type (used as a data
// member in folly/crypto/Blake2xb.h, so it must be complete) plus the handful
// of functions Folly names, so those headers parse under MrDocs in isolation.
#ifndef SODIUM_SHIM_H_
#define SODIUM_SHIM_H_

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct crypto_generichash_blake2b_state {
    unsigned char opaque[384];
} crypto_generichash_blake2b_state;

int crypto_generichash_blake2b(
    unsigned char* out, size_t outlen,
    const unsigned char* in, unsigned long long inlen,
    const unsigned char* key, size_t keylen);

int crypto_generichash_blake2b_salt_personal(
    unsigned char* out, size_t outlen,
    const unsigned char* in, unsigned long long inlen,
    const unsigned char* key, size_t keylen,
    const unsigned char* salt, const unsigned char* personal);

int sodium_init(void);
int sodium_memcmp(const void* b1, const void* b2, size_t len);
void sodium_memzero(void* pnt, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_SHIM_H_
