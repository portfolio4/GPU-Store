#include <funx/net/UdpSocket.h>

#include <funx/util/expected.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

namespace funx {

std::expected<UdpSocket::FdConfig, std::string>
UdpSocket::configToFdConfig(UdpSocket::Config const& c)
{
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        return std::unexpected(std::format("Could not create UdpSocket. Errno={}", errno));
    }

    if (fcntl(sock_fd, F_SETFL, fcntl(sock_fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
        ::close(sock_fd);
        return std::unexpected(std::format("Could not make UdpSocket nonblocking. Errno={}", errno));
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(c.local_port.value_or(funx::UdpPort{0}));
    if (c.local_ip4) {
        local_addr.sin_addr.s_addr = inet_addr(c.local_ip4->c_str());
        if (local_addr.sin_addr.s_addr == INADDR_NONE) {
            ::close(sock_fd);
            return std::unexpected(std::format("Invalid UDP local address: {}", *c.local_ip4));
        }
    } else {
        local_addr.sin_addr.s_addr = INADDR_ANY;
    }

    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(c.remote_port.value_or(funx::UdpPort{0}));
    if (c.remote_ip4) {
        remote_addr.sin_addr.s_addr = inet_addr(c.remote_ip4->c_str());
        if (remote_addr.sin_addr.s_addr == INADDR_NONE) {
            ::close(sock_fd);
            return std::unexpected(std::format("Invalid UDP remote address: {}", c.remote_ip4->c_str()));
        }
    } else {
        remote_addr.sin_addr.s_addr = INADDR_ANY;
    }

    bool is_multicast_receiver = [&]() {
        uint32_t addr = ntohl(local_addr.sin_addr.s_addr);
        uint8_t first_octet = (addr >> 24) & 0xFF;
        return 224 <= first_octet && first_octet <= 239;
    }();
    if (is_multicast_receiver) {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = local_addr.sin_addr.s_addr;
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
            ::close(sock_fd);
            return std::unexpected(std::format("Failed to enable IGMP joins for multicast {}:{}. Errno={}",
                        c.local_ip4.value_or("any"), c.local_port.value_or(funx::UdpPort{0}), errno));
        }

        int opt = 1;
        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            ::close(sock_fd);
            return std::unexpected(std::format("Failed to set SO_REUSEADDR for multicast listener. Errno={}", errno));
        }
        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
            ::close(sock_fd);
            return std::unexpected(std::format("Failed to set SO_REUSEPORT for multicast listener. Errno={}", errno));
        }

        local_addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(sock_fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
       ::close(sock_fd);
       return std::unexpected(std::format("Failed to bind UDP socket to {}:{}. Errno={}",
                   c.local_ip4.value_or("any"), c.local_port.value_or(funx::UdpPort{0}), errno));
    }

    return FdConfig{
        .sock_fd = sock_fd,
        .local_addr = local_addr,
        .remote_addr = remote_addr,
    };
}

std::expected<UdpSocket, std::string> UdpSocket::create(Config const& c)
{
    return UdpSocket::create(FUNX_TRY(UdpSocket::configToFdConfig(c)));
}

std::expected<UdpSocket, std::string> UdpSocket::create(FdConfig const& c)
{
    return UdpSocket(c.sock_fd, c.local_addr, c.remote_addr);
}

UdpSocket::UdpSocket(int sock_fd, sockaddr_in local_addr, sockaddr_in remote_addr)
    : sock_fd_(sock_fd)
    , local_addr_(local_addr)
    , remote_addr_(remote_addr)
    , remote_addr_len_(sizeof(remote_addr))
{
}

UdpSocket::~UdpSocket()
{
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other)
{
    moveFrom(std::move(other));
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other)
{
    close();
    moveFrom(std::move(other));
    return *this;
}

void UdpSocket::close() {
    if (!closed_) {
        ::close(sock_fd_);
        closed_ = true;
    }
}

void UdpSocket::moveFrom(UdpSocket&& other)
{
    if (this == &other) {
        return;
    }
    sock_fd_ = std::exchange(other.sock_fd_, -1);
    local_addr_ = std::exchange(other.local_addr_, sockaddr_in{});
    remote_addr_ = std::exchange(other.remote_addr_, sockaddr_in{});
    remote_addr_len_ = other.remote_addr_len_;
    closed_ = std::exchange(other.closed_, true); for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        read_buffer_[i] = std::exchange(other.read_buffer_[i], std::byte{0});
    }
}

std::expected<void, int> UdpSocket::trySend(funx::byte_view bytes)
{
    do {
        if (sendto(sock_fd_, bytes.data(), bytes.size(), 0, (struct sockaddr*)&remote_addr_, sizeof(remote_addr_)) > 0) [[likely]] {
            return {};
        }
    } while (errno == EAGAIN);
    return std::unexpected(errno);;
}

std::optional<funx::byte_view> UdpSocket::tryRead() {
    int bytes_received = recvfrom(sock_fd_, (char*)read_buffer_, sizeof(read_buffer_), 0, (struct sockaddr*)&remote_addr_, &remote_addr_len_);
    if (bytes_received > 0) {
        return funx::byte_view(read_buffer_, bytes_received);
    } else {
        return std::nullopt;
    }
}

} // namespace funx

