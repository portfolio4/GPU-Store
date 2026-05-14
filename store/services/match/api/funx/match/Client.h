#ifndef API_FUNX_MATCH_CLIENT_H
#define API_FUNX_MATCH_CLIENT_H

#include <funx/log/Log.h>
#include <funx/net/TcpSession.h>
#include <funx/net/TcpSocket.h>
#include <funx/net/util.h>
#include <funx/match/config.h>
#include <funx/match/proto.h>

#include <expected>

namespace funx::match {

class Client
{
public:

    static constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(1);

    enum class Mode {
        Client,
        Listener,
    };

    struct Config {
        funx::match::MatchingServiceConfig matching_service_cfg;
        Mode mode;
    };

    Client(Config c)
        : config_(c)
        , session_(connectOrThrow())
    {}

    // Returns the request seqnum. Use it to poll for the response to it
    uint64_t addOrder(funx::match::Order const& order) {
        assert(config_.mode != Mode::Listener);
        funx::match::proto::AddOrderRequest req{
            .order = order
        };
        return send(req);
    }

    // Returns the request seqnum. Use it to poll for the response to it
    uint64_t cancelOrder(funx::OrderId order_id) {
        assert(config_.mode != Mode::Listener);
        return send(funx::match::proto::CancelOrderRequest{.order_id = order_id});
    }

    // Polls for messages from the matching service. (Nonblocking)
    // The Handler must handle all possible response by defining
    // operator()(Msg const& m)
    //
    // The possible events are
    //   * MatchingEvent
    //   * Reject
    //
    // If no message is received, then the handler is not invoked
    template <typename Handler>
    void poll(Handler&& handler) {
        if (!connected()) {
            if (!tryReconnect()) {
                return;
            }
        }
        using namespace funx::match::proto;
        std::optional<const Header*> maybe_hdr = session_.peek<Header>();
        if (maybe_hdr) {
            auto const& header = **maybe_hdr;
            if (header.magic != Header::MAGIC) {
                LOG_WARN("Disconnecting from server with wrong magic value: {}", header.magic);
                session_.close();
                return;
            }
            switch (header.msg_type)
            {
            case MsgType::AddOrderRequest:
                LOG_WARN("Response from matching service is an AddOrderRequest. Dropping it.");
                session_.consume(sizeof(MessageT<AddOrderRequest>));
                return;
            case MsgType::CancelOrderRequest:
                LOG_WARN("Response from matching service is a CancelOrderRequest. Dropping it.");
                session_.consume(sizeof(MessageT<CancelOrderRequest>));
                return;
            case MsgType::MatchingEvent: {
                std::optional<const MessageT<MatchingEvent>*>
                    maybe_matching_event = session_.peek<MessageT<MatchingEvent>>();
                if (maybe_matching_event) {
                    handler((*maybe_matching_event)->body);
                    session_.consume(sizeof(MessageT<MatchingEvent>));
                }
                return;
            }
            case MsgType::Reject: {
                std::optional<const MessageT<Reject>*> maybe_reject =
                    session_.peek<MessageT<Reject>>();
                if (maybe_reject) {
                    handler((*maybe_reject)->body);
                    session_.consume(sizeof(MessageT<Reject>));
                }
                return;
            }
            }
        }
    }

    bool connected() const {
        return !closed();
    }

    bool closed() const {
        return session_.closed();
    }

    void close() {
        session_.close();
    }

    Config const& config() const {
        return config_;
    }

private:

    funx::TcpPort serverPort() const {
        switch (config_.mode)
        {
        case Mode::Client:
            return config_.matching_service_cfg.server_port;
        case Mode::Listener:
            return config_.matching_service_cfg.listen_server_port;
        }
        assert(false);
        return config_.matching_service_cfg.server_port;
    }

    funx::TcpSession connectOrThrow() const {
        return funx::TcpSession(funx::TcpSession::Config{
            .socket = funx::TcpSocket::createOrThrow(funx::TcpSocket::Config{
                .ip4 = config_.matching_service_cfg.server_ip4,
                .port = serverPort(),
            })
        });
    }

    bool tryReconnect() {
        if (!session_.closed()) {
            return true;
        }
        auto now = funx::SteadyClock::now();
        if (now < next_reconnect_attempt_) {
            return false;
        }
        next_reconnect_attempt_ = now + RECONNECT_INTERVAL;
        auto socket = funx::TcpSocket::create(funx::TcpSocket::Config{
            .ip4 = config_.matching_service_cfg.server_ip4,
            .port = serverPort(),
        });
        if (!socket) {
            return false;
        }
        session_ = funx::TcpSession(funx::TcpSession::Config{.socket = std::move(*socket)});
        return true;
    }

    // Returns the seqnum of this request so that you can check for a response
    // to that seqnum
    template <typename T>
    uint64_t send(T const& body) {
        using funx::match::proto::MessageT;

        MessageT<T> msg{
            .body = body,
        };
        msg.hdr.seqnum = next_seqnum_++;
        if (!tryReconnect()) {
            return msg.hdr.seqnum;
        }
        session_.send(funx::util::to_bytes(msg));
        return msg.hdr.seqnum;
    }

private:
    const Config config_;
    funx::TcpSession session_;
    uint64_t next_seqnum_ = 1;
    funx::SteadyClock::time_point next_reconnect_attempt_{};
};

} // namespace funx::match

#endif  // API_FUNX_MATCH_CLIENT_H

