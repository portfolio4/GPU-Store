#ifndef API_FUNX_GATEWAY_CLIENT_H
#define API_FUNX_GATEWAY_CLIENT_H

#include <flop/flop.h>
#include <flop/util.h>
#include <funx/gateway/config.h>
#include <funx/log/Log.h>
#include <funx/net/TcpSession.h>
#include <funx/net/util.h>

namespace funx::gateway {

class Client
{
public:

    static constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(1);

    struct Config {
        // Client will connect to a specific session on the gateway
        funx::gateway::GatewayConfig gateway_cfg;
        funx::gateway::SessionConfig session_cfg;
    };

    Client(Config const& c)
        : config_(c)
        , session_(connectOrThrow())
        , heartbeat_interval_(config_.session_cfg.heartbeat_timeout_seconds.transform([](uint32_t seconds) {
            // We want to send at 3x the frequency of the heartbeat timeout
            // interval to be safe
            return std::chrono::milliseconds(seconds * 1000 / 3);
          }))
    {
        if (c.session_cfg.session_name.size() > sizeof(flop::Login::session)) {
            throw std::runtime_error(std::format(
                "Session name '{}' will not fit in the Login struct which only "
                "has room for {} characters",
                c.session_cfg.session_name, sizeof(flop::Login::session)));
        }
        if (c.session_cfg.password.size() > sizeof(flop::Login::password)) {
            throw std::runtime_error(std::format(
                "Password '{}' will not fit in the Login struct which only "
                "has room for {} characters",
                c.session_cfg.password, sizeof(flop::Login::password)));
        }
    }

    ~Client() {
        close();
    }

    Client(Client const& other) = delete;
    Client& operator=(Client const& other) = delete;

    Client(Client&& other)
        : config_(other.config_)
        , session_(std::move(other.session_))
        , heartbeat_interval_(other.heartbeat_interval_)
        , next_heartbeat_time_(other.next_heartbeat_time_)
        , next_reconnect_attempt_(other.next_reconnect_attempt_)
        , reconnect_login_pending_(other.reconnect_login_pending_)
    {
    }

    Client& operator=(Client&& other)
    {
        config_ = other.config_;
        session_.close();
        session_ = std::move(other.session_);
        heartbeat_interval_ = other.heartbeat_interval_;
        next_heartbeat_time_ = other.next_heartbeat_time_;
        next_reconnect_attempt_ = other.next_reconnect_attempt_;
        reconnect_login_pending_ = other.reconnect_login_pending_;
        return *this;
    }

    void sendLogin() {
        if (session_.closed()) {
            (void) tryReconnect();
            return;
        }
        sendLoginMessage();
    }

private:
    void sendLoginMessage() {
        flop::Login login;

        // already validated the sizes are okay in the constructor
        memset(login.session, 0, sizeof(login.session));
        memcpy(login.session, config_.session_cfg.session_name.data(),
                config_.session_cfg.session_name.size());
        memset(login.password, 0, sizeof(login.password));
        memcpy(login.password, config_.session_cfg.password.data(),
                config_.session_cfg.password.size());

        session_.send(funx::util::to_bytes(flop::MessageT<flop::Login>{
            .body = login
        }));
    }

public:
    // Send an order without add-ons
    void sendBasicNewOrder(flop::NewOrder const& order) {
        sendBytes(funx::util::to_bytes(flop::MessageT<flop::NewOrder>{
            .body = order
        }));
    };

    void sendCancelOrder(flop::CancelOrder const& cancel) {
        sendBytes(funx::util::to_bytes(flop::MessageT<flop::CancelOrder>{
            .body = cancel
        }));
    }

    void sendHeartbeat() {
        sendBytes(funx::util::to_bytes(flop::MessageT<flop::Heartbeat>{}));
    }

    void sendLogout() {
        if (!connected()) {
            if (!tryReconnect()) {
                return;
            }
        }
        session_.send(funx::util::to_bytes(flop::MessageT<flop::Logout>{
            .body = flop::Logout{},
        }));
    }

