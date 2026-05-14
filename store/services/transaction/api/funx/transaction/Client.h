#ifndef API_FUNX_TRANSACTION_CLIENT_H
#define API_FUNX_TRANSACTION_CLIENT_H

#include <funx/clock/Clock.h>
#include <funx/log/Log.h>
#include <funx/net/TcpSession.h>
#include <funx/net/TcpSocket.h>
#include <funx/net/util.h>
#include <funx/transaction/config.h>
#include <funx/transaction/proto.h>
#include <funx/util/overload.h>

#include <chrono>
#include <expected>

namespace funx::transaction {

class Client
{
public:

    static constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(1);
    struct Config {
        funx::transaction::TransactionDbAppConfig transaction_db_app_cfg;
        std::chrono::milliseconds response_timeout = std::chrono::milliseconds(1);
    };

    Client(Config const& c)
        : config_(c)
        , session_(connectOrThrow())
        , response_timeout_(c.response_timeout)
    {}

    // Save transaction to database. Blocks up to response timeout configuration
    std::expected<void, funx::transaction::proto::ErrorCode>
        save(funx::transaction::Transaction const& tx)
    {
        if (!tryReconnect()) {
            return std::unexpected(funx::transaction::proto::ErrorCode::ReceiveTimeout);
        }
        uint64_t req_seqnum = send(
            funx::transaction::proto::SaveTransaction{
                .tx = tx
            }
        );
        return waitAckOrError(req_seqnum);
    }

    // Query for a given transaction. Blocks up to response timeout configuration
    std::expected<funx::transaction::Transaction, funx::transaction::proto::ErrorCode>
        query(funx::TransactionId txid)
    {
        if (!tryReconnect()) {
            return std::unexpected(funx::transaction::proto::ErrorCode::ReceiveTimeout);
        }
        uint64_t req_seqnum = send(
            funx::transaction::proto::QueryTransaction{
                .txid = txid
            }
        );
        return waitTransactionResponseOrError(req_seqnum);
    }

private:

