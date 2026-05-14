#include <funx/net/TcpServerSocket.h>

#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>

namespace funx {
TcpServerSocket::TcpServerSocket(Config const& c)
    : TcpServerSocket(TcpServerSocket::MultiConfig{
        .server_ports = {c.server_port}
      })
{}

TcpServerSocket::TcpServerSocket(MultiConfig const& c)
{
    // set up nonblocking epoll for server socket ports
    epoll_fd_ = epoll_create1(0);

    for (funx::TcpPort server_port : c.server_ports) {
        LOG_INFO("Initializing server socket on port {}", server_port);

        // set up server socket
        socklen_t socklen = sizeof(struct sockaddr);
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(sockaddr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        int server_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock_fd < 0) {
            throw std::runtime_error(std::format(
                    "Could not create server socket. Errno={}", errno));
        }
        int optval = 1;
        if (setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
            throw std::runtime_error(std::format(
                    "Could not setsockopt. Errno={}", errno));
        }
        if (bind(server_sock_fd, (struct sockaddr*)&server_addr, socklen) < 0) {
            throw std::runtime_error(std::format(
                    "Could not bind server socket. Errno={}", errno));
        }
        if (listen(server_sock_fd, 5) < 0) {
            throw std::runtime_error(std::format(
                    "Could not listen on server socket. Errno={}", errno));
        }

        epoll_event server_sock_epoll_event;
        server_sock_epoll_event.events = EPOLLIN;
        server_sock_epoll_event.data.fd = server_sock_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_sock_fd, &server_sock_epoll_event) < 0) {
            throw std::runtime_error(std::format(
                    "Could not set up epoll for server socket. Errno={}", errno));
        }

        port_data_[server_port] = PortData{
            .server_sock_fd = server_sock_fd,
            .server_sock_epoll_event = server_sock_epoll_event,
        };
        server_sock_fds_.insert(server_sock_fd);
    }
}

TcpServerSocket::~TcpServerSocket() {
    for (auto [_, data] : port_data_) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, data.server_sock_fd, &data.server_sock_epoll_event);
        close(data.server_sock_fd);
    }
}

std::optional<TcpSocket> TcpServerSocket::maybeAccept()
{
    int num_fds = epoll_wait(epoll_fd_, epoll_events_, MAX_EPOLL_EVENTS, 0);
    for (int i = 0; i < num_fds; ++i) {
        if (server_sock_fds_.contains(epoll_events_[i].data.fd)) {
            TcpSocket::FdConfig client_cfg;
            client_cfg.sock_fd = accept(epoll_events_[i].data.fd,
                    (sockaddr*)&client_cfg.sin, &client_cfg.socklen);
            if (client_cfg.sock_fd == -1) {
                LOG_WARN("Error accepting client. Errono={}", errno);
                continue;
            }

            accepted_clients_.push(client_cfg);
        }
    }

    if (!accepted_clients_.empty()) {
        auto cfg = accepted_clients_.front();
        accepted_clients_.pop();
        auto res = TcpSocket::create(cfg);
        if (!res) {
            LOG_WARN("Failed to create socket for client: {}", res.error());
            return std::nullopt;
        }
        return std::move(*res);
    } else {
        return std::nullopt;
    }
}

} // namespace funx

