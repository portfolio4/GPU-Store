#ifndef FUNX_MATCH_SESSION_TRACKER_H
#define FUNX_MATCH_SESSION_TRACKER_H

#include <funx/net/types.h>

#include <map>
#include <optional>
#include <vector>

namespace funx::match {

class ListenSession;
class MatchingSession;

class SessionTracker
{
public:
    // Returns if the session was added successfully. This should always
    // succeed, because it's not possible for a client to connect to us
    // twice at the same time, using the same IP:port
    bool add(funx::TcpEndpointDetails tcp, MatchingSession& session);

    // Removes the session, or does nothing if the session was never added
    void remove(funx::TcpEndpointDetails);

    // Gets the MatchingSession connected to the client at the given IP:port.
    // If a successful result is returned, the MatchingSession pointer is
    // guaranteed to be valid. It's not a reference type because optional<>
    // does not allow references
    std::optional<MatchingSession*> get(funx::TcpEndpointDetails d);

    void addListenSession(ListenSession* s);

    std::vector<ListenSession*>& getListenSessions();

    void removeListenSession(ListenSession* s);

private:
    // We can't have that many sessions, so map should be fine
    std::map<funx::TcpEndpointDetails, MatchingSession*> tcp_to_session_;

    // Listener clients won't be frequently connecting/disconnecting, so
    // vector is fine to optimize for the common iteration case where we want to
    // send a copy of an event to all listen clients
    std::vector<ListenSession*> listen_sessions_;
};

} // namespace funx::match

#endif // FUNX_MATCH_SESSION_TRACKER_H

