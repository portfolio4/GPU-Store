#ifndef FUNX_MATCH_LISTEN_SESSION_H
#define FUNX_MATCH_LISTEN_SESSION_H

#include <funx/match/proto.h>
#include <funx/net/TcpSession.h>
#include <funx/net/TcpSocket.h>

namespace funx::match {

// Listen sessions receive a copy of all matching events from all clients.
// But they cannot send anything to the matching service
class ListenSession
{
public:
    struct Config
    {
        funx::TcpSocket&& client_conn;
    };

    ListenSession(Config c);

    void sendEvent(int64_t event_timestamp, funx::match::Event const& e);

    bool closed() const {
        return client_conn_.closed();
    }

private:
    funx::TcpSession client_conn_;
};

} // namespace funx::match

#endif // FUNX_MATCH_LISTEN_SESSION_H

