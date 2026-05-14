#pragma once

#include <funx/clock/Clock.h>
#include <funx/common/flip.h>
#include <funx/md/Client.h>
#include <funx/util/demangle.h>

class MarketDataTestClient
{
public:

    static void requireAddMatchesOrder(flip::AddOrder const& add, funx::match::Order const& order)
    {
        REQUIRE(add.product == +order.product);
        auto maybe_flip_side = funx::funxSideToFlipSide(order.side);
        REQUIRE(maybe_flip_side);
        REQUIRE(add.side == *maybe_flip_side);
        REQUIRE(add.qty == +order.qty);
        REQUIRE(add.price == +order.price);
        REQUIRE(add.funx_order_id == +order.order_id);
    }

    static void requireTradeMatchesOrder(flip::Trade const& trade, funx::match::Order const& order, int64_t trade_qty)
    {
        REQUIRE(trade.product == +order.product);
        auto maybe_flip_side = funx::funxSideToFlipSide(order.side);
        REQUIRE(maybe_flip_side);
        REQUIRE(trade.side == *maybe_flip_side);
        REQUIRE(trade.qty == trade_qty);
        REQUIRE(trade.price == +order.price);
        REQUIRE(trade.funx_order_id == +order.order_id);
    }

    static void requireModifyMatchesOrder(flip::ModifyOrder const& modify, funx::match::Order const& order)
    {
        REQUIRE(modify.funx_order_id == +order.order_id);
        REQUIRE(modify.new_qty == +order.qty);
        REQUIRE(modify.new_price == +order.price);
    }

    static void requireRemoveMatchesOrder(flip::RemoveOrder const& remove, funx::match::Order const& order)
    {
        REQUIRE(remove.funx_order_id == +order.order_id);
    }

    void requireNextSnapshotIs(flip::Snapshot const& expected) {
        auto snap = requireSnapshot<flip::Snapshot>();
        REQUIRE(snap.product == expected.product);
        for (int i = 0; i < flip::Snapshot::LEVELS_PER_SIDE; ++i) {
            REQUIRE(snap.best_bids[i].price == expected.best_bids[i].price);
            REQUIRE(snap.best_bids[i].total_qty == expected.best_bids[i].total_qty);
        }
    }

    MarketDataTestClient(funx::md::Client::Config const& c)
        : client_(c)
    {
    }

    template <flip::AnyBody T>
    T requireIncremental()
    {
        auto stop = funx::Clock::now() + receive_timeout_;
        do {
            std::optional<T> rtn;
            client_.pollIncremental(funx::overloaded{
                [&](flip::MessageT<T> const& msg) {
                    rtn = msg.body;
                },
                [](auto&&) {}
            });
            if (rtn) {
                return *rtn;
            }
        } while (funx::Clock::now() < stop);
        FAIL("expecting response of type " << funx::demangle(typeid(T).name())
                << " on incremental channel but didn't get one before timeout");
        return {};
    }

    template <flip::AnyBody T>
    T requireSnapshot()
    {
        auto stop = funx::Clock::now() + receive_timeout_;
        do {
            std::optional<T> rtn;
            client_.pollSnapshot(funx::overloaded{
                [&](flip::MessageT<T> const& msg) {
                    rtn = msg.body;
                },
                [](auto&&) {}
            });
            if (rtn) {
                return *rtn;
            }
        } while (funx::Clock::now() < stop);
        FAIL("expecting response of type " << funx::demangle(typeid(T).name())
                << " on snapshot channel but didn't get one before timeout");
        return {};
    }
private:

    funx::md::Client client_;

    std::chrono::milliseconds receive_timeout_ = std::chrono::milliseconds(1000);

};

