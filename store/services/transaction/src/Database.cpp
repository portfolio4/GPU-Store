#include <funx/transaction/Database.h>
#include <funx/net/util.h>

namespace funx::transaction {

Database::Database(Config const& c)
    : max_clients_(c.max_clients)
    , storage_(c.storage)
    , server_socket_(funx::TcpServerSocket::Config{
        .server_port = c.port
      })
{
}

void Database::poll()
{
    // check if there are any new clients
    auto new_client = server_socket_.maybeAccept();
    if (new_client) {
        if (clients_.size() < max_clients_) {
            clients_.emplace_back(DatabaseSession::Config{
                .storage = storage_,
                .client_conn = std::move(*new_client),
            });
        } else {
            LOG_WARN("Dropping new connection. Already at max capacity. "
                    "max_clients={}", max_clients_);
        }
    }

    // service all client connections
    for (auto it = clients_.begin(); it != clients_.end();) {
        auto& client = *it;
        client.poll();
        if (client.closed()) {
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace funx::transaction

