#include <catch2/catch_all.hpp>

#include <flip/flip.h>
#include <funx/common/flip.h>
#include <funx/match/Client.h>
#include <funx/match/proto.h>
#include <funx/match/test/defaults.h>
#include <funx/match/test/mocks.h>
#include <funx/md/Client.h>
#include <funx/md/config.h>
#include <funx/md/MarketDataService.h>

#include "market_data_test_client.h"

TEST_CASE("Test Market Data One Matching Service", "[md]")
{
    funx::match::test::RunMockMatchingService matching_service;
    funx::match::Client match_client(funx::match::Client::Config{
        .matching_service_cfg = matching_service.getMatchingServiceConfig(),
        .mode = funx::match::Client::Mode::Client,
    });

    funx::md::MarketDataServiceConfig md_config{
        .multicast_incremental_ip4 = "239.1.2.3",
        .multicast_incremental_port = funx::UdpPort{8000},
        .multicast_snapshot_ip4 = "238.1.2.3",
        .multicast_snapshot_port = funx::UdpPort{9000},
        .matching_services = {matching_service.getMatchingServiceConfig()},
    };
    funx::md::MarketDataService md(md_config);

    bool test_finished = false;
    std::thread md_thread([&]() {
        auto stop = funx::Clock::now() + std::chrono::seconds(30);
        while (!test_finished && funx::Clock::now() < stop) {
            md.poll();
        }
    });

    MarketDataTestClient client(funx::md::Client::Config{
        .md_cfg = md_config
    });


    SECTION("Add, Trade, Modify, Delete") {
        flip::Snapshot expected_snap{};

        auto buy = funx::test::defaults::match::defaultBuyOrder();
        match_client.addOrder(buy);
        expected_snap.product = +buy.product;
        expected_snap.best_bids[0].price = +buy.price;
        expected_snap.best_bids[0].total_qty = +buy.qty;
        client.requireNextSnapshotIs(expected_snap);

        auto add = client.requireIncremental<flip::AddOrder>();
        MarketDataTestClient::requireAddMatchesOrder(add, buy);

        auto sell = funx::test::defaults::match::defaultSellOrder();
        sell.qty = buy.qty - 1;
        match_client.addOrder(sell);

        auto trade = client.requireIncremental<flip::Trade>();
        MarketDataTestClient::requireTradeMatchesOrder(trade, buy, +sell.qty);

        buy.qty = funx::Quantity{1};
        auto modify = client.requireIncremental<flip::ModifyOrder>();
        MarketDataTestClient::requireModifyMatchesOrder(modify, buy);
        expected_snap.best_bids[0].total_qty = 1;
        client.requireNextSnapshotIs(expected_snap);

        match_client.cancelOrder(buy.order_id);
        auto remove = client.requireIncremental<flip::RemoveOrder>();
        MarketDataTestClient::requireRemoveMatchesOrder(remove, buy);
        expected_snap.best_bids[0].total_qty = 0;
        expected_snap.best_bids[0].price = 0;
        client.requireNextSnapshotIs(expected_snap);
    }

    SECTION("Add lots of orders for snapshot") {
        // if this is failing check that the system-wide socket receiver buffer
        // is large enough - i.e. `sudo sysctl -w net.core.rmem_max=33554432`
        const int ORDERS_PER_LEVEL = 5;
        const funx::Quantity QTY_PER_ORDER{3};
        flip::Snapshot expected_snap{};

        auto buy = funx::test::defaults::match::defaultBuyOrder();
        buy.qty = QTY_PER_ORDER;
        buy.price = funx::Price{95};

        auto sell = funx::test::defaults::match::defaultSellOrder();
        sell.qty = QTY_PER_ORDER;
        sell.price += funx::Price{105};

        expected_snap.product = +buy.product;

        // Populate every level with the 5 orders of qty 10
        for (int level = 0; level < flip::Snapshot::LEVELS_PER_SIDE; ++level) {
            for (int i = 0; i < ORDERS_PER_LEVEL; ++i) {
                match_client.addOrder(buy);
                auto add_buy = client.requireIncremental<flip::AddOrder>();
                MarketDataTestClient::requireAddMatchesOrder(add_buy, buy);
                buy.order_id = funx::test::defaults::match::nextOrderId();
                expected_snap.best_bids[level].price = +buy.price;
                expected_snap.best_bids[level].total_qty += +buy.qty;
                client.requireNextSnapshotIs(expected_snap);

                match_client.addOrder(sell);
                auto add_sell = client.requireIncremental<flip::AddOrder>();
                MarketDataTestClient::requireAddMatchesOrder(add_sell, sell);
                sell.order_id = funx::test::defaults::match::nextOrderId();
                expected_snap.best_asks[level].price = +sell.price;
                expected_snap.best_asks[level].total_qty += +sell.qty;
                client.requireNextSnapshotIs(expected_snap);
            }
            buy.price -= 5;
            sell.price += 5;
        }

        // Then trade out every order at increments of qty=1 until
        // nothing is left. Validate the snapshot/incremental updates
        // are correct
        for (int level = 0; level < flip::Snapshot::LEVELS_PER_SIDE; ++level) {

            int64_t best_buy_price = expected_snap.best_bids[0].price;
            int64_t best_sell_price = expected_snap.best_asks[0].price;

            // note: buy and sell levels both have the same quantity
            int64_t total_level_qty = +QTY_PER_ORDER * ORDERS_PER_LEVEL;
            for (int i = 0; i < total_level_qty; ++i) {
                buy = funx::test::defaults::match::defaultBuyOrder();
                buy.price = funx::Price{best_sell_price + 1};
                buy.qty = funx::Quantity{1};
                match_client.addOrder(buy);
                expected_snap.best_asks[0].total_qty -= 1;

                // validate incremental
                auto sell_trade = client.requireIncremental<flip::Trade>();
                REQUIRE(sell_trade.price == expected_snap.best_asks[0].price);
                REQUIRE(sell_trade.qty == 1);
                if (expected_snap.best_asks[0].total_qty % +QTY_PER_ORDER == 0) {
                    client.requireIncremental<flip::RemoveOrder>();
                    // tracking individual orders is more work than needed...
                    // the individual order IDs should be okay
                }

                // validate snapshot
                if (expected_snap.best_asks[0].total_qty == 0) {
                    for (int i = 0; i < flip::Snapshot::LEVELS_PER_SIDE - 1; ++i) {
                        expected_snap.best_asks[i].price = expected_snap.best_asks[i+1].price;
                        expected_snap.best_asks[i].total_qty = expected_snap.best_asks[i+1].total_qty;
                    }
                    expected_snap.best_asks[flip::Snapshot::LEVELS_PER_SIDE - 1].price = 0;
                    expected_snap.best_asks[flip::Snapshot::LEVELS_PER_SIDE - 1].total_qty = 0;
                }
                client.requireNextSnapshotIs(expected_snap);


                sell = funx::test::defaults::match::defaultSellOrder();
                sell.price = funx::Price{best_buy_price - 1};
                sell.qty = funx::Quantity{1};
                match_client.addOrder(sell);
                expected_snap.best_bids[0].total_qty -= 1;

                // validate incremental
                auto buy_trade = client.requireIncremental<flip::Trade>();
                REQUIRE(buy_trade.price == expected_snap.best_bids[0].price);
                REQUIRE(buy_trade.qty == 1);
                if (expected_snap.best_bids[0].total_qty % +QTY_PER_ORDER == 0) {
                    client.requireIncremental<flip::RemoveOrder>();
                    // tracking individual orders is more work than needed...
                    // the individual order IDs should be okay
                }

                // validate snapshot
                if (expected_snap.best_bids[0].total_qty == 0) {
                    for (int i = 0; i < flip::Snapshot::LEVELS_PER_SIDE - 1; ++i) {
                        expected_snap.best_bids[i].price = expected_snap.best_bids[i+1].price;
                        expected_snap.best_bids[i].total_qty = expected_snap.best_bids[i+1].total_qty;
                    }
                    expected_snap.best_bids[flip::Snapshot::LEVELS_PER_SIDE - 1].price = 0;
                    expected_snap.best_bids[flip::Snapshot::LEVELS_PER_SIDE - 1].total_qty = 0;
                }
                client.requireNextSnapshotIs(expected_snap);
            }
        }
    }

    test_finished = true;
    md_thread.join();
}

