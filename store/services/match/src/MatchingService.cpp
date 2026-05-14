#include <funx/match/MatchingService.h>
#include <funx/match/Matcher.h>

namespace funx::match {

MatchingService::MatchingService(MatchingServiceConfig const& c)
    : max_clients_(c.max_clients)
    , txid_generator_(StandardTxidGenerator::Config{
            .matching_service_unique_id = c.unique_id
      })
    , matcher_(Matcher::Config{
            .products = c.products,
            .txid_generator = &txid_generator_,
      })
    , server_port_(c.server_port)
    , listen_server_port_(c.listen_server_port)
    , server_socket_(funx::TcpServerSocket::MultiConfig{
        .server_ports = {c.server_port, c.listen_server_port}
      })
    , transaction_db_client_(funx::transaction::Client::Config{
        .transaction_db_app_cfg = c.transaction_db_app_cfg,
        .response_timeout = std::chrono::seconds(1),
      })
{
}

void MatchingService::poll()
{
    // check if there are any new clients
    auto new_client = server_socket_.maybeAccept();
    if (new_client) {
        funx::TcpPort local_port = new_client->getLocalTcpEndpoint().port;
        if (local_port == server_port_) {
            if (clients_.size() + listen_clients_.size() < max_clients_) {
                MatchingSession& new_session =
                    clients_.emplace_back(MatchingSession::Config{
                        .client_conn = std::move(*new_client),
                        .session_tracker = session_tracker_,
                        .order_tracker = order_tracker_,
                        .matcher = matcher_,
                        .transaction_db_client = transaction_db_client_,
                    });
                [[maybe_unused]] auto res = session_tracker_.add(
                            new_session.getRemoteTcpEndpoint(), new_session);
                assert(res);
            } else {
                LOG_WARN("Dropping new connection. Already at max capacity. "
                        "max_clients={}", max_clients_);
            }
        } else if (local_port == listen_server_port_) {
            if (clients_.size() + listen_clients_.size() < max_clients_) {
                ListenSession& new_listen_session =
                    listen_clients_.emplace_back(ListenSession::Config{
                        .client_conn = std::move(*new_client)
                    });
                session_tracker_.addListenSession(&new_listen_session);
            } else {
                LOG_WARN("Dropping new connection. Already at max capacity. "
                        "max_clients={}", max_clients_);
            }
        } else {
            // should never happen
            LOG_ERR("Server socket accepted a connection on incorrect port {}. Dropping it", local_port);
            assert(false);
        }
    }

    // service all client connections
    for (auto it = clients_.begin(); it != clients_.end();) {
        auto& client = *it;
        client.poll();
        if (client.closed()) {
            client.cancelAllOrders();
            session_tracker_.remove(client.getRemoteTcpEndpoint());
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = listen_clients_.begin(); it != listen_clients_.end();) {
        auto& listen_client = *it;
        if (listen_client.closed()) {
            session_tracker_.removeListenSession(&listen_client);
            it = listen_clients_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace funx::match

