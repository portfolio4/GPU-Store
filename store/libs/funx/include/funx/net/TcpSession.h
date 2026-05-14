#ifndef FUNX_NET_TCP_SESSION_H
#define FUNX_NET_TCP_SESSION_H

#include <funx/clock/Clock.h>
#include <funx/net/TcpSocket.h>

#include <array>
#include <chrono>
#include <cstring>
#include <type_traits>
#include <optional>
#include <string>

namespace funx {

class TcpSession
{
public:

    static constexpr size_t MTU = TcpSocket::MTU;

    struct Config {
        TcpSocket&& socket;
    };

    TcpSession(Config c);

    // Gets bytes but does not consume them. Note: you can only peek up to
    // TcpSocket::MTU. Will return nullopt if you try to peek for more than MTU
    std::optional<funx::byte_view> peek(size_t bytes);

    template <typename T>
    std::optional<const T*> peek() {
        std::optional<funx::byte_view> peek_res = peek(sizeof(T));
        if (!peek_res) {
            return std::nullopt;
        }
        return reinterpret_cast<const T*>(peek_res->data());
    }

    template <typename T>
    std::optional<T> peekCopy() {
        static_assert(std::is_trivially_copyable_v<T>);
        std::optional<funx::byte_view> peek_res = peek(sizeof(T));
        if (!peek_res) {
            return std::nullopt;
        }
        T value;
        std::memcpy(&value, peek_res->data(), sizeof(T));
        return value;
    }

    template <typename T>
    std::optional<const T*> waitUntil(Clock::time_point timeout) {
        do {
            auto peek_res = peek<T>();
            if (peek_res) {
                return peek_res;
            }
        } while (Clock::now() < timeout);
        return std::nullopt;
    }

    // Consumes up to the given number of bytes.
    // If we don't have enough bytes available, then all the available bytes
    // will be consumed and the remainder of `bytes` is ignored.
    void consume(size_t bytes);

    void send(funx::byte_view bytes);

    void close();

    bool closed() const;

    funx::TcpEndpointDetails getRemoteTcpEndpoint() const;

private:

    size_t numAvailableBytes() const;
    size_t capacityLeft() const;

private:
    TcpSocket socket_;

    // Large enough to make it impossible for us to exhaust the buffer space
    constexpr static size_t BUFFER_SIZE = 8192;
    static_assert(4*TcpSocket::MTU <= BUFFER_SIZE);
    std::array<std::byte, BUFFER_SIZE> buffer_;

    size_t buffer_start_ = 0;
    size_t buffer_end_ = 0;
};

} // namespace funx

#endif  // FUNX_NET_TCP_SESSION_H

