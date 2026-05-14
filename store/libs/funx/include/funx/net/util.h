#ifndef FUNX_NET_UTIL_H
#define FUNX_NET_UTIL_H

#include <funx/net/types.h>

#include <cassert>
#include <cctype>
#include <format>

namespace funx::util {

template <typename T>
funx::byte_view to_bytes(T const& t) {
    return funx::byte_view(reinterpret_cast<const std::byte*>(&t), sizeof(T));
}

template <typename T>
T const& from_bytes(funx::byte_view bytes) {
    assert(bytes.size() >= sizeof(T));
    return *reinterpret_cast<T const*>(bytes.data());
}

inline std::string to_hexdump(funx::byte_view bytes) {
    std::string rtn;

    // 16 bytes per line
    for (size_t i = 0; i < bytes.size(); i += 16) {
        std::string hex;
        std::string ascii;
        for (size_t j = i; j < bytes.size() && j < i + 16; ++j) {
            char b = static_cast<char>(bytes[j]);
            if (j != i && j % 2 == 0) {
                hex += " ";
            }
            hex += std::format("{:02x}", b);
            ascii += std::format("{}", std::isprint(b) ? b : '.');
        }
        while (hex.size() < 40) {
            hex += " ";
        }
        rtn += std::format("{}    {}\n", hex, ascii);
    }
    return rtn;
}

} // namespace funx

#endif  // FUNX_NET_UTIL_H

