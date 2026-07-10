#pragma once
#include "picohash.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crypto {
    static constexpr size_t SHA256_SIZE = PICOHASH_SHA256_DIGEST_LENGTH;

    inline void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t* out) {
        picohash_ctx_t ctx;
        picohash_init_hmac(&ctx, picohash_init_sha256, key, keyLen);
        picohash_update(&ctx, data, dataLen);
        picohash_final(&ctx, out);
    }

    inline void pbkdf2Sha256(const uint8_t* password, size_t passwordLen, const uint8_t* salt, size_t saltLen, uint32_t rounds, uint8_t* out, size_t outLen) {
        if (rounds == 0 || outLen == 0) { return; }

        uint8_t u[SHA256_SIZE];
        uint8_t t[SHA256_SIZE];
        uint8_t blockIndex[4];
        size_t generated = 0;
        uint32_t block = 1;
        while (generated < outLen) {
            blockIndex[0] = (uint8_t)((block >> 24) & 0xff);
            blockIndex[1] = (uint8_t)((block >> 16) & 0xff);
            blockIndex[2] = (uint8_t)((block >> 8) & 0xff);
            blockIndex[3] = (uint8_t)(block & 0xff);

            picohash_ctx_t ctx;
            picohash_init_hmac(&ctx, picohash_init_sha256, password, passwordLen);
            picohash_update(&ctx, salt, saltLen);
            picohash_update(&ctx, blockIndex, sizeof(blockIndex));
            picohash_final(&ctx, u);
            memcpy(t, u, SHA256_SIZE);

            for (uint32_t i = 1; i < rounds; i++) {
                hmacSha256(password, passwordLen, u, SHA256_SIZE, u);
                for (size_t j = 0; j < SHA256_SIZE; j++) { t[j] ^= u[j]; }
            }

            size_t toCopy = std::min(outLen - generated, SHA256_SIZE);
            memcpy(out + generated, t, toCopy);
            generated += toCopy;
            block++;
        }

        memset(u, 0, sizeof(u));
        memset(t, 0, sizeof(t));
    }

    inline bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t len) {
        uint8_t diff = 0;
        for (size_t i = 0; i < len; i++) { diff |= a[i] ^ b[i]; }
        return diff == 0;
    }
}
