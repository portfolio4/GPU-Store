#ifndef FUNX_MATCH_MATCHING_SESSION_H
#define FUNX_MATCH_MATCHING_SESSION_H

#include <funx/match/Matcher.h>
#include <funx/match/OrderTracker.h>
#include <funx/match/proto.h>
#include <funx/match/SessionTracker.h>
#include <funx/net/TcpSession.h>
#include <funx/net/TcpSocket.h>
#include <funx/transaction/Client.h>

#include <unordered_set>

namespace funx::match {

class MatchingSession
{
public:
    struct Config
    {
        funx::TcpSocket&& client_conn;
        funx::match::SessionTracker& session_tracker;
        OrderTracker& order_tracker;
        Matcher& matcher;
        funx::transaction::Client& transaction_db_client;
    };

    MatchingSession(Config c);

    void poll();

    void cancelAllOrders();

    bool closed() const {
        return client_conn_.closed();
    }

    funx::TcpEndpointDetails getRemoteTcpEndpoint() const {
        return client_conn_.getRemoteTcpEndpoint();
    }

private:

    void pollClient();

    void handleAddOrderRequest(
            funx::match::proto::Header const& hdr,
            funx::match::proto::AddOrderRequest const& add);

    void handleCancelOrderRequest(
            funx::match::proto::Header const& hdr,
            funx::match::proto::CancelOrderRequest const& cancel);

    void respondReject(
            funx::match::proto::Header const& request_hdr,
            funx::match::proto::ErrorCode error);

    // send the given event back to the client
    void respondEvent(
            funx::match::proto::Header const& request_hdr,
            int64_t event_timestamp,
            funx::match::Event const& e);

    // When an something external triggers an event - namely you send a
    // passive order and someone else trades against you - this function
    // notifies the client.
    //
    // In the future, this can be used to issue things like stop loss, self
    // trade protection, etc.
    void sendTriggeredEvent(
            int64_t event_timestamp,
            funx::match::Event const& e);

    MatchingSession* getSessionForOrderId(funx::OrderId id) const;

private:
    funx::TcpSession client_conn_;
    SessionTracker& session_tracker_;
    OrderTracker& order_tracker_;
    Matcher& matcher_;
    funx::transaction::Client& transaction_db_client_;
    std::unordered_set<funx::OrderId> live_order_ids_;
};

} // namespace funx::match

#endif  // FUNX_MATCH_MATCHING_SESSION_H

