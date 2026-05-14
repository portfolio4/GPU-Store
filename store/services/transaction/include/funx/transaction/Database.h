#ifndef FUNX_TRANSACTION_DATABASE_H
#define FUNX_TRANSACTION_DATABASE_H

#include <list>

#include <funx/net/TcpServerSocket.h>
#include <funx/net/TcpSocket.h>
#include <funx/transaction/DatabaseSession.h>
#include <funx/transaction/StorageInterface.h>

namespace funx::transaction {

class Database
{
public:
    struct Config
    {
        const size_t max_clients = 1024;
        StorageInterface* storage;
        funx::TcpPort port;
    };

    Database(Config const& c);

    void poll();

private:
    const size_t max_clients_;

    StorageInterface* storage_;

    funx::TcpServerSocket server_socket_;

    std::list<DatabaseSession> clients_;
};

} // namespace funx::transaction

#endif  // FUNX_TRANSACTION_DATABASE_H

