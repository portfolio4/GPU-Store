#ifndef FUNX_NET_TYPES_H
#define FUNX_NET_TYPES_H

#include <cstddef>
#include <format>
#include <type_traits>
#include <utility>
#include <span>

namespace funx {

using mut_byte_view = std::span<std::byte>;
using byte_view = std::span<const std::byte>;

inline std::string_view byte_to_str_view(byte_view bv) {
    return std::string_view{
        reinterpret_cast<const char*>(bv.data()),
        bv.size()
    };
}

inline byte_view str_to_byte_view(std::string_view sv) {
    return byte_view{
        reinterpret_cast<const std::byte*>(sv.data()),
        sv.size()
    };
}

// Network byte order (big endian)
enum Ip4Addr : uint32_t {};

// Host byte order (little endian)
enum TcpPort : uint16_t {};
enum UdpPort : uint16_t {};

struct TcpEndpointDetails {
    Ip4Addr ip4;
    TcpPort port;

    bool operator==(TcpEndpointDetails const& other) const = default;
    auto operator<=>(TcpEndpointDetails const&) const = default;
};

} // namespace funx

template <>
struct std::formatter<funx::Ip4Addr> : std::formatter<std::string> {
    auto format(funx::Ip4Addr ip, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}.{}.{}.{}",
                ((+ip) >> 24) & 0xFF,
                ((+ip) >> 16) & 0xFF,
                ((+ip) >> 8) & 0xFF,
                ((+ip) >> 0) & 0xFF)
            , ctx
        );
    }
};

template <>
struct std::formatter<funx::TcpPort> : std::formatter<std::string> {
    auto format(funx::TcpPort p, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", +p), ctx);
    }
};

template <>
struct std::formatter<funx::UdpPort> : std::formatter<std::string> {
    auto format(funx::UdpPort p, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}", +p), ctx);
    }
};

template <>
struct std::formatter<funx::TcpEndpointDetails> : std::formatter<std::string> {
    auto format(funx::TcpEndpointDetails p, format_context& ctx) const {
        return formatter<string>::format(
            std::format("{}:{}", p.ip4, p.port), ctx);
    }
};

namespace std {

template <>
struct hash<funx::TcpEndpointDetails> {
    std::size_t operator()(funx::TcpEndpointDetails const& d) {
        // top 32 bits is ip4 address, bottom 32 bits is port
        uint64_t int_repr = (static_cast<uint64_t>(+d.ip4) << 32) | (+d.port);
        return std::hash<uint64_t>{}(int_repr);
    }
};

} // namespace std

#endif  // FUNX_NET_TYPES_H

