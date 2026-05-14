#include <funx/md/SnapshotTracker.h>

#include <funx/log/Log.h>

#include <cassert>

namespace funx::md {

SnapshotTracker::SnapshotTracker(SnapshotTracker::Config const& c)
    : product_(c.product)
{
}

void SnapshotTracker::handleEvent(funx::match::AddOrder const& add)
{
    funx::match::Order const& order = add.order;
    if (id_to_order_.contains(order.order_id)) {
        LOG_ERR("SnapshotTracker got duplicate order ID {}. Ignoring it", order);
        assert(false);
        return;
    }
    id_to_order_[order.order_id] = order;

    switch (order.side)
    {
    case funx::Side::Buy:
        buys_[order.price].total_qty += order.qty;
        return;
    case funx::Side::Sell:
        sells_[order.price].total_qty += order.qty;
        return;
    }
    assert(false);
}

funx::Quantity SnapshotTracker::handleEvent(funx::match::Match const& match)
{
    switch (match.passive_side)
    {
    case funx::Side::Buy: {
        auto buy_it = id_to_order_.find(match.buyer_order_id);
        if (buy_it != id_to_order_.end()) {
            auto& order = buy_it->second;
            order.qty -= match.qty;
            buys_[order.price].total_qty -= match.qty;
            return order.qty;
        }
        return funx::Quantity{0};
    }
    case funx::Side::Sell: {
        auto sell_it = id_to_order_.find(match.seller_order_id);
        if (sell_it != id_to_order_.end()) {
            auto& order = sell_it->second;
            order.qty -= match.qty;
            sells_[order.price].total_qty -= match.qty;
            return order.qty;
        }
        return funx::Quantity{0};
    }
    }
    assert(false);
    return funx::Quantity{0};
}

void SnapshotTracker::handleEvent(funx::match::RemoveOrder const& remove)
{
    auto it = id_to_order_.find(remove.order_id);
    if (it != id_to_order_.end()) {
        auto const& order = it->second;
        [&]() {
            switch (order.side)
            {
            case funx::Side::Buy: {
                auto& total_qty = buys_[order.price].total_qty;
                total_qty -= order.qty;
                if (+total_qty <= 0) {
                    buys_.erase(order.price);
                }
                return;
            }
            case funx::Side::Sell: {
                auto& total_qty = sells_[order.price].total_qty;
                total_qty -= order.qty;
                if (+total_qty <= 0) {
                    sells_.erase(order.price);
                }
                return;
            }
            }
            assert(false);
        }();
        id_to_order_.erase(it);
    } else {
        LOG_ERR("SnapshotTracker got a remove for unknown order ID {}", remove.order_id);
        assert(false);
    }
}

flip::Snapshot SnapshotTracker::buildSnapshot() const
{
    flip::Snapshot snap{
        .product = +product_,
        .best_bids = {},
        .best_asks = {},
    };

    auto buy_it = buys_.rbegin();
    for (int i = 0; i < flip::Snapshot::LEVELS_PER_SIDE; ++i) {
        if (buy_it != buys_.rend()) {
            funx::Price price = buy_it->first;
            Level const& level = buy_it->second;
            assert(+level.total_qty > 0);
            snap.best_bids[i] = flip::LevelSnapshot{
                .price = +price,
                .total_qty = +level.total_qty,
            };
            ++buy_it;
        } else {
            snap.best_bids[i] = flip::LevelSnapshot{
                .price = 0,
                .total_qty = 0,
            };
        }
    }

    auto sell_it = sells_.rbegin();
    for (int i = 0; i < flip::Snapshot::LEVELS_PER_SIDE; ++i) {
        if (sell_it != sells_.rend()) {
            funx::Price price = sell_it->first;
            Level const& level = sell_it->second;
            assert(+level.total_qty > 0);
            snap.best_asks[i] = flip::LevelSnapshot{
                .price = +price,
                .total_qty = +level.total_qty,
            };
            ++sell_it;
        } else {
            snap.best_asks[i] = flip::LevelSnapshot{
                .price = 0,
                .total_qty = 0,
            };
        }
    }

    return snap;
}

} // namespace funx::md

