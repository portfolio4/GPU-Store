#include <funx/match/SessionTracker.h>

namespace funx::match {

bool SessionTracker::add(funx::TcpEndpointDetails tcp, MatchingSession& session)
{
    auto [_, inserted] = tcp_to_session_.try_emplace(tcp, &session);
    return inserted;
}

void SessionTracker::remove(funx::TcpEndpointDetails tcp)
{
    auto it = tcp_to_session_.find(tcp);
    if (it != tcp_to_session_.end()) {
        tcp_to_session_.erase(it);
    }
}

std::optional<MatchingSession*> SessionTracker::get(funx::TcpEndpointDetails d)
{
    auto it = tcp_to_session_.find(d);
    if (it != tcp_to_session_.end()) {
        return it->second;
    } else {
        return std::nullopt;
    }
}

void SessionTracker::addListenSession(ListenSession* s)
{
    listen_sessions_.push_back(s);
}

std::vector<ListenSession*>& SessionTracker::getListenSessions()
{
    return listen_sessions_;
}

void SessionTracker::removeListenSession(ListenSession* s)
{
    auto it = std::find(listen_sessions_.begin(), listen_sessions_.end(), s);
    if (it != listen_sessions_.end()) {
        listen_sessions_.erase(it);
    }
}

} // namespace funx::match

