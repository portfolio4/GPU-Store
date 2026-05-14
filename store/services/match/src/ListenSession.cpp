#include <funx/match/ListenSession.h>
#include <funx/net/util.h>

namespace funx::match {

ListenSession::ListenSession(Config c)
    : client_conn_(funx::TcpSession::Config{.socket = std::move(c.client_conn)})
{
}

void ListenSession::sendEvent(
        int64_t event_timestamp,
        funx::match::Event const& e)
{
    using namespace funx::match::proto;
    MessageT<MatchingEvent> msg{
        .body = {
            .req_seqnum = 0,
            .match_timestamp = event_timestamp,
            .event = SerializedEvent{e},
        }
    };
    client_conn_.send(funx::util::to_bytes(msg));
}

} // namespace funx::match