    // Nonblocking poll that reads at most one message.
    //
    // If there is no message, we return std::expected(false)
    // If there is a message, we return std::expected(true) and handler is invoked
    // If there is a problem with the message, then std::unexpected() is returned
    // with the error message.
    //
    // Handler must define operator()(MessageT<Msg> const&) for every Msg
    // defined in the FLOP protocol
    template <typename Handler>
    std::expected<bool, flop::FlopError>
    poll(Handler&& handler) {
        if (!connected()) {
            if (!tryReconnect()) {
                return false;
            }
        }
        if (heartbeat_interval_ && funx::Clock::now() >= next_heartbeat_time_) {
            sendHeartbeat();
        }
        using msg_size_t = decltype(flop::Header::msg_size);
        std::optional<msg_size_t> maybe_msg_size = session_.peekCopy<msg_size_t>();
        if (!maybe_msg_size) {
            // full message hasn't arrived yet
            return false;
        }
        msg_size_t msg_size = *maybe_msg_size;

        if (msg_size > funx::TcpSession::MTU - sizeof(msg_size)) {
            constexpr const char* err_msg = "Disconnecting from server sending msg_size larger than MTU";
            LOG_WARN(err_msg);
            session_.close();
            return std::unexpected(flop::FlopError(err_msg));
        }

        auto maybe_bytes = session_.peek(msg_size + sizeof(msg_size));
        if (maybe_bytes) {
            if (reconnect_login_pending_) {
                bool login_accepted = false;
                auto rtn = flop::handleMsg(*maybe_bytes, [&](auto const& msg) {
                    using Message = std::remove_cvref_t<decltype(msg)>;
                    if constexpr (std::same_as<Message, flop::MessageT<flop::LoginResponse>>) {
                        login_accepted = msg.body.reason == flop::Reason::NoError;
                    }
                });
                session_.consume(msg_size + sizeof(msg_size));
                if (!rtn) {
                    session_.close();
                    return std::unexpected(rtn.error());
                }
                if (!login_accepted) {
                    constexpr const char* err_msg = "Gateway rejected login after reconnect";
                    session_.close();
                    return std::unexpected(flop::FlopError(err_msg));
                }
                reconnect_login_pending_ = false;
                return false;
            }
            auto rtn = flop::handleMsg(*maybe_bytes, std::forward<Handler>(handler));
            session_.consume(msg_size + sizeof(msg_size));
            if (rtn) {
                return true;
            } else {
                return std::unexpected(rtn.error());
            }
        } else {
            // full message hasn't arrived yet
            return false;
        }
    }

    // Polls for up to one message for up to the given duration.
    //
    // If no message ever arrives, we return std::expected(false)
    // If a message arrives and handler is invoked, we return std::expected(true).
    // If there is an error, we will stop immediately and return std::unexpected
    // with the error message.
    template <typename Handler>
    std::expected<bool, flop::FlopError>
    pollFor(Handler&& handler, funx::Clock::duration duration) {
        funx::Clock::time_point stop = funx::Clock::now() + duration;
        while (funx::Clock::now() < stop) {
            auto result = poll(handler);
            if (result) {
                if (*result == true) {
                    return true;
                } else {
                    // no message was received yet
                }
            } else {
                return std::unexpected(result.error());
            }
        }
        return false;
    }

    void close() {
        session_.close();
    }

    bool connected() const {
        return !closed();
    }

    bool closed() const {
        return session_.closed();
    }

    const Config& config() const {
        return config_;
    }

private:

    funx::TcpSession connectOrThrow() const {
        return funx::TcpSession(funx::TcpSession::Config{
            .socket = funx::TcpSocket::createOrThrow(funx::TcpSocket::Config{
                .ip4 = config_.gateway_cfg.server_ip4,
                .port = config_.session_cfg.port,
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
            .ip4 = config_.gateway_cfg.server_ip4,
            .port = config_.session_cfg.port,
        });
        if (!socket) {
            return false;
        }
        session_ = funx::TcpSession(funx::TcpSession::Config{.socket = std::move(*socket)});
        reconnect_login_pending_ = true;
        sendLoginMessage();
        return true;
    }

    void sendBytes(funx::byte_view bytes) {
        if (!connected()) {
            if (!tryReconnect()) {
                return;
            }
        }
        if (heartbeat_interval_) {
            next_heartbeat_time_ = funx::Clock::now() + *heartbeat_interval_;
        }
        session_.send(bytes);
    }

private:

    Config config_;
    funx::TcpSession session_;

    std::optional<funx::Clock::duration> heartbeat_interval_;
    funx::Clock::time_point next_heartbeat_time_;
    funx::SteadyClock::time_point next_reconnect_attempt_{};
    bool reconnect_login_pending_ = false;
};

} // namespace funx::gateway

#endif  // API_FUNX_GATEWAY_CLIENT_H

