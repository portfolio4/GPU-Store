#ifndef FUNX_NET_TCP_SOCKET_H
#define FUNX_NET_TCP_SOCKET_H

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include <funx/net/types.h>

namespace funx {

class TcpSocket
{
public:

    constexpr static size_t MTU = 1500;

    // Create a socket from a file descriptor that's already connected to remote
    struct FdConfig {
        int sock_fd;
        sockaddr_in sin;
        socklen_t socklen = sizeof(struct sockaddr);
    };
    static std::expected<TcpSocket, std::string> create(FdConfig const&);

    // Create socket from connecting to given IP port
    struct Config {
        std::string ip4;
        funx::TcpPort port;
    };
    static std::expected<TcpSocket, std::string> create(Config const&);

    // Create the TcpSocket or throw an exception. Be careful not to use this
    // in any critical server-side code - i.e. you do not want to throw
    // when accepting a new client while you are servicing other clients.
    // Okay to use this in clients where this could fail on startup.
    template <typename ConfigT>
    static TcpSocket createOrThrow(ConfigT const& c, int retries = 3) {
        std::string last_error;
        do {
            auto res = create(c);
            if (res) {
                return std::move(*res);
            }
            last_error = res.error();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            --retries;
        } while (retries >= 0);
        throw std::runtime_error(last_error);
    }

    ~TcpSocket();

    TcpSocket(TcpSocket const& other) = delete;
    TcpSocket& operator=(TcpSocket const& other) = delete;

    TcpSocket(TcpSocket&& other);
    TcpSocket& operator=(TcpSocket&& other);

    // Send bytes. Normally, this doesn't block.
    // But if the underlying buffers are full, this may block until
    // everything can get sent out
    // TODO this should return std::expected<void, errno> and callers
    // should check the result
    // TODO we should have a max send time, after which if we can't send,
    // we disconnect
    void send(funx::byte_view bytes);
    std::optional<funx::byte_view> tryRead();
    size_t tryReadTo(std::byte* buffer);

    TcpEndpointDetails getRemoteTcpEndpoint() const {
        return remote_tcp_details_;
    }

    TcpEndpointDetails getLocalTcpEndpoint() const {
        return local_tcp_details_;
    }

    // Closes any file descriptors (socket and epoll)
    void close();

    bool closed() const {
        return closed_;
    }

private:

    static std::expected<TcpSocket, std::string>
    create(int sock_fd, sockaddr_in sin, socklen_t socklen);

    // Rely on the factory functions to create a tcp socket for you
    TcpSocket(
        int sock_fd,
        sockaddr_in sin,
        socklen_t socklen,
        TcpEndpointDetails remote_tcp_details,
        TcpEndpointDetails local_tcp_details,
        int epoll_fd,
        epoll_event sock_epoll_event
    );

    static std::expected<FdConfig, std::string> configToFdConfig(Config const&);

    static TcpEndpointDetails getLocalTcpEndpoint(int sock_fd);

    void moveFrom(TcpSocket&& other);

private:

    int sock_fd_;
    sockaddr_in sin_;
    socklen_t socklen_;
    TcpEndpointDetails remote_tcp_details_;
    TcpEndpointDetails local_tcp_details_;

    // The only file descriptor we register is that of this socket, so
    // we should only ever get 1 event
    constexpr static size_t MAX_EPOLL_EVENTS = 1;
    epoll_event epoll_events_[MAX_EPOLL_EVENTS];
    int epoll_fd_;
    epoll_event sock_epoll_event_;

    static constexpr size_t BUFFER_SIZE = 2048;
    static_assert(MTU <= BUFFER_SIZE);
    std::byte readBuffer_[BUFFER_SIZE] = {static_cast<std::byte>(0)};

    bool closed_ = false;
};

} // namespace funx

#endif  // FUNX_NET_TCP_SOCKET_H

