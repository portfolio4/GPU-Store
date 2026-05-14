#ifndef FUNX_MARKET_DATA_MARKET_DATA_SERVICE_H
#define FUNX_MARKET_DATA_MARKET_DATA_SERVICE_H
#include <flip/flip.h>
#include <funx/net/UdpSocket.h>
#include <funx/match/Client.h>
#include <funx/md/config.h>
#include <funx/md/SnapshotTracker.h>

#include <list>
#include <unordered_map>

namespace funx::md {

class MarketDataService
{
public:

    MarketDataService(MarketDataServiceConfig const& c);

    void poll();

private:

    flip::Side convertToFlipSide(funx::Side s);

    void handleAddOrder(funx::match::AddOrder const& a, int64_t funx_timestamp);

    void handleMatch(funx::match::Match const& m, int64_t funx_timestamp);

    void handleRemoveOrder(funx::match::RemoveOrder const& r, int64_t funx_timestamp);

    template <typename T>
    void sendFlipIncrementalMsg(T const& body) {
        flip::MessageT<T> msg {
            .body = body
        };
        msg.header.seqnum = next_incremental_seqnum_;
        auto res = incremental_socket_.trySend(funx::util::to_bytes(msg));
        if (!res) {
            LOG_ERR("Failed to send incremental message. Seqnum={} Errno={}",
                    next_incremental_seqnum_, res.error());
        }
        ++next_incremental_seqnum_;
    }

    template <typename T>
    void sendFlipSnapshotMsg(T const& body) {
        flip::MessageT<T> msg {
            .body = body
        };
        msg.header.seqnum = next_snapshot_seqnum_;
        auto res = snapshot_socket_.trySend(funx::util::to_bytes(msg));
        if (!res) {
            LOG_ERR("Failed to send snapshot message. Seqnum={} Errno={}",
                    next_snapshot_seqnum_, res.error());
        }
        ++next_snapshot_seqnum_;
    }

    std::optional<SnapshotTracker*> getSnapshotTracker(funx::Product);

    void resetBooks(funx::match::Client const& listener);

private:
    // This order_id_to_product_ lookup is a bit inefficient because we're
    // going to do another order_id lookup in the SnapshotTracker. But
    // we'll keep it this way because it's simpler and don't need to optimize
    // latency just yet
    std::unordered_map<funx::OrderId, funx::Product> order_id_to_product_;
    std::unordered_map<funx::Product, SnapshotTracker> snapshot_trackers_;
    std::list<funx::match::Client> matching_service_listeners_;

    funx::UdpSocket incremental_socket_;
    uint64_t next_incremental_seqnum_ = 1;

    funx::UdpSocket snapshot_socket_;
    uint64_t next_snapshot_seqnum_ = 1;
};

} // namespace funx::md

#endif // FUNX_MARKET_DATA_MARKET_DATA_SERVICE_H