TEST_CASE("Market data resets books after matching service reconnect", "[md]")
{
    auto matching_service = std::make_unique<funx::match::test::RunMockMatchingService>();
    auto matching_service_cfg = matching_service->getMatchingServiceConfig();
    auto match_client = std::make_unique<funx::match::Client>(funx::match::Client::Config{
        .matching_service_cfg = matching_service_cfg,
        .mode = funx::match::Client::Mode::Client,
    });

    funx::md::MarketDataServiceConfig md_config{
        .multicast_incremental_ip4 = "239.1.2.3",
        .multicast_incremental_port = funx::UdpPort{8000},
        .multicast_snapshot_ip4 = "238.1.2.3",
        .multicast_snapshot_port = funx::UdpPort{9000},
        .matching_services = {matching_service_cfg},
    };
    funx::md::MarketDataService md(md_config);
    bool test_finished = false;
    std::thread md_thread([&]() {
        auto stop = funx::Clock::now() + std::chrono::seconds(10);
        while (!test_finished && funx::Clock::now() < stop) {
            md.poll();
        }
    });
    MarketDataTestClient client(funx::md::Client::Config{.md_cfg = md_config});

    auto old_order = funx::test::defaults::match::defaultBuyOrder();
    match_client->addOrder(old_order);
    flip::Snapshot old_snapshot{};
    old_snapshot.product = +old_order.product;
    old_snapshot.best_bids[0] = flip::LevelSnapshot{
        .price = +old_order.price,
        .total_qty = +old_order.qty,
    };
    client.requireNextSnapshotIs(old_snapshot);

    match_client.reset();
    matching_service.reset();
    matching_service = std::make_unique<funx::match::test::RunMockMatchingService>();
    match_client = std::make_unique<funx::match::Client>(funx::match::Client::Config{
        .matching_service_cfg = matching_service_cfg,
        .mode = funx::match::Client::Mode::Client,
    });

    auto reconnect_wait_end = funx::SteadyClock::now() +
        2 * funx::match::Client::RECONNECT_INTERVAL;
    while (funx::SteadyClock::now() < reconnect_wait_end) {
    }

    auto new_order = funx::test::defaults::match::defaultBuyOrder();
    new_order.price += 10;
    match_client->addOrder(new_order);

    flip::Snapshot new_snapshot{};
    new_snapshot.product = +new_order.product;
    new_snapshot.best_bids[0] = flip::LevelSnapshot{
        .price = +new_order.price,
        .total_qty = +new_order.qty,
    };
    client.requireNextSnapshotIs(new_snapshot);

    test_finished = true;
    md_thread.join();
}
