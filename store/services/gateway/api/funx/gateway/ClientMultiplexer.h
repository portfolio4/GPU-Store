#ifndef FUNX_GATEWAY_CLIENT_MULTIPLEXER_H
#define FUNX_GATEWAY_CLIENT_MULTIPLEXER_H

#include <funx/gateway/Client.h>
#include <funx/util/overload.h>

#include <list>

namespace funx::gateway {

class ClientMultiplexer
{
public:
    struct Config {
        std::vector<Client::Config> clients;
    };

    ClientMultiplexer(Config const& c)
        : config_(c)
    {
        for (Client::Config const& client_cfg : c.clients) {
            clients_.emplace_back(client_cfg);
        }

        if (clients_.size() == 0) {
            throw std::invalid_argument("ClientMultiplexer requires at least 1 client");
        }

        for (Client& client : clients_) {
            client.sendLogin();
            auto login_result = client.pollFor(funx::overloaded{
                [&](flop::MessageT<flop::LoginResponse> const& resp) {
                    if (resp.body.reason != flop::Reason::NoError) {
                        throw std::runtime_error(std::format(
                            "Failed to login to session {}. ErrorCode={}",
                            client.config().session_cfg.session_name,
                            static_cast<int>(resp.body.reason)));
                    }
                },
                [&](auto&&) {
                    throw std::runtime_error(std::format(
                        "Didn't get a login response after sending login to "
                        "session {}", client.config().session_cfg.session_name));
                }
            }, std::chrono::seconds(1));

            if (!login_result) {
                throw std::runtime_error(std::format(
                    "Failed while waiting for login response from session {}: {}",
                    client.config().session_cfg.session_name,
                    login_result.error().str()));
            }
            if (!*login_result) {
                throw std::runtime_error(std::format(
                    "Timed out waiting for login response from session {}",
                    client.config().session_cfg.session_name));
            }
        }
        next_client_ = clients_.begin();
    }

    Client& next() {
        Client& rtn = *next_client_;
        next_client_++;
        if (next_client_ == clients_.end()) {
            next_client_ = clients_.begin();
        }
        return rtn;
    }

    Config const& config() {
        return config_;
    }

    uint64_t nextClientOrderId() {
        return next_client_order_id_++;
    }

private:

    const Config config_;

    std::list<Client> clients_;
    std::list<Client>::iterator next_client_;

    uint64_t next_client_order_id_ = 1;
};

} // namespace funx::gateway

#endif  // FUNX_GATEWAY_CLIENT_MULTIPLEXER_H
