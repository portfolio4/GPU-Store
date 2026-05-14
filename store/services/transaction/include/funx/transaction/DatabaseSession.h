#ifndef FUNX_TRANSACTION_DATABASE_SESSION_H
#define FUNX_TRANSACTION_DATABASE_SESSION_H

#include <funx/net/TcpSession.h>
#include <funx/net/TcpSocket.h>
#include <funx/transaction/proto.h>
#include <funx/transaction/StorageInterface.h>

namespace funx::transaction {

class DatabaseSession
{
public:
    struct Config
    {
        StorageInterface* storage;
        funx::TcpSocket&& client_conn;
    };

    DatabaseSession(Config c);

    void poll();

    bool closed() const {
        return client_conn_.closed();
    }

private:

    void pollClient();

    void handleSaveTransaction(
            funx::transaction::proto::Header const& hdr,
            funx::transaction::proto::SaveTransaction const& save);

    void handleQueryTransaction(
            funx::transaction::proto::Header const& hdr,
            funx::transaction::proto::QueryTransaction const& query);


private:

    StorageInterface* storage_;
    funx::TcpSession client_conn_;
};

} // namespace funx::transaction

#endif  // FUNX_TRANSACTION_DATABASE_SESSION_H
