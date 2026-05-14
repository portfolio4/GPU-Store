#include <catch2/catch_all.hpp>

#include <flip/flip.h>
#include <funx/match/proto.h>
#include <funx/match/test/defaults.h>
#include <funx/md/SnapshotTracker.h>

TEST_CASE("Test Snapshot Tracker", "[md]")
{
    const funx::Product PRODUCT{5};
    funx::md::SnapshotTracker tracker(funx::md::SnapshotTracker::Config{
        .product = PRODUCT
    });

    auto requireBuySnapshotLevel = [&](int level, flip::LevelSnapshot expected) {
        flip::Snapshot snap = tracker.buildSnapshot();
        REQUIRE(snap.best_bids[level].price == expected.price);
        REQUIRE(snap.best_bids[level].total_qty == expected.total_qty);
    };
    auto requireSellSnapshotLevel = [&](int level, flip::LevelSnapshot expected) {
        flip::Snapshot snap = tracker.buildSnapshot();
        REQUIRE(snap.best_asks[level].price == expected.price);
        REQUIRE(snap.best_asks[level].total_qty == expected.total_qty);
    };

    SECTION("Add, trade, remove orders") {
        auto buy = funx::test::defaults::match::defaultBuyOrder();
        tracker.handleEvent(funx::match::AddOrder{
            .order = buy
        });
        auto sell = funx::test::defaults::match::defaultSellOrder();
        sell.price += 1;
        tracker.handleEvent(funx::match::AddOrder{
            .order = sell
        });

        requireBuySnapshotLevel(0, flip::LevelSnapshot{
            .price = +buy.price,
            .total_qty = +buy.qty,
        });
        requireSellSnapshotLevel(0, flip::LevelSnapshot{
            .price = +sell.price,
            .total_qty = +sell.qty,
        });

        tracker.handleEvent(funx::match::Match{
            .product = PRODUCT,
            .qty = buy.qty - 1,
            .price = buy.price,
            .txid = funx::TransactionId{0},
            .buyer_company = funx::CompanyId{1},
            .buyer_order_id = buy.order_id,
            .seller_company = funx::CompanyId{2},
            .seller_order_id = funx::test::defaults::match::nextOrderId(),
            .passive_side = funx::Side::Buy,
        });
        tracker.handleEvent(funx::match::Match{
            .product = PRODUCT,
            .qty = sell.qty - 2,
            .price = sell.price,
            .txid = funx::TransactionId{0},
            .buyer_company = funx::CompanyId{1},
            .buyer_order_id = funx::test::defaults::match::nextOrderId(),
            .seller_company = funx::CompanyId{2},
            .seller_order_id = sell.order_id,
            .passive_side = funx::Side::Sell,
        });

        requireBuySnapshotLevel(0, flip::LevelSnapshot{
            .price = +buy.price,
            .total_qty = 1,
        });
        requireSellSnapshotLevel(0, flip::LevelSnapshot{
            .price = +sell.price,
            .total_qty = 2,
        });

        tracker.handleEvent(funx::match::RemoveOrder{
            .order_id = buy.order_id
        });
        tracker.handleEvent(funx::match::RemoveOrder{
            .order_id = sell.order_id
        });

        requireBuySnapshotLevel(0, flip::LevelSnapshot{
            .price = 0,
            .total_qty = 0,
        });
        requireSellSnapshotLevel(0, flip::LevelSnapshot{
            .price = 0,
            .total_qty = 0,
        });
    }

    SECTION("15 levels of 5 orders on each side") {
        std::vector<funx::match::Order> buys;
        std::vector<funx::match::Order> sells;
        const int ORDERS_PER_LEVEL = 5;
        for (int j = 0; j < ORDERS_PER_LEVEL; ++j) {
            for (int i = 1; i <= flip::Snapshot::LEVELS_PER_SIDE + 5; ++i) {
                auto buy = funx::test::defaults::match::defaultBuyOrder();
                buy.qty = funx::Quantity{2*i};
                buy.price = funx::test::defaults::PRICE - i;
                buys.push_back(buy);
                tracker.handleEvent(funx::match::AddOrder{
                    .order = buy
                });
                auto sell = funx::test::defaults::match::defaultSellOrder();
                sell.qty = funx::Quantity{3*i};
                sell.price = funx::test::defaults::PRICE + i;
                sells.push_back(sell);
                tracker.handleEvent(funx::match::AddOrder{
                    .order = sell
                });
            }
        }

        for (int i = 1; i <= flip::Snapshot::LEVELS_PER_SIDE; ++i) {
            requireBuySnapshotLevel(i - 1, flip::LevelSnapshot{
                .price = +funx::test::defaults::PRICE - i,
                .total_qty = ORDERS_PER_LEVEL*2*i,
            });
            requireSellSnapshotLevel(i - 1, flip::LevelSnapshot{
                .price = +funx::test::defaults::PRICE + i,
                .total_qty = ORDERS_PER_LEVEL*3*i,
            });
        }

        // Now partially fill qty=1 of the first 15 orders on each side
        for (int i = 0; i < flip::Snapshot::LEVELS_PER_SIDE + 5; ++i) {
            tracker.handleEvent(funx::match::Match{
                .product = PRODUCT,
                .qty = funx::Quantity{1},
                .price = buys[i].price,
                .txid = funx::TransactionId{0},
                .buyer_company = funx::CompanyId{1},
                .buyer_order_id = buys[i].order_id,
                .seller_company = funx::CompanyId{2},
                .seller_order_id = funx::test::defaults::match::nextOrderId(),
                .passive_side = funx::Side::Buy,
            });
            tracker.handleEvent(funx::match::Match{
                .product = PRODUCT,
                .qty = funx::Quantity{1},
                .price = sells[i].price,
                .txid = funx::TransactionId{0},
                .buyer_company = funx::CompanyId{1},
                .buyer_order_id = funx::test::defaults::match::nextOrderId(),
                .seller_company = funx::CompanyId{2},
                .seller_order_id = sells[i].order_id,
                .passive_side = funx::Side::Sell,
            });

        }

        for (int i = 1; i <= flip::Snapshot::LEVELS_PER_SIDE; ++i) {
            requireBuySnapshotLevel(i - 1, flip::LevelSnapshot{
                .price = +funx::test::defaults::PRICE - i,
                .total_qty = ORDERS_PER_LEVEL*2*i - 1,
            });
            requireSellSnapshotLevel(i - 1, flip::LevelSnapshot{
                .price = +funx::test::defaults::PRICE + i,
                .total_qty = ORDERS_PER_LEVEL*3*i - 1,
            });
        }

        // Now remove the last 15 orders added to each level
        for (int i = 0; i < flip::Snapshot::LEVELS_PER_SIDE + 5; ++i) {
            tracker.handleEvent(funx::match::RemoveOrder{
                .order_id = buys[buys.size() - 1 - i].order_id
            });
            tracker.handleEvent(funx::match::RemoveOrder{
                .order_id = sells[sells.size() - 1 - i].order_id
            });
        }
        for (int i = 1; i <= flip::Snapshot::LEVELS_PER_SIDE; ++i) {
            requireBuySnapshotLevel(i - 1, flip::LevelSnapshot{
                .price = +funx::test::defaults::PRICE - i,
                .total_qty = (ORDERS_PER_LEVEL - 1) *2*i - 1,
            });
            requireSellSnapshotLevel(i - 1, flip::LevelSnapshot{
                .price = +funx::test::defaults::PRICE + i,
                .total_qty = (ORDERS_PER_LEVEL - 1)*3*i - 1,
            });
        }
    }
}

