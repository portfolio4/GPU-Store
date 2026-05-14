#include <funx/md/MarketDataService.h>

#include <funx/match/proto.h>
#include <funx/util/overload.h>
#include <funx/common/flip.h>

namespace funx::md {

MarketDataService::MarketDataService(MarketDataServiceConfig const& c)
    : incremental_socket_(funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
        .local_ip4 = std::nullopt,
        .local_port = std::nullopt,
        .remote_ip4 = c.multicast_incremental_ip4,
        .remote_port = c.multicast_incremental_port,
      }))
    , snapshot_socket_(funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
        .local_ip4 = std::nullopt,
        .local_port = std::nullopt,
        .remote_ip4 = c.multicast_snapshot_ip4,
        .remote_port = c.multicast_snapshot_port,
      }))
{
    for (auto const& matching_service_cfg : c.matching_services) {
        for (funx::Product p : matching_service_cfg.products) {
            snapshot_trackers_.emplace(p, SnapshotTracker::Config{
                .product = p
            });
        }
        matching_service_listeners_.emplace_back(funx::match::Client::Config{
            .matching_service_cfg = matching_service_cfg,
            .mode = funx::match::Client::Mode::Listener,
        });
    }
}

void MarketDataService::poll()
{
    for (funx::match::Client& c : matching_service_listeners_) {
        if (!c.connected()) {
            resetBooks(c);
        }
        c.poll(funx::overloaded{
            [&](funx::match::proto::MatchingEvent const& m) {
                using funx::match::SerializedEvent;
                switch (m.event.type)
                {
                case SerializedEvent::EventType::AddOrder:
                    handleAddOrder(m.event.data.add, m.match_timestamp);
                    return;
                case SerializedEvent::EventType::Match:
                    handleMatch(m.event.data.match, m.match_timestamp);
                    return;
                case SerializedEvent::EventType::RemoveOrder:
                    handleRemoveOrder(m.event.data.remove, m.match_timestamp);
                    return;
                case SerializedEvent::EventType::IocMiss:
                    // IOC misses are not brodcast in market data
                    return;
                }
                LOG_ERR("Matching service sent unknown event type: {}", static_cast<int>(m.event.type));
                assert(false);
            },
            [&](funx::match::proto::Reject const&) {
                // nothing to broadcast if a request was rejected
            }
        });
    }
}

void MarketDataService::resetBooks(funx::match::Client const& listener)
{
    for (funx::Product product : listener.config().matching_service_cfg.products) {
        snapshot_trackers_.insert_or_assign(product, SnapshotTracker::Config{
            .product = product,
        });
        std::erase_if(order_id_to_product_, [product](auto const& entry) {
            return entry.second == product;
        });
    }
}

flip::Side MarketDataService::convertToFlipSide(funx::Side s)
{
    std::optional<flip::Side> maybe_flip_side = funx::funxSideToFlipSide(s);
    if (!maybe_flip_side) [[unlikely]] {
        LOG_ERR("Matching service sent invalid Side: {}", static_cast<int>(s));
        return flip::Side::Buy;
    }
    return *maybe_flip_side;
}

void MarketDataService::handleAddOrder(funx::match::AddOrder const& a, int64_t funx_timestamp) {
    sendFlipIncrementalMsg(flip::AddOrder{
        .product = +a.order.product,
        .side = convertToFlipSide(a.order.side),
        .qty = +a.order.qty,
        .price = +a.order.price,
        .funx_order_id = +a.order.order_id,
        .funx_event_timestamp = +funx_timestamp,
    });
    auto maybe_snapshot_tracker = getSnapshotTracker(a.order.product);
    if (!maybe_snapshot_tracker) [[unlikely]] {
        return;
    }
    SnapshotTracker& snapshot_tracker = **maybe_snapshot_tracker;

    snapshot_tracker.handleEvent(a);
    sendFlipSnapshotMsg(snapshot_tracker.buildSnapshot());

    order_id_to_product_[a.order.order_id] = a.order.product;
}

void MarketDataService::handleMatch(funx::match::Match const& m, int64_t funx_timestamp) {
    uint64_t funx_order_id =
        m.passive_side == funx::Side::Buy
            ? +m.buyer_order_id
            : +m.seller_order_id;
    sendFlipIncrementalMsg(flip::Trade{
        .product = +m.product,
        .side = convertToFlipSide(m.passive_side),
        .qty = +m.qty,
        .price = +m.price,
        .funx_order_id = funx_order_id,
        .funx_transaction_id = +m.txid,
        .funx_event_timestamp = funx_timestamp,
    });

    auto maybe_snapshot_tracker = getSnapshotTracker(m.product);
    if (!maybe_snapshot_tracker) [[unlikely]] {
        return;
    }
    SnapshotTracker& snapshot_tracker = **maybe_snapshot_tracker;

    funx::Quantity remaining_qty = snapshot_tracker.handleEvent(m);
    if (+remaining_qty > 0) {
        sendFlipIncrementalMsg(flip::ModifyOrder{
            .funx_order_id = funx_order_id,
            .new_qty = +remaining_qty,
            // trades should always be at the passive order's price
            // and only passive orders should be getting modified.
            .new_price = +m.price,
            .funx_event_timestamp = funx_timestamp,
        });

        // Note: if there's no remaining quantity left, then we don't
        // sent a snapshot, because there will be a RemoveOrder coming right
        // after, and we'll send the snapshot after that
        sendFlipSnapshotMsg(snapshot_tracker.buildSnapshot());
    }
}

void MarketDataService::handleRemoveOrder(funx::match::RemoveOrder const& r, int64_t funx_timestamp)
{
    sendFlipIncrementalMsg(flip::RemoveOrder{
        .funx_order_id = +r.order_id,
        .funx_event_timestamp = funx_timestamp,
    });

    auto product_it = order_id_to_product_.find(r.order_id);
    if (product_it == order_id_to_product_.end()) [[unlikely]] {
        LOG_ERR("Matching service sent Remove for unknown order ID {}", r.order_id);
        assert(false);
        return;
    }

    auto maybe_snapshot_tracker = getSnapshotTracker(product_it->second);
    if (!maybe_snapshot_tracker) [[unlikely]] {
        return;
    }
    SnapshotTracker& snapshot_tracker = **maybe_snapshot_tracker;

    order_id_to_product_.erase(product_it);

    snapshot_tracker.handleEvent(r);
    sendFlipSnapshotMsg(snapshot_tracker.buildSnapshot());
}

std::optional<SnapshotTracker*> MarketDataService::getSnapshotTracker(funx::Product p)
{
    auto snapshot_tracker_it = snapshot_trackers_.find(p);
    if (snapshot_tracker_it == snapshot_trackers_.end()) {
        LOG_ERR("Matching engine sent product {} that didn't have a snapshot tracker configured", p);
        assert(false);
        return std::nullopt;
    }

    return &snapshot_tracker_it->second;
}

} // namespace funx::md

