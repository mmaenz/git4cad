#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include <openssl/evp.h>

namespace util {

[[nodiscard]] inline std::string to_hex(const uint8_t* data, std::size_t len) {
    constexpr char kHexChars[] = "0123456789abcdef";
    std::string result{};
    result.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        result += kHexChars[(data[i] >> 4u) & 0x0Fu];
        result += kHexChars[data[i] & 0x0Fu];
    }
    return result;
}

[[nodiscard]] inline std::string sha256_hex(const void* data, std::size_t len) {
    uint8_t digest[EVP_MAX_MD_SIZE] = {};
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    return to_hex(digest, digest_len);
}

[[nodiscard]] inline std::string sha256_hex(std::string_view s) {
    return sha256_hex(s.data(), s.size());
}

} // namespace util
