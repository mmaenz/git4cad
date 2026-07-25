#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace util {

namespace detail {

constexpr std::string_view kB64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr int b64_index(char c) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+')              return 62;
    if (c == '/')              return 63;
    return -1;
}

} // namespace detail

[[nodiscard]] inline std::string base64_encode(const uint8_t* data, std::size_t len) {
    std::string result{};
    result.reserve(((len + 2) / 3) * 4);

    for (std::size_t i = 0; i < len; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0u;
        const uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0u;
        const uint32_t triple = (b0 << 16u) | (b1 << 8u) | b2;

        result += detail::kB64Chars[(triple >> 18u) & 0x3Fu];
        result += detail::kB64Chars[(triple >> 12u) & 0x3Fu];
        result += (i + 1 < len) ? detail::kB64Chars[(triple >> 6u) & 0x3Fu] : '=';
        result += (i + 2 < len) ? detail::kB64Chars[triple & 0x3Fu]         : '=';
    }

    return result;
}

[[nodiscard]] inline std::vector<uint8_t> base64_decode(std::string_view input) {
    std::vector<uint8_t> result{};
    result.reserve((input.size() / 4) * 3);

    int buf[4] = {};
    int buf_len = 0;

    for (const char c : input) {
        if (c == '=') break;
        const int idx = detail::b64_index(c);
        if (idx < 0) continue;

        buf[buf_len++] = idx;
        if (buf_len == 4) {
            result.push_back(static_cast<uint8_t>((buf[0] << 2) | (buf[1] >> 4)));
            result.push_back(static_cast<uint8_t>((buf[1] << 4) | (buf[2] >> 2)));
            result.push_back(static_cast<uint8_t>((buf[2] << 6) | buf[3]));
            buf_len = 0;
        }
    }

    if (buf_len >= 2) result.push_back(static_cast<uint8_t>((buf[0] << 2) | (buf[1] >> 4)));
    if (buf_len >= 3) result.push_back(static_cast<uint8_t>((buf[1] << 4) | (buf[2] >> 2)));

    return result;
}

} // namespace util
