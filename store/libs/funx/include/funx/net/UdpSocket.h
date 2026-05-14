#ifndef FUNX_NET_UDP_SOCKET_H
#define FUNX_NET_UDP_SOCKET_H

#include <funx/net/types.h>
#include <funx/net/util.h>

#include <cstring>
#include <expected>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>

namespace funx {

class UdpSocket
{
public:

    static constexpr size_t MTU = 1500;

    struct FdConfig {
        int sock_fd;
        sockaddr_in local_addr;
        sockaddr_in remote_addr;
    };
    static std::expected<UdpSocket, std::string> create(FdConfig const&);

    struct Config {
        // If an ip4 is not provided, we will use INADDR_ANY for the IP.
        // If no port is provided, we will use port 0 to indicate any port.
        // Note: Multicast senders must not set local_ip4. Otherwise, it may go
        // out the wrong interface and not be routed properly
        std::optional<std::string> local_ip4;
        std::optional<funx::UdpPort> local_port;
        std::optional<std::string> remote_ip4;
        std::optional<funx::UdpPort> remote_port;
    };
    static std::expected<UdpSocket, std::string> create(Config const&);

    template <typename ConfigT>
    static UdpSocket createOrThrow(ConfigT const& c) {
        auto res = create(c);
        if (!res) {
            throw std::runtime_error(res.error());
        }
        return std::move(*res);
    }

    ~UdpSocket();

    UdpSocket(UdpSocket const& other) = delete;
    UdpSocket& operator=(UdpSocket const& other) = delete;

    UdpSocket(UdpSocket&& other);
    UdpSocket& operator=(UdpSocket&& other);

    // If send fails, the errno is returned
    // This call will block as long as sendto returns errno EAGAIN (indicating
    // the transmit buffer is full)
    std::expected<void, int> trySend(funx::byte_view bytes);

    std::optional<funx::byte_view> tryRead();

private:

    UdpSocket(int sock_fd, sockaddr_in local_addr, sockaddr_in remote_addr);

    static std::expected<FdConfig, std::string> configToFdConfig(Config const&);

    void moveFrom(UdpSocket&& other);

    void close();

private:

    int sock_fd_;
    sockaddr_in local_addr_;
    sockaddr_in remote_addr_;
    socklen_t remote_addr_len_;

    bool closed_ = false;

    static constexpr size_t BUFFER_SIZE = 2048;
    static_assert(MTU <= BUFFER_SIZE);
    std::byte read_buffer_[BUFFER_SIZE] = {static_cast<std::byte>(0)};

};

} // namespace funx

#endif // FUNX_NET_UDP_SOCKET_H