    funx::TcpSession connectOrThrow() const {
        return funx::TcpSession(funx::TcpSession::Config{
            .socket = funx::TcpSocket::createOrThrow(funx::TcpSocket::Config{
                .ip4 = config_.transaction_db_app_cfg.server_ip4,
                .port = config_.transaction_db_app_cfg.server_port,
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
            .ip4 = config_.transaction_db_app_cfg.server_ip4,
            .port = config_.transaction_db_app_cfg.server_port,
        });
        if (!socket) {
            return false;
        }
        session_ = funx::TcpSession(funx::TcpSession::Config{.socket = std::move(*socket)});
        return true;
    }

    // Returns the seqnum of this request, so that you can check for a response
    // to that seqnum
    template <typename T>
    uint64_t send(T const& body) {
        using funx::transaction::proto::MessageT;

        MessageT<T> msg{
            .body = body,
        };
        msg.hdr.seqnum = next_seqnum_++;
        session_.send(funx::util::to_bytes(msg));
        return msg.hdr.seqnum;
    }

    // Processes up to one response from the server.
    // The Handler needs to provide operator(T const&) for all
    // message body types (Ack, Error, etc.) defined in the protocol.
    // If the next response is invalid, then the handler is not invoked and an
    // error will be logged
    template <typename Handler>
    void handleNextResponse(
            uint64_t req_seqnum,
            funx::Clock::time_point timeout,
            Handler&& handler) {
        using namespace funx::transaction::proto;

        // Waits up until `timeout` for the bytes of Msg to arrive.
        //   * If Msg arrives, the handler is invoked on it, the bytes in the
        //     TCP session are consumed, and the function returns true
        //   * If Msg does not arrive, the handler is not invoked, no bytes
        //     are consumed and the function returns false
        auto waitForMsgAndInvokeHandler = [&]<typename Msg>() {
            auto maybe_msg = session_.waitUntil<MessageT<Msg>>(timeout);
            if (maybe_msg) {
                Msg const& body = (*maybe_msg)->body;
                if (body.req_seqnum == req_seqnum) {
                    handler(body);
                } else {
                    LOG_WARN("Expecting server to respond to seqnum {} but "
                            "got a response for {}. Ignoring it.",
                            req_seqnum, body.req_seqnum);
                }
                session_.consume(sizeof(MessageT<Msg>));
            }
        };

        do {
            std::optional<const Header*> maybe_hdr = session_.peek<Header>();
            if (maybe_hdr) {
                auto const& header = **maybe_hdr;
                if (header.magic != Header::MAGIC) {
                    LOG_WARN("Disconnecting from server sending wrong magic value: {}", header.magic);
                    session_.close();
                    continue;
                }
                switch(header.msg_type)
                {
                case MsgType::SaveTransaction:
                    LOG_WARN("Response from server is a SaveTransaction request. Dropping it.");
                    session_.consume(sizeof(MessageT<SaveTransaction>));
                    return;
                case MsgType::QueryTransaction:
                    LOG_WARN("Response from server is a QueryTransaction request. Dropping it.");
                    session_.consume(sizeof(MessageT<QueryTransaction>));
                    return;
                case MsgType::TransactionResponse:
                    waitForMsgAndInvokeHandler.template operator()<TransactionResponse>();
                    return;
                case MsgType::Ack:
                    waitForMsgAndInvokeHandler.template operator()<Ack>();
                    return;
                case MsgType::Error:
                    waitForMsgAndInvokeHandler.template operator()<Error>();
                    return;
                }
                return;
            }
        } while (Clock::now() < timeout);
    };

    std::expected<void, funx::transaction::proto::ErrorCode>
        waitAckOrError(uint64_t req_seqnum)
    {
        using namespace funx::transaction::proto;

        auto timeout = funx::Clock::now() + response_timeout_;
        std::expected<void, ErrorCode> rtn = std::unexpected(ErrorCode::ReceiveTimeout);

        bool got_valid_response = false;
        do {
            handleNextResponse(req_seqnum, timeout, funx::overloaded{
                [&](Ack const& ack) {
                    if (ack.req_seqnum == req_seqnum) {
                        got_valid_response = true;
                        rtn = {};
                    } else {
                        LOG_ERR("Sent request seqnum {} but got response for seqnum {}",
                            req_seqnum, ack.req_seqnum);
                    }
                },
                [&](Error const& err) {
                    if (err.req_seqnum == req_seqnum) {
                        got_valid_response = true;
                        rtn = std::unexpected(err.error);
                    } else {
                        LOG_ERR("Sent request seqnum {} but got response for seqnum {}",
                            req_seqnum, err.req_seqnum);
                    }
                },
                [&](auto&&) {
                    LOG_ERR("Got an invalid response from server for request seqnum {}", req_seqnum);
                }
            });
        } while (!got_valid_response && Clock::now() < timeout);

        return rtn;
    }

    std::expected<funx::transaction::Transaction, funx::transaction::proto::ErrorCode>
        waitTransactionResponseOrError(uint64_t req_seqnum)
    {
        using namespace funx::transaction::proto;

        auto timeout = funx::Clock::now() + response_timeout_;
        std::expected<funx::transaction::Transaction, ErrorCode> rtn =
            std::unexpected(ErrorCode::ReceiveTimeout);

        bool got_valid_response = false;
        do {
            handleNextResponse(req_seqnum, timeout, funx::overloaded{
                [&](TransactionResponse const& resp) {
                    if (resp.req_seqnum == req_seqnum) {
                        got_valid_response = true;
                        rtn = resp.tx;
                    } else {
                        LOG_ERR("Sent request seqnum {} but got response for seqnum {}",
                            req_seqnum, resp.req_seqnum);
                    }
                },
                [&](Error const& err) {
                    if (err.req_seqnum == req_seqnum) {
                        got_valid_response = true;
                        rtn = std::unexpected(err.error);
                    } else {
                        LOG_ERR("Sent request seqnum {} but got response for seqnum {}",
                            req_seqnum, err.req_seqnum);
                    }
                },
                [&](auto&&) {
                    LOG_ERR("Got an invalid response from server for request seqnum {}", req_seqnum);
                }
            });
        } while (!got_valid_response && Clock::now() < timeout);

        return rtn;
    }

private:

    const Config config_;
    funx::TcpSession session_;
    const std::chrono::milliseconds response_timeout_;

    uint64_t next_seqnum_ = 1;
    funx::SteadyClock::time_point next_reconnect_attempt_{};
};

} // namespace funx::transaction

#endif // API_FUNX_TRANSACTION_CLIENT_H

