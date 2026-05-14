#ifndef FUNX_GATEWAY_SESSION_H
#define FUNX_GATEWAY_SESSION_H

#include <flop/flop.h>
#include <flop/util.h>
#include <funx/clock/Clock.h>
#include <funx/common/types.h>
#include <funx/gateway/config.h>
#include <funx/gateway/MatchingServiceInterface.h>
#include <funx/gateway/OrderTracker.h>
#include <funx/match/proto.h>
#include <funx/net/TcpSocket.h>
#include <funx/net/TcpSession.h>

#include <variant>

namespace funx::gateway {

class Session
{
public:

    // No TCP connection established
    struct NotConnected {};

    // TCP connection established, but not logged in yet
    struct Connected {
        funx::TcpSession session;
    };

    // Logged in and trading is permitted
    struct Active {
        funx::TcpSession session;
        funx::Clock::time_point last_msg_time;
    };

    using State = std::variant<
        NotConnected,
        Connected,
        Active
    >;

    Session(SessionConfig const& c,
            OrderTracker& order_tracker,
            MatchingServiceInterface& matching_service_intf);

    void handleNewClient(funx::TcpSocket&& client);

    void poll(funx::Clock::time_point now = funx::Clock::now());

    funx::TcpPort getLocalPort() const;

    void handleOrderAccepted(
            funx::match::proto::MatchingEvent const& me,
            funx::match::AddOrder const& add);

    // Since a match has both a buyer and seller, the side_to_process
    // instructs which side of the match to send a message to the client
    void handleMatch(
            funx::match::proto::MatchingEvent const& me,
            funx::match::Match const& match,
            funx::Side side_to_process);

    void handleOrderRemoved(
            funx::match::proto::MatchingEvent const& me,
            funx::match::RemoveOrder const& remove);

    void handleIocMiss(
            funx::match::proto::MatchingEvent const& me,
            funx::match::IocMiss const& ioc_miss);

private:

    void close();
    void checkClientDisconnected();
    void checkHeartbeatTimeout(Session::Active& state, funx::Clock::time_point now);
    void cancelAllOrders();

    std::optional<funx::byte_view> peekOneMsgNoValidation(funx::TcpSession& session);

    void pollConnectedState(Session::Connected& state, funx::Clock::time_point now);

    void pollActiveState(Session::Active& state, funx::Clock::time_point now);

    void handleNewOrder(Session::Active& state, flop::NewOrder const& order);
    void handleCancelOrder(Session::Active& state, flop::CancelOrder const& cancel);

    void sendLoginResponse(Session::Connected& state, flop::LoginResponse const& resp);
    void sendOrderAccepted(Session::Active& state, flop::OrderAccepted const& accept);
    void sendOrderReject(Session::Active& state, flop::OrderReject const& reject);
    void sendOrderRemoved(Session::Active& state, flop::OrderRemoved const& remove);
    void sendCancelReject(Session::Active& state, flop::CancelReject const& reject);
    void sendTrade(Session::Active& state, flop::Trade const& trade);

    template <typename Msg>
    flop::MessageT<Msg> makeUnsequencedResponse(Msg const& body) {
        return flop::MessageT<Msg> {
            .body = body,
        };
    }

    template <typename Msg>
    flop::MessageT<Msg> makeSequencedResponse(Msg const& body) {
        flop::MessageT<Msg> msg{
            .body = body,
        };
        msg.header.seqnum = next_seqnum_++;
        return msg;
    }


private:

    enum class OrderAckStatus {
        // Haven't sent an accept for this order yet
        HaventSentAck,

        // The matching service sent an accept, which we forwarded on
        // to the client
        SentAck,

        // The matching service immediately sent trades for this order,
        // so we sent a synthetic accept. We will also have to send a
        // synthetic remove later on if the quantity becomes 0
        SentSynthesizedAck,

        SentSynthesizedRemove,
    };

    struct ClientOrderDetails {
        uint64_t client_order_id;
        uint64_t product;
        flop::Side side;
        int64_t original_price;
        int64_t remaining_qty;
        flop::Duration duration;
        OrderAckStatus ack_status;
    };

    // Note: do not save the pointer. It is only stable up until the next
    // insert to the eoid_to_client_ map
    std::optional<ClientOrderDetails*> getClientOrderDetails(funx::OrderId order_id);

private:

    SessionConfig config_;
    std::optional<funx::Clock::duration> heartbeat_timeout_duration_;
    OrderTracker& order_tracker_;
    MatchingServiceInterface& matching_service_intf_;
    State state_ = NotConnected{};

    // TODO (non-MVP) recovery in case gateway crashes
    //
    // We are going to defer this because this is not critical for
    // MVP. It's straightforward to save the bytes of every sequenced
    // Message<T> to a append-only log file and read it on a restart.
    //
    // If you need to be safe from disk failure, then add a background process
    // periodically copying new writes to AWS S3 style storage. And
    // every week, move logs that are more than 1 week old to cold storage.
    uint32_t next_seqnum_ = 1;

    std::unordered_map<uint64_t, funx::OrderId> cloid_to_eoid_;
    std::unordered_map<funx::OrderId, ClientOrderDetails> eoid_to_client_;
};

} // namespace funx::gateway

#endif // FUNX_GATEWAY_SESSION_H

