#ifndef FUNX_MATCH_MATCHING_SERVICE_H
#define FUNX_MATCH_MATCHING_SERVICE_H

#include <funx/common/types.h>
#include <funx/match/config.h>
#include <funx/match/ListenSession.h>
#include <funx/match/Matcher.h>
#include <funx/match/MatchingSession.h>
#include <funx/match/OrderTracker.h>
#include <funx/match/proto.h>
#include <funx/match/SessionTracker.h>
#include <funx/match/TxidGenerator.h>
#include <funx/net/TcpServerSocket.h>
#include <funx/net/TcpSocket.h>
#include <funx/net/types.h>
#include <funx/transaction/Client.h>
#include <funx/transaction/config.h>
#include <funx/transaction/proto.h>

#include <unordered_map>

namespace funx::match {

// Handles match
class MatchingService
{
public:
    MatchingService(MatchingServiceConfig const& c);

    void poll();

private:

    const size_t max_clients_;

    StandardTxidGenerator txid_generator_;
    SessionTracker session_tracker_;
    OrderTracker order_tracker_;
    Matcher matcher_;

    funx::TcpPort server_port_;
    funx::TcpPort listen_server_port_;
    funx::TcpServerSocket server_socket_;
    std::list<MatchingSession> clients_;
    std::list<ListenSession> listen_clients_;

    funx::transaction::Client transaction_db_client_;
};

} // namespace funx::match

#endif  // FUNX_MATCH_MATCHING_SERVICE_H

