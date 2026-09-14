#pragma once
#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

namespace srun {

// HMAC-MD5(password, challenge) -> 32-char lowercase hex
void hmacMd5Hex(const char *key, size_t keyLen,
                const char *msg, size_t msgLen,
                char out[33]);

// SHA1(str) -> 40-char lowercase hex
void sha1Hex(const char *msg, size_t msgLen, char out[41]);

// XXTEA xencode (srun variant, little-endian words, length-prefixed input)
// Returns byte length written to out (<= outCap); 0 on error.
size_t xEncode(const char *str, size_t strLen,
               const char *key, size_t keyLen,
               uint8_t *out, size_t outCap);

// Srun custom base64 (alphabet LVoJPiCN2R8G90yg+hmFHuacZ1OWMnrsSTXkYpUq/3dlbfKwv6xztjI7DeBE45QA)
// Returns byte length written to out (NUL-terminated); 0 on error.
size_t srunBase64(const uint8_t *in, size_t inLen, char *out, size_t outCap);

// Builds the "info" field: "{SRBX1}" + srunBase64(xEncode(json, challenge))
// Returns false if buffer too small.
bool buildInfo(const char *json, const char *challenge,
               char *out, size_t outCap);

} // namespace srun
