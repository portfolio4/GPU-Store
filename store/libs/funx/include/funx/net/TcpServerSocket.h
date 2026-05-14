#ifndef FUNX_NET_SERVER_SOCKET_H
#define FUNX_NET_SERVER_SOCKET_H

#include <sys/epoll.h>
#include <sys/socket.h>
#include <map>
#include <queue>
#include <set>

#include <funx/log/Log.h>
#include <funx/net/TcpSocket.h>

namespace funx
{

class TcpServerSocket
{
public:

    struct Config
    {
        funx::TcpPort server_port;
    };

    // For servers that need to listen on multiple ports
    struct MultiConfig
    {
        std::vector<funx::TcpPort> server_ports;
    };

    TcpServerSocket(Config const& c);
    TcpServerSocket(MultiConfig const& c);

    ~TcpServerSocket();

    std::optional<TcpSocket> maybeAccept();

private:

    struct PortData {
        int server_sock_fd;
        epoll_event server_sock_epoll_event;
    };

private:

    std::map<funx::TcpPort, PortData> port_data_;
    std::set<int> server_sock_fds_;

    constexpr static size_t MAX_EPOLL_EVENTS = 64;
    epoll_event epoll_events_[MAX_EPOLL_EVENTS];

    int epoll_fd_;
    epoll_event server_sock_epoll_event_;

    std::queue<TcpSocket::FdConfig> accepted_clients_;
};

} // namespace funx

#endif  // FUNX_NET_SERVER_SOCKET_H

