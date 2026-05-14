#include <funx/match/test/catch_printers.h>
#include <catch2/catch_all.hpp>

#include <mutex>

#include <funx/match/MatchingService.h>
#include <funx/match/test/defaults.h>
#include <funx/match/proto.h>
#include <funx/transaction/test/mocks.h>

#include "matching_test_client.h"

using namespace funx::match;
using namespace funx::match::proto;
using namespace funx::test::defaults::match;

TEST_CASE("Test MatchingService <> Client Communication", "[match]")
{
    funx::transaction::test::RunMockDb transaction_db;

    const funx::TcpPort MATCHING_SERVICE_PORT{9998};
    const funx::TcpPort LISTEN_PORT{9997};
    MatchingServiceConfig cfg {
        .unique_id = 1,
        .products = {PRODUCT},
        .server_ip4 = "127.0.0.1",
        .server_port = MATCHING_SERVICE_PORT,
        .listen_server_port = LISTEN_PORT,
        .transaction_db_app_cfg = transaction_db.getTransactionDbAppConfig(),
    };

    bool test_finished = false;
    MatchingService match(cfg);
    std::mutex matching_service_mutex;

    std::thread server_thread([&]() {
        auto stop = funx::Clock::now() + std::chrono::seconds(5);
        while (!test_finished && funx::Clock::now() < stop) {
            std::scoped_lock lock(matching_service_mutex);
            match.poll();
        }
    });

    MatchingTestClient test_client(cfg);
    MatchingTestClient test_listener(cfg, funx::match::Client::Mode::Listener);

    SECTION("Add unknown product") {
        auto order = defaultBuyOrder();
        order.product = funx::Product{+order.product + 1};
        test_client.addOrderAndExpectReject(order, ErrorCode::UnknownProduct);
    }

    SECTION("One client send and cancel order") {
        auto order = defaultBuyOrder();
        test_client.addOrderAndExpectEvents(order,
            {
                AddOrder{
                    .order = order
                }
            }
        );

        // Send same order again and it should be rejected with DuplicateOrderId
        test_client.addOrderAndExpectReject(order, ErrorCode::DuplicateOrderId);

        test_client.cancelOrderAndExpectEvents(order.order_id, {
            RemoveOrder{
                .order_id = order.order_id
            }
        });

        // Send cancel again and should get rejected with OrderNotFound
        test_client.cancelOrderAndExpectReject(order.order_id, ErrorCode::OrderNotFound);

        // A cancelled order ID is no longer live and can be reused.
        test_client.addOrderAndExpectEvents(order, {
            AddOrder{
                .order = order
            }
        });
    }

    SECTION("One client partially fills IOC") {
        auto buy1 = defaultBuyOrder();
        buy1.qty = funx::Quantity{10};
        auto buy2 = defaultBuyOrder();
        buy2.qty = funx::Quantity{20};

        auto sell = defaultSellOrder();
        sell.duration = funx::Duration::IOC;
        sell.qty = funx::Quantity{1000};

        test_client.addOrderAndExpectEvents(buy1, {
            AddOrder{
                .order = buy1
            }
        });
        test_client.addOrderAndExpectEvents(buy2, {
            AddOrder{
                .order = buy2
            }
        });

        test_client.addOrderAndExpectEvents(sell, {
            Match{
                .product = buy1.product,
                .qty = buy1.qty,
                .price = buy1.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy1.company,
                .buyer_order_id = buy1.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            RemoveOrder{
                .order_id = buy1.order_id
            },
            Match{
                .product = buy2.product,
                .qty = buy2.qty,
                .price = buy2.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy2.company,
                .buyer_order_id = buy2.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            RemoveOrder{
                .order_id = buy2.order_id
            },
            IocMiss {
                .order_id = sell.order_id
            }
        });

        // The completed IOC ID and fully-filled passive IDs are no longer live.
        test_client.addOrderAndExpectEvents(sell, {
            IocMiss {
                .order_id = sell.order_id
            }
        });
        test_client.addOrderAndExpectEvents(buy1, {
            AddOrder{
                .order = buy1
            }
        });
    }

    SECTION("One client partial fills") {
        auto sell1 = defaultSellOrder();
        sell1.qty = funx::Quantity{20};
        auto sell2 = defaultSellOrder();
        sell2.qty = funx::Quantity{40};
        auto sell3 = defaultSellOrder();
        sell3.qty = funx::Quantity{60};

        auto buy = defaultBuyOrder();
        buy.qty = funx::Quantity{1000};

        INFO("sell1=" << sell1);
        INFO("sell2=" << sell2);
        INFO("sell3=" << sell3);
        INFO("buy=" << buy);

        test_client.addOrderAndExpectEvents(sell1, {
            AddOrder{
                .order = sell1
            }
        });
        test_client.addOrderAndExpectEvents(sell2, {
            AddOrder{
                .order = sell2
            }
        });
        test_client.addOrderAndExpectEvents(sell3, {
            AddOrder{
                .order = sell3
            }
        });

        test_client.addOrderAndExpectEvents(buy, {
            Match{
                .product = sell1.product,
                .qty = sell1.qty,
                .price = sell1.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy.company,
                .buyer_order_id = buy.order_id,
                .seller_company = sell1.company,
                .seller_order_id = sell1.order_id,
                .passive_side = funx::Side::Sell,
            },
            RemoveOrder{
                .order_id = sell1.order_id
            },
            Match{
                .product = sell2.product,
                .qty = sell2.qty,
                .price = sell2.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy.company,
                .buyer_order_id = buy.order_id,
                .seller_company = sell2.company,
                .seller_order_id = sell2.order_id,
                .passive_side = funx::Side::Sell,
            },
            RemoveOrder{
                .order_id = sell2.order_id
            },
            Match{
                .product = sell3.product,
                .qty = sell3.qty,
                .price = sell3.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy.company,
                .buyer_order_id = buy.order_id,
                .seller_company = sell3.company,
                .seller_order_id = sell3.order_id,
                .passive_side = funx::Side::Sell,
            },
            RemoveOrder{
                .order_id = sell3.order_id
            },
            AddOrder{
                .order = [&]() {
                    auto rtn = buy;
                    rtn.qty = buy.qty - sell1.qty - sell2.qty - sell3.qty;
                    return rtn;
                 }()
            }
        });
        test_client.expectNoEvents();
    }

    SECTION("Two clients") {
        MatchingTestClient test_client2(cfg);

        auto buy1 = defaultBuyOrder();
        buy1.qty = funx::Quantity{25};
        auto buy2 = defaultBuyOrder();
        buy2.qty = funx::Quantity{50};
        auto buy3 = defaultBuyOrder();
        buy3.qty = funx::Quantity{75};

        auto sell = defaultSellOrder();
        sell.qty = funx::Quantity{1000};

        test_client.addOrderAndExpectEvents(buy1, {
            AddOrder{
                .order = buy1
            }
        });
        test_client.addOrderAndExpectEvents(buy2, {
            AddOrder{
                .order = buy2
            }
        });
        test_client.addOrderAndExpectEvents(buy3, {
            AddOrder{
                .order = buy3
            }
        });

        test_client2.expectNoEvents();

        test_client2.addOrderAndExpectEvents(sell, {
            Match{
                .product = buy1.product,
                .qty = buy1.qty,
                .price = buy1.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy1.company,
                .buyer_order_id = buy1.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            Match{
                .product = buy2.product,
                .qty = buy2.qty,
                .price = buy2.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy2.company,
                .buyer_order_id = buy2.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            Match{
                .product = buy3.product,
                .qty = buy3.qty,
                .price = buy3.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy3.company,
                .buyer_order_id = buy3.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            AddOrder{
                .order = [&]() {
                    auto rtn = sell;
                    rtn.qty = sell.qty - buy1.qty - buy2.qty - buy3.qty;
                    return rtn;
                 }()
            }
        });

        // no more events
        test_client2.expectNoEvents();

        test_client.expectEvents({
            Match{
                .product = buy1.product,
                .qty = buy1.qty,
                .price = buy1.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy1.company,
                .buyer_order_id = buy1.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            RemoveOrder{
                .order_id = buy1.order_id
            },
            Match{
                .product = buy2.product,
                .qty = buy2.qty,
                .price = buy2.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy2.company,
                .buyer_order_id = buy2.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            RemoveOrder{
                .order_id = buy2.order_id
            },
            Match{
                .product = buy3.product,
                .qty = buy3.qty,
                .price = buy3.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy3.company,
                .buyer_order_id = buy3.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            RemoveOrder{
                .order_id = buy3.order_id
            }
        });

        // no more events
        test_client.expectNoEvents();

        // client 2 cancels their resting order
        test_client2.cancelOrderAndExpectEvents(sell.order_id, {
            RemoveOrder{
                .order_id = sell.order_id
            },
        });
        test_client2.expectNoEvents();
        test_client.expectNoEvents();
    }


    SECTION("Disconnect cancels all resting orders") {
        auto resting_buy = defaultBuyOrder();
        {
            MatchingTestClient disconnecting_client(cfg);
            disconnecting_client.addOrderAndExpectEvents(resting_buy, {
                AddOrder{
                    .order = resting_buy
                }
            });
            disconnecting_client.close();
        }

        // On poll, the matching service detects the closed client connection
        // and cancels all orders owned by that session.
        {
            std::scoped_lock lock(matching_service_mutex);
            match.poll();
        }
        auto crossing_sell = defaultSellOrder();
        crossing_sell.duration = funx::Duration::IOC;
        crossing_sell.order_id = nextOrderId();
        test_client.addOrderAndExpectEvents(crossing_sell, {
            IocMiss{
                .order_id = crossing_sell.order_id
            }
        });

        // Both the matcher and order tracker released the disconnected order.
        test_client.addOrderAndExpectEvents(resting_buy, {
            AddOrder{
                .order = resting_buy
            }
        });
    }

    SECTION("Many clients cross against resting order") {
        const int NUM_CLIENTS = 10;

        auto buy = defaultBuyOrder();
        buy.qty = funx::Quantity{1000};

        test_client.addOrderAndExpectEvents(buy, {
            AddOrder{
                .order = buy
            }
        });
        test_listener.expectEvents({
            AddOrder{
                .order = buy
            }
        });

        for (int i = 0; i < NUM_CLIENTS; ++i) {
            MatchingTestClient another_client(cfg);

            auto sell = defaultSellOrder();
            sell.qty = funx::Quantity{28};

            Match expected_match{
                .product = buy.product,
                .qty = sell.qty,
                .price = buy.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy.company,
                .buyer_order_id = buy.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            };
            another_client.addOrderAndExpectEvents(sell, {
                expected_match
            });
            another_client.expectNoEvents();
            test_client.expectEvents({
                expected_match
            });
            test_client.expectNoEvents();
            test_listener.expectEvents({
                expected_match
            });
            test_listener.expectNoEvents();
        }
    }

    SECTION("Many clients place resting orders and get swept") {
        const int NUM_CLIENTS = 50;
        std::vector<Order> resting_orders;
        std::vector<MatchingTestClient> other_clients;

        funx::Quantity total_resting_qty{0};
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            other_clients.emplace_back(cfg);
            auto sell = defaultSellOrder();
            sell.qty = funx::Quantity{27};
            resting_orders.push_back(sell);
            total_resting_qty += sell.qty;
        }

        for (int i = 0; i < NUM_CLIENTS; ++i) {
            other_clients[i].addOrderAndExpectEvents(resting_orders[i], {
                AddOrder{
                    .order = resting_orders[i]
                }
            });
            test_listener.expectEvents({
                AddOrder{
                    .order = resting_orders[i]
                }
            });
        }

        auto buy = defaultBuyOrder();
        buy.qty = total_resting_qty + 1;

        uint64_t buy_order_seqnum = test_client.addOrder(buy);

        for (int i = 0; i < NUM_CLIENTS; ++i) {
            auto const& sell = resting_orders[i];
            Match expected_match{
                .product = sell.product,
                .qty = sell.qty,
                .price = sell.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy.company,
                .buyer_order_id = buy.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Sell,
            };
            test_client.expectEvents({
                expected_match
            }, static_cast<uint64_t>(buy_order_seqnum));
            other_clients[i].expectEvents({
                expected_match,
                RemoveOrder{
                    .order_id = sell.order_id
                }
            });
            test_listener.expectEvents({
                expected_match,
                RemoveOrder{
                    .order_id = sell.order_id
                }
            });
        }

        auto finalAdd = AddOrder{
            .order = [&]() {
                auto rtn = buy;
                rtn.qty = funx::Quantity{1};
                return rtn;
             }()
        };

        test_client.expectEvents({
            finalAdd
        }, static_cast<uint64_t>(buy_order_seqnum));
        test_listener.expectEvents({
            finalAdd
        });

        test_client.expectNoEvents();
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            other_clients[i].expectNoEvents();
        }
        test_listener.expectNoEvents();
    }

    test_finished = true;
    server_thread.join();
}

TEST_CASE("Test MatchingService <> TransactionDb Communication", "[match]")
{
    auto TEST_LENGTH = std::chrono::seconds(10);
    funx::transaction::test::RunMockDb transaction_db(
        funx::transaction::test::RunMockDb::Config{
            .max_run_time = TEST_LENGTH,
        }
    );

    const funx::TcpPort MATCHING_SERVICE_PORT{9998};
    const funx::TcpPort LISTEN_PORT{9997};
    MatchingServiceConfig cfg {
        .unique_id = 1,
        .products = {PRODUCT},
        .server_ip4 = "127.0.0.1",
        .server_port = MATCHING_SERVICE_PORT,
        .listen_server_port = LISTEN_PORT,
        .transaction_db_app_cfg = transaction_db.getTransactionDbAppConfig(),
    };

    bool test_finished = false;
    MatchingService matching_service(cfg);

    std::thread server_thread([&]() {
        auto stop = funx::Clock::now() + TEST_LENGTH;
        while (!test_finished && funx::Clock::now() < stop) {
            matching_service.poll();
        }
    });

    MatchingTestClient test_client(cfg);

    for (int i = 0; i < 50; ++i) {
        auto buy = defaultBuyOrder();
        auto sell = defaultSellOrder();
        REQUIRE(buy.qty == sell.qty);

        test_client.addOrderAndExpectEvents(buy, {
            AddOrder{
                .order = buy
            }
        });
        test_client.addOrder(sell);
        auto matching_event = test_client.requireResponse<MatchingEvent>();
        REQUIRE(static_cast<SerializedEvent::EventType>(matching_event.event.type) ==
                SerializedEvent::EventType::Match);
        funx::match::Match match = matching_event.event.data.match;

        std::optional<funx::transaction::Transaction> maybe_tx = transaction_db.storage.query(match.txid);
        REQUIRE(maybe_tx);
        REQUIRE(*maybe_tx ==
            funx::transaction::Transaction{
                .product = match.product,
                .qty = match.qty,
                .price = match.price,
                .txid = match.txid,
                .buyerId = match.buyer_company,
                .sellerId = match.seller_company,
            }
        );

        test_client.expectEvents({
            RemoveOrder{.order_id = buy.order_id}
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    test_finished = true;
    server_thread.join();
}
