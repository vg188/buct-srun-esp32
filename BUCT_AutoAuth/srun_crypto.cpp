#include "srun_crypto.h"
#include <mbedtls/md.h>
#include <mbedtls/sha1.h>
#include <string.h>

namespace srun {

static void toHex(const uint8_t *digest, size_t n, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2]     = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

void hmacMd5Hex(const char *key, size_t keyLen,
                const char *msg, size_t msgLen,
                char out[33]) {
    uint8_t digest[16];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_MD5),
                    (const uint8_t *)key, keyLen,
                    (const uint8_t *)msg, msgLen, digest);
    toHex(digest, 16, out);
}

void sha1Hex(const char *msg, size_t msgLen, char out[41]) {
    uint8_t digest[20];
    // mbedtls 3.x (esp32 core 3.x) renamed mbedtls_sha1_ret -> mbedtls_sha1;
    // both have identical signature.
    mbedtls_sha1((const uint8_t *)msg, msgLen, digest);
    toHex(digest, 20, out);
}

// ---- xencode (srun XXTEA variant) ----
// Translated from wyfhbb/BUCT-Login utils/xencode.go (MIT), itself from the
// portal's own JS. Little-endian 32-bit words; input padded with its length.

static uint32_t charAt(const char *s, size_t len, size_t idx) {
    return idx >= len ? 0 : (uint8_t)s[idx];
}

// str -> uint32 words (+ optional length word appended)
static size_t strToWords(const char *a, size_t len, bool appendLen,
                         uint32_t *v, size_t vCap) {
    size_t nWords = (len + 3) / 4 + (appendLen ? 1 : 0);
    if (nWords == 0 || nWords > vCap) return 0;
    for (size_t i = 0; i < nWords; i++) {
        size_t off = i * 4;
        v[i] = charAt(a, len, off)
             | charAt(a, len, off + 1) << 8
             | charAt(a, len, off + 2) << 16
             | charAt(a, len, off + 3) << 24;
    }
    if (appendLen) v[nWords - 1] = (uint32_t)len;
    return nWords;
}

size_t xEncode(const char *str, size_t strLen,
               const char *key, size_t keyLen,
               uint8_t *out, size_t outCap) {
    if (strLen == 0) return 0;

    uint32_t v[96];  // data words + 1 length word; info json < 300 bytes
    uint32_t k[4];
    size_t vLen = strToWords(str, strLen, true, v, 96);
    // XXTEA uses only k[0..3] (the Go/JS reference packs the full key into
    // words but indexes with (p&3)^e, i.e. only the first 4 words matter).
    size_t kLen = strToWords(key, keyLen > 16 ? 16 : keyLen, false, k, 4);
    if (vLen == 0 || kLen == 0) return 0;
    while (kLen < 4) k[kLen++] = 0;

    size_t wordBytes = vLen * 4;
    if (wordBytes > outCap) return 0;

    size_t n = vLen - 1;
    uint32_t z = v[n], y = v[0], c = 0x86014019 | 0x183639A0;
    uint32_t m, e, p, d = 0;
    uint32_t q = 6 + 52 / (n + 1);

    while (q-- > 0) {
        d = (d + c) & (0x8CE0D9BF | 0x731F2640);
        e = (d >> 2) & 3;
        for (p = 0; p < n; p++) {
            y = v[p + 1];
            m = (z >> 5 ^ y << 2) + ((y >> 3 ^ z << 4) ^ (d ^ y)) + (k[(p & 3) ^ e] ^ z);
            v[p] += m;
            z = v[p];
        }
        y = v[0];
        m = (z >> 5 ^ y << 2) + ((y >> 3 ^ z << 4) ^ (d ^ y)) + (k[(p & 3) ^ e] ^ z);
        v[n] += m;
        z = v[n];
    }

    memcpy(out, v, wordBytes);
    return wordBytes;
}

// ---- srun custom base64 ----
static const char ALPHA[] = "LVoJPiCN2R8G90yg+hmFHuacZ1OWMnrsSTXkYpUq/3dlbfKwv6xztjI7DeBE45QA";

size_t srunBase64(const uint8_t *in, size_t inLen, char *out, size_t outCap) {
    if (inLen == 0) { if (outCap) out[0] = '\0'; return 0; }

    size_t need = ((inLen + 2) / 3) * 4 + 1;
    if (need > outCap) return 0;

    size_t o = 0;
    size_t imax = inLen - (inLen % 3);
    for (size_t i = 0; i < imax; i += 3) {
        uint32_t b10 = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out[o++] = ALPHA[b10 >> 18];
        out[o++] = ALPHA[(b10 >> 12) & 63];
        out[o++] = ALPHA[(b10 >> 6) & 63];
        out[o++] = ALPHA[b10 & 63];
    }
    size_t rem = inLen - imax;
    if (rem == 1) {
        uint32_t b10 = in[imax] << 16;
        out[o++] = ALPHA[b10 >> 18];
        out[o++] = ALPHA[(b10 >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        uint32_t b10 = (in[imax] << 16) | (in[imax + 1] << 8);
        out[o++] = ALPHA[b10 >> 18];
        out[o++] = ALPHA[(b10 >> 12) & 63];
        out[o++] = ALPHA[(b10 >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

bool buildInfo(const char *json, const char *challenge,
               char *out, size_t outCap) {
    uint8_t enc[128];  // xEncode output = ((jsonLen+3)/4+1)*4 bytes; json < 100B
    size_t encLen = xEncode(json, strlen(json), challenge, strlen(challenge),
                            enc, sizeof(enc));
    if (encLen == 0) return false;

    size_t prefix = strlen("{SRBX1}");
    if (prefix + ((encLen + 2) / 3) * 4 + 1 > outCap) return false;
    strcpy(out, "{SRBX1}");
    return srunBase64(enc, encLen, out + prefix, outCap - prefix) > 0;
}

} // namespace srun
