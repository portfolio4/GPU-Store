#include <funx/net/TcpSocket.h>

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#include <utility>

#include <funx/log/Log.h>
#include <funx/util/expected.h>

namespace funx {

std::expected<TcpSocket::FdConfig, std::string>
TcpSocket::configToFdConfig(Config const& c)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        return std::unexpected(std::format("Could not create TcpSocket. Errno={}", errno));
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(c.port);
    saddr.sin_addr.s_addr = inet_addr(c.ip4.c_str());

    if (connect(sock_fd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        ::close(sock_fd);
        return std::unexpected(std::format("Could not connect to {}:{}", c.ip4, c.port));
    }

    return FdConfig{
        .sock_fd = sock_fd,
        .sin = saddr,
    };
}

TcpEndpointDetails TcpSocket::getLocalTcpEndpoint(int sock_fd)
{
    struct sockaddr_in local_addr;
    socklen_t len = sizeof(local_addr);
    if (getsockname(sock_fd, (struct sockaddr *)&local_addr, &len) == -1) {
        auto error_msg = std::format("Failed to getsockname. Errno={}", errno);
        std::println("{}", error_msg);
        throw std::runtime_error(error_msg);
    }
    return TcpEndpointDetails{
        .ip4 = funx::Ip4Addr{ntohl(local_addr.sin_addr.s_addr)},
        .port = funx::TcpPort{ntohs(local_addr.sin_port)},
    };
}

std::expected<TcpSocket, std::string>
TcpSocket::create(int sock_fd, sockaddr_in sin, socklen_t socklen)
{
    TcpEndpointDetails remote_tcp_details{
        .ip4 = funx::Ip4Addr{ntohl(sin.sin_addr.s_addr)},
        .port = funx::TcpPort{ntohs(sin.sin_port)},
    };
    TcpEndpointDetails local_tcp_details = getLocalTcpEndpoint(sock_fd);

    // set up nonblocking epoll for socket
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        int saved_errno = errno;
        ::close(sock_fd);
        return std::unexpected(std::format(
                "Could not create epoll for socket connected to {}. Errno={}",
                remote_tcp_details, saved_errno));
    }
    epoll_event sock_epoll_event;
    sock_epoll_event.events = EPOLLIN;
    sock_epoll_event.data.fd = sock_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &sock_epoll_event) < 0) {
        int saved_errno = errno;
        ::close(epoll_fd);
        ::close(sock_fd);
        return std::unexpected(std::format(
                "Could not set up epoll for socket connected to {}. Errno={}",
                remote_tcp_details, saved_errno));
    }

    LOG_INFO("Connected {} <=> {}", local_tcp_details, remote_tcp_details);
    return TcpSocket(
        sock_fd,
        sin,
        socklen,
        remote_tcp_details,
        local_tcp_details,
        epoll_fd,
        sock_epoll_event
    );
}

std::expected<TcpSocket, std::string>
TcpSocket::create(FdConfig const& c)
{
    return TcpSocket::create(c.sock_fd, c.sin, c.socklen);
}

std::expected<TcpSocket, std::string>
TcpSocket::create(Config const& c)
{
    return TcpSocket::create(FUNX_TRY(TcpSocket::configToFdConfig(c)));
}

TcpSocket::TcpSocket(
        int sock_fd,
        sockaddr_in sin,
        socklen_t socklen,
        TcpEndpointDetails remote_tcp_details,
        TcpEndpointDetails local_tcp_details,
        int epoll_fd,
        epoll_event sock_epoll_event
    )
    : sock_fd_(sock_fd)
    , sin_(sin)
    , socklen_(socklen)
    , remote_tcp_details_(remote_tcp_details)
    , local_tcp_details_(local_tcp_details)
    , epoll_fd_(epoll_fd)
    , sock_epoll_event_(sock_epoll_event)
{
}

TcpSocket::~TcpSocket()
{
    close();
}

TcpSocket::TcpSocket(TcpSocket&& other)
{
    moveFrom(std::move(other));
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other)
{
    close();
    moveFrom(std::move(other));
    return *this;
}

void TcpSocket::close()
{
    if (!closed_) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, sock_fd_, &sock_epoll_event_);
        ::close(sock_fd_);
        ::close(epoll_fd_);
        sock_fd_ = -1;
        epoll_fd_ = -1;
        closed_ = true;
    }
}

void TcpSocket::moveFrom(TcpSocket&& other)
{
    if (this == &other) {
        return;
    }
    sock_fd_ = std::exchange(other.sock_fd_, -1);
    sin_ = std::exchange(other.sin_, sockaddr_in{});
    socklen_ = std::exchange(other.socklen_, socklen_t{});
    remote_tcp_details_ = std::exchange(other.remote_tcp_details_, TcpEndpointDetails{});
    local_tcp_details_ = std::exchange(other.local_tcp_details_, TcpEndpointDetails{});
    for (size_t i = 0; i < MAX_EPOLL_EVENTS; ++i) {
        epoll_events_[i] = std::exchange(other.epoll_events_[i], epoll_event{});
    }
    epoll_fd_ = std::exchange(other.epoll_fd_, -1);
    sock_epoll_event_ = std::exchange(other.sock_epoll_event_, epoll_event{});
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        readBuffer_[i] = std::exchange(other.readBuffer_[i], std::byte{0});
    }
    closed_ = std::exchange(other.closed_, true);
}

void TcpSocket::send(funx::byte_view bytes)
{
    while (bytes.size() > 0) {
        int bytes_sent = ::send(sock_fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (bytes_sent < 0) {
            LOG_WARN("Failed to send to {}. Errno={}", remote_tcp_details_, errno);
            close();
            return;
        }
        bytes = bytes.subspan(bytes_sent);
    }
}

size_t TcpSocket::tryReadTo(std::byte* buffer) {
    int num_fds = epoll_wait(epoll_fd_, epoll_events_, MAX_EPOLL_EVENTS, 0);
    for (int i = 0; i < num_fds; ++i) {
        if (epoll_events_[i].data.fd == sock_fd_) {
            int bytesRead = read(sock_fd_, buffer, MTU);
            if (bytesRead <= 0) {
                LOG_INFO("Disconnected {} <=> {}. Closing socket.", local_tcp_details_, remote_tcp_details_);
                close();
                return 0;
            } else {
                return bytesRead;
            }
        }
    }
    return 0;
}

std::optional<funx::byte_view> TcpSocket::tryRead()
{
    size_t bytesRead = tryReadTo(readBuffer_);
    if (bytesRead > 0) {
        return funx::byte_view(readBuffer_, bytesRead);
    } else {
        return std::nullopt;
    }
}

} // namespace funx
