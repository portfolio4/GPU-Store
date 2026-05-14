#include <catch2/catch_all.hpp>

#include <funx/gateway/Client.h>
#include <funx/gateway/Gateway.h>
#include <funx/match/test/mocks.h>
#include <funx/util/overload.h>

#include "gateway_test_client.h"

#include <future>

using namespace funx::gateway;

TEST_CASE("Test Gateway One Matching Service", "[gateway]")
{
    funx::match::test::RunMockMatchingService matching_service;

    SessionConfig sess0_cfg{
        .session_name = "sess0",
        .password = "password0",
        .company_id = funx::CompanyId{1},
        .port = funx::TcpPort{20000},
    };
    SessionConfig sess1_cfg{
        .session_name = "sess1",
        .password = "password1",
        .company_id = funx::CompanyId{1},
        .port = funx::TcpPort{20001},
    };
    GatewayConfig cfg{
        .unique_id = 0,
        .server_ip4 = "127.0.0.1",
        .matching_services = { matching_service.getMatchingServiceConfig() },
        .sessions = {sess0_cfg, sess1_cfg},
    };

    bool test_finished = false;
    Gateway gateway(cfg);
    std::thread server_thread([&]() {
        auto stop = funx::Clock::now() + std::chrono::seconds(5);
        while (!test_finished && funx::Clock::now() < stop) {
            gateway.poll();
        }
    });

    SECTION("Test bad login") {
        SECTION("Invalid session name") {
            auto bad_sess_cfg = cfg.sessions[0];
            bad_sess_cfg.session_name = "obviously wrong";
            GatewayTestClient bad_sess_name(Client::Config{
                .gateway_cfg = cfg,
                .session_cfg = bad_sess_cfg,
            });
            bad_sess_name.client().sendLogin();
            auto login_resp = bad_sess_name.requireResponse<flop::LoginResponse>();
            REQUIRE(login_resp.reason == flop::Reason::IncorrectSessionName);
        }

        SECTION("Invalid password") {
            auto bad_sess_cfg = cfg.sessions[0];
            bad_sess_cfg.password = "obviously wrong";
            GatewayTestClient bad_password(Client::Config{
                .gateway_cfg = cfg,
                .session_cfg = bad_sess_cfg,
            });
            bad_password.client().sendLogin();
            auto login_resp = bad_password.requireResponse<flop::LoginResponse>();
            REQUIRE(login_resp.reason == flop::Reason::IncorrectPassword);
        }

        SECTION("Cannot connect multiple times") {
            GatewayTestClient first_connect(Client::Config{
                .gateway_cfg = cfg,
                .session_cfg = cfg.sessions[0],
            });
            GatewayTestClient second_connect(Client::Config{
                .gateway_cfg = cfg,
                .session_cfg = cfg.sessions[0],
            });
            second_connect.client().sendLogin();
            second_connect.requireNoResponse();
            first_connect.client().sendLogin();
            auto login_resp = first_connect.requireResponse<flop::LoginResponse>();
            REQUIRE(login_resp.reason == flop::Reason::NoError);

            first_connect.client().sendLogout();
            auto logout_resp = first_connect.requireResponse<flop::LogoutResponse>();
            REQUIRE(logout_resp.reason == flop::Reason::NoError);
        }
    }

    // ---------- All tests after this will use logged in clients ---------- //

    GatewayTestClient sess0(Client::Config{
        .gateway_cfg = cfg,
        .session_cfg = cfg.sessions[0],
    });
    {
        sess0.client().sendLogin();
        auto login_resp = sess0.requireResponse<flop::LoginResponse>();
        REQUIRE(login_resp.reason == flop::Reason::NoError);
    }

    GatewayTestClient sess1(Client::Config{
        .gateway_cfg = cfg,
        .session_cfg = cfg.sessions[1],
    });
    {
        sess1.client().sendLogin();
        auto login_resp = sess1.requireResponse<flop::LoginResponse>();
        REQUIRE(login_resp.reason == flop::Reason::NoError);
    }

    SECTION("Send order and get response back") {
        flop::NewOrder new_order{
            .product = 1,
            .side = flop::Side::Sell,
            .qty = 123,
            .price = 456,
            .duration = flop::Duration::Day,
            .client_order_id = 1234567,
        };
        sess0.client().sendBasicNewOrder(new_order);

        auto order_ack = sess0.requireResponse<flop::OrderAccepted>();
        GatewayTestClient::requireAckMatchesOrder(order_ack, new_order);
    }

    SECTION("Send order and get reject") {
        flop::NewOrder new_order{
            .product = 1,
            .side = flop::Side::Sell,
            .qty = 123,
            .price = 456,
            .duration = flop::Duration::Day,
            .client_order_id = 1234567,
        };

        auto sendAndExpectReject = [&](flop::Reason reason) {
            sess0.client().sendBasicNewOrder(new_order);
            auto reject = sess0.requireResponse<flop::OrderReject>();
            REQUIRE(reject.reason == reason);
        };

        SECTION("Bad product") {
            new_order.product = 843578;
            sendAndExpectReject(flop::Reason::InvalidProduct);
        }

        SECTION("Bad Side") {
            new_order.side = static_cast<flop::Side>(255);
            sendAndExpectReject(flop::Reason::InvalidSide);
        }

        SECTION("Bad quantity") {
            new_order.qty = GENERATE(-100, 0);
            sendAndExpectReject(flop::Reason::InvalidQuantity);
        }

        SECTION("Bad Duration") {
            new_order.duration = static_cast<flop::Duration>(255);
            sendAndExpectReject(flop::Reason::InvalidDuration);
        }

        SECTION("Duplicate client order ID") {
            // send first order and it should be accepted
            sess0.client().sendBasicNewOrder(new_order);
            sess0.requireResponse<flop::OrderAccepted>();

            // send second order with same client order ID and it should be rejected
            sendAndExpectReject(flop::Reason::DuplicateClientOrderId);
        }
    }

    SECTION("Generate trades") {
        flop::NewOrder buy{
            .product = 1,
            .side = flop::Side::Buy,
            .qty = 100,
            .price = 456,
            .duration = flop::Duration::Day,
            .client_order_id = 1234567,
        };
        flop::NewOrder sell{
            .product = buy.product,
            .side = flop::Side::Sell,
            .qty = buy.qty,
            .price = buy.price,
            .duration = flop::Duration::Day,
            .client_order_id = 1234568,
        };

        SECTION("Same session") {
            sess0.client().sendBasicNewOrder(buy);
            auto ack_buy = sess0.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);

            sess0.client().sendBasicNewOrder(sell);

            // aggressor trade is reported first
            auto ack_sell = sess0.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_sell, sell);

            auto trade_sell = sess0.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_sell, ack_sell);
            REQUIRE(trade_sell.qty == sell.qty);
            REQUIRE(trade_sell.price == sell.price);

            auto remove_sell = sess0.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_sell, ack_sell);

            // passive trade reported second
            auto trade_buy = sess0.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_buy, ack_buy);
            REQUIRE(trade_buy.qty == buy.qty);
            REQUIRE(trade_buy.price == buy.price);

            auto remove_buy = sess0.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_buy, ack_buy);
        }

        SECTION("Different sessions") {
            sess1.client().sendBasicNewOrder(sell);
            auto ack_sell = sess1.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_sell, sell);

            sess0.client().sendBasicNewOrder(buy);
            auto ack_buy = sess0.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);

            auto trade_sell = sess1.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_sell, ack_sell);
            REQUIRE(trade_sell.qty == sell.qty);
            REQUIRE(trade_sell.price == sell.price);

            auto trade_buy = sess0.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_buy, ack_buy);
            REQUIRE(trade_buy.qty == buy.qty);
            REQUIRE(trade_buy.price == buy.price);

            auto remove_sell = sess1.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_sell, ack_sell);

            auto remove_buy = sess0.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_buy, ack_buy);
        }

        SECTION("Aggressive order gets partially filled, Passive order stays") {
            sell.qty = buy.qty - 1;
            sell.price -= 1;

            sess0.client().sendBasicNewOrder(buy);
            auto ack_buy = sess0.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);

            sess1.client().sendBasicNewOrder(sell);
            auto ack_sell = sess1.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_sell, sell);
            auto trade_sell = sess1.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_sell, ack_sell);
            REQUIRE(trade_sell.qty == sell.qty);
            REQUIRE(trade_sell.price == buy.price);
            auto remove_sell = sess1.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_sell, ack_sell);

            auto trade_buy = sess0.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_buy, ack_buy);
            REQUIRE(trade_buy.qty == sell.qty);
            REQUIRE(trade_buy.price == buy.price);
        }

        SECTION("Both orders get fully filled") {
            sell.qty = buy.qty;

            sess0.client().sendBasicNewOrder(buy);
            auto ack_buy = sess0.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);

            sess1.client().sendBasicNewOrder(sell);
            auto ack_sell = sess1.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_sell, sell);
            auto trade_sell = sess1.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_sell, ack_sell);
            REQUIRE(trade_sell.qty == sell.qty);
            REQUIRE(trade_sell.price == buy.price);
            auto remove_sell = sess1.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_sell, ack_sell);

            auto trade_buy = sess0.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_buy, ack_buy);
            REQUIRE(trade_buy.qty == sell.qty);
            REQUIRE(trade_buy.price == buy.price);
            auto remove_buy = sess0.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_buy, ack_buy);
        }

        SECTION("Passive order fully filled, aggressive order stays") {
            sell.qty = buy.qty + 1;

            sess0.client().sendBasicNewOrder(buy);
            auto ack_buy = sess0.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);

            sess1.client().sendBasicNewOrder(sell);
            auto ack_sell = sess1.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_sell, sell);
            auto trade_sell = sess1.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_sell, ack_sell);
            REQUIRE(trade_sell.qty == buy.qty);
            REQUIRE(trade_sell.price == buy.price);
            sess1.requireNoResponse();

            auto trade_buy = sess0.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_buy, ack_buy);
            REQUIRE(trade_buy.qty == buy.qty);
            REQUIRE(trade_buy.price == buy.price);
            auto remove_buy = sess0.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_buy, ack_buy);
        }
    }

    SECTION("Cancel order") {
        flop::NewOrder buy{
            .product = 1,
            .side = flop::Side::Buy,
            .qty = 100,
            .price = 456,
            .duration = flop::Duration::Day,
            .client_order_id = 1234567,
        };
        flop::NewOrder sell{
            .product = buy.product,
            .side = flop::Side::Sell,
            .qty = buy.qty,
            .price = buy.price,
            .duration = flop::Duration::Day,
            .client_order_id = 1234568,
        };

        sess0.client().sendBasicNewOrder(buy);
        auto ack_buy = sess0.requireResponse<flop::OrderAccepted>();
        GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);

        sess0.client().sendCancelOrder(flop::CancelOrder{
            .client_order_id = buy.client_order_id,
        });
        auto remove_buy = sess0.requireResponse<flop::OrderRemoved>();
        GatewayTestClient::requireRemoveMatchesAck(remove_buy, ack_buy);

        sess1.client().sendBasicNewOrder(sell);
        auto ack_sell = sess1.requireResponse<flop::OrderAccepted>();
        GatewayTestClient::requireAckMatchesOrder(ack_sell, sell);

        sess1.client().sendCancelOrder(flop::CancelOrder{
            .client_order_id = sell.client_order_id,
        });
        auto remove_sell = sess1.requireResponse<flop::OrderRemoved>();
        GatewayTestClient::requireRemoveMatchesAck(remove_sell, ack_sell);

        // canceling again should result in rejects
        sess0.client().sendCancelOrder(flop::CancelOrder{
            .client_order_id = buy.client_order_id,
        });
        auto reject_cancel_buy = sess0.requireResponse<flop::CancelReject>();
        GatewayTestClient::requireCancelRejectMatchesOrder(reject_cancel_buy, buy);
        REQUIRE(reject_cancel_buy.reason == flop::Reason::CancelClientOrderIdNotFound);

        sess1.client().sendCancelOrder(flop::CancelOrder{
            .client_order_id = sell.client_order_id,
        });
        auto reject_cancel_sell = sess1.requireResponse<flop::CancelReject>();
        GatewayTestClient::requireCancelRejectMatchesOrder(reject_cancel_sell, sell);
        REQUIRE(reject_cancel_sell.reason == flop::Reason::CancelClientOrderIdNotFound);
    }

    SECTION("Cannot cancel order on other sessions") {
        flop::NewOrder order{
            .product = 1,
            .side = flop::Side::Buy,
            .qty = 100,
            .price = 456,
            .duration = flop::Duration::Day,
            .client_order_id = 1234567,
        };

        sess0.client().sendBasicNewOrder(order);
        auto ack = sess0.requireResponse<flop::OrderAccepted>();
        GatewayTestClient::requireAckMatchesOrder(ack, order);

        sess1.client().sendCancelOrder(flop::CancelOrder{
            .client_order_id = order.client_order_id,
        });
        auto reject_cancel = sess1.requireResponse<flop::CancelReject>();
        GatewayTestClient::requireCancelRejectMatchesOrder(reject_cancel, order);
        REQUIRE(reject_cancel.reason == flop::Reason::CancelClientOrderIdNotFound);

        sess0.client().sendCancelOrder(flop::CancelOrder{
            .client_order_id = order.client_order_id,
        });
        auto remove = sess0.requireResponse<flop::OrderRemoved>();
        GatewayTestClient::requireRemoveMatchesAck(remove, ack);
    }

    SECTION("IOC orders") {
        flop::NewOrder buy{
            .product = 1,
            .side = flop::Side::Buy,
            .qty = 100,
            .price = 456,
            .duration = flop::Duration::Day,
            .client_order_id = 1234567,
        };
        flop::NewOrder sell_ioc{
            .product = buy.product,
            .side = flop::Side::Sell,
            .qty = buy.qty,
            .price = buy.price,
            .duration = flop::Duration::ImmediateOrCancel,
            .client_order_id = 1234568,
        };

        SECTION("IOC Misses Completely") {
            sess0.client().sendBasicNewOrder(sell_ioc);
            auto ack_sell = sess0.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_sell, sell_ioc);
            auto remove_sell = sess0.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_sell, ack_sell);

            buy.duration = flop::Duration::ImmediateOrCancel;
            sess1.client().sendBasicNewOrder(buy);
            auto ack_buy = sess1.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);
            auto remove_buy = sess1.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_buy, ack_buy);
        }

        SECTION("IOC Fills Either Fully or Partially") {
            // in both cases, we fully take out the opposing order, so the
            // events that the client receives is the same.
            // However, on the matching service side,
            //    - buy.qty leads to a single Trade event which the gateway
            //      should synthesize both Accept and Remove for
            //    - 2 * buy.qty leads to a Trade event, which the gateway
            //      synthesizes just an Accept. Then an IocMiss, which the
            //      gateway translates to a Remove
            sell_ioc.qty = GENERATE_COPY(buy.qty, 2 * buy.qty);

            sess0.client().sendBasicNewOrder(buy);
            auto ack_buy = sess0.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);

            sess1.client().sendBasicNewOrder(sell_ioc);
            auto ack_sell = sess1.requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack_sell, sell_ioc);
            auto trade_sell = sess1.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_sell, ack_sell);
            REQUIRE(trade_sell.qty == buy.qty);
            REQUIRE(trade_sell.price == buy.price);
            auto remove_sell = sess1.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_sell, ack_sell);

            auto trade_buy = sess0.requireResponse<flop::Trade>();
            GatewayTestClient::requireTradeMatchesAck(trade_buy, ack_buy);
            REQUIRE(trade_sell.qty == buy.qty);
            REQUIRE(trade_sell.price == buy.price);
            auto remove_buy = sess0.requireResponse<flop::OrderRemoved>();
            GatewayTestClient::requireRemoveMatchesAck(remove_buy, ack_buy);
        }
    }

    sess0.client().sendLogout();
    auto logout_resp0 = sess0.requireResponse<flop::LogoutResponse>();
    REQUIRE(logout_resp0.reason == flop::Reason::NoError);

    sess1.client().sendLogout();
    auto logout_resp1 = sess1.requireResponse<flop::LogoutResponse>();
    REQUIRE(logout_resp1.reason == flop::Reason::NoError);

    test_finished = true;
    server_thread.join();
}

TEST_CASE("Orders are rejected when matching service is disconnected", "[gateway]")
{
    auto matching_service = std::make_unique<funx::match::test::RunMockMatchingService>();
    auto matching_service_cfg = matching_service->getMatchingServiceConfig();

    SessionConfig session_cfg{
        .session_name = "sess0",
        .password = "password0",
        .company_id = funx::CompanyId{1},
        .port = funx::TcpPort{20000},
    };
    GatewayConfig gateway_cfg{
        .unique_id = 0,
        .server_ip4 = "127.0.0.1",
        .matching_services = {matching_service_cfg},
        .sessions = {session_cfg},
    };
    Gateway gateway(gateway_cfg);
    GatewayTestClient session(Client::Config{
        .gateway_cfg = gateway_cfg,
        .session_cfg = session_cfg,
    });

    session.client().sendLogin();
    auto login = session.requireResponse<flop::LoginResponse>([&]() { gateway.poll(); });
    REQUIRE(login.reason == flop::Reason::NoError);

    matching_service.reset();
    gateway.poll();

    flop::NewOrder order{
        .product = 1,
        .side = flop::Side::Buy,
        .qty = 100,
        .price = 456,
        .duration = flop::Duration::Day,
        .client_order_id = 1234567,
    };
    session.client().sendBasicNewOrder(order);
    auto reject = session.requireResponse<flop::OrderReject>([&]() { gateway.poll(); });
    REQUIRE(reject.client_order_id == order.client_order_id);
    REQUIRE(reject.reason == flop::Reason::InternalError);

    matching_service = std::make_unique<funx::match::test::RunMockMatchingService>();
    auto reconnect_deadline = funx::SteadyClock::now() + 2 * funx::match::Client::RECONNECT_INTERVAL;
    while (funx::SteadyClock::now() < reconnect_deadline) {
        gateway.poll();
    }

    order.client_order_id += 1;
    session.client().sendBasicNewOrder(order);
    auto accepted = session.requireResponse<flop::OrderAccepted>([&]() { gateway.poll(); });
    GatewayTestClient::requireAckMatchesOrder(accepted, order);
}

TEST_CASE("3 Gateways, 3 Matching Services", "[gateway]")
{
    const uint8_t NUM_MATCHING_SERVICES = 3;
    const uint8_t NUM_GWS = 3;
    const uint8_t NUM_SESSIONS_PER_GW = 3;
    const int NUM_PRODUCTS_PER_MATCHING_SERVICE = 3;

    // Allow longer runtimes and timeouts so that we can also run under
    // valgrind and ensure no problems
    const auto MAX_RUNTIME = std::chrono::seconds(300);
    const auto RECEIVE_TIMEOUT = std::chrono::seconds(10);

    uint64_t nextProductId = 100;
    std::list<funx::match::test::RunMockMatchingService> matching_services;
    for (uint8_t i = 0; i < NUM_MATCHING_SERVICES; ++i) {
        std::vector<funx::Product> products;
        for (int i = 0; i < NUM_PRODUCTS_PER_MATCHING_SERVICE; ++i) {
            products.push_back(funx::Product{nextProductId++});
        }
        matching_services.emplace_back(funx::match::test::RunMockMatchingService::Config{
            .unique_id = i,
            .products = products,
            .server_port = funx::TcpPort{static_cast<uint16_t>(9000 + 100u*i + 1)},
            .listen_server_port = funx::TcpPort{static_cast<uint16_t>(9000 + 100u*i + 2)},
            .max_run_time = MAX_RUNTIME,
            .mock_db_cfg = funx::transaction::test::RunMockDb::Config{
                .server_port = funx::TcpPort{static_cast<uint16_t>(9000 + 100u*i + 0)},
                .max_run_time = MAX_RUNTIME,
            }
        });
    }
    std::vector<funx::match::MatchingServiceConfig> matching_service_cfgs;
    for (auto const& s : matching_services) {
        matching_service_cfgs.push_back(s.matching_service_cfg);
    }

    uint16_t nextSessPort = 20000;
    uint64_t nextCompanyId = 1000;
    std::vector<GatewayConfig> gateway_cfgs;
    for (uint8_t gw_id = 0; gw_id < NUM_GWS; ++gw_id) {
        std::vector<SessionConfig> sess_cfgs;
        for (uint8_t sess_id = 0; sess_id < NUM_SESSIONS_PER_GW; ++sess_id) {
            sess_cfgs.push_back(SessionConfig{
                .session_name = std::format("gw{}_sess{}", gw_id, sess_id),
                .password = "password",
                .company_id = funx::CompanyId{nextCompanyId++},
                .port = funx::TcpPort{nextSessPort++},
            });
        }
        gateway_cfgs.push_back(GatewayConfig{
            .unique_id = gw_id,
            .server_ip4 = "127.0.0.1",
            .matching_services = matching_service_cfgs,
            .sessions = sess_cfgs,
        });
    }

    std::list<Gateway> gateways;
    for (uint8_t i = 0; i < NUM_GWS; ++i) {
        gateways.emplace_back(gateway_cfgs[i]);
    }

    bool test_finished = false;
    auto stop = funx::Clock::now() + MAX_RUNTIME;
    std::vector<std::thread> gw_threads;
    for (auto& gw : gateways) {
        gw_threads.emplace_back(std::thread([&]() {
            while (!test_finished && funx::Clock::now() < stop) {
                gw.poll();
            }
        }));
    }

    std::list<GatewayTestClient> client_list;
    std::vector<GatewayTestClient*> clients;
    for (auto const& gw_cfg : gateway_cfgs) {
        for (auto const& sess_cfg : gw_cfg.sessions) {
            client_list.emplace_back(Client::Config{
                .gateway_cfg = gw_cfg,
                .session_cfg = sess_cfg,
            });
            client_list.back().setResponseTimeout(RECEIVE_TIMEOUT);
            clients.push_back(&client_list.back());
        }
    }

    for (auto& client : client_list) {
        client.client().sendLogin();
        auto login_resp = client.requireResponse<flop::LoginResponse>();
        REQUIRE(login_resp.reason == flop::Reason::NoError);
    }


    uint64_t next_client_order_id = 1234567;

    SECTION("Send product to each matching service") {
        for (auto const& match_cfg : matching_service_cfgs) {
            for (funx::Product p : match_cfg.products) {
                flop::NewOrder buy{
                    .product = +p,
                    .side = flop::Side::Buy,
                    .qty = 100,
                    .price = 456,
                    .duration = flop::Duration::Day,
                    .client_order_id = next_client_order_id++,
                };
                clients[0]->client().sendBasicNewOrder(buy);
                auto ack = clients[0]->requireResponse<flop::OrderAccepted>();
                GatewayTestClient::requireAckMatchesOrder(ack, buy);
            }
        }
    }

    SECTION("Send to every session") {
        for (GatewayTestClient* client : clients) {
            flop::NewOrder sell{
                .product = +matching_service_cfgs[0].products[0],
                .side = flop::Side::Sell,
                .qty = 100,
                .price = 456,
                .duration = flop::Duration::Day,
                .client_order_id = next_client_order_id++,
            };
            client->client().sendBasicNewOrder(sell);
            auto ack = client->requireResponse<flop::OrderAccepted>();
            GatewayTestClient::requireAckMatchesOrder(ack, sell);
        }
    }

    SECTION("Order on one session crosses order on another session") {
        // We'll test every pair of gateways. We could do every pair of sessions
        // but that makes the valgrind runs take very long
        std::set<uint16_t> visited_keys;
        for (GatewayTestClient* client1 : clients) {
            for (GatewayTestClient* client2 : clients) {
                // Top 8 bytes of key is the first client's gw id.
                // Bottom 8 bytes of key is the second client's gw id.
                // If we've seen this key before, then we've already tested
                // a session on this first gateway crossing a session the second
                // gateway and can skip
                //
                // This lets us test every gateway crossing every other gateway
                const auto gw1_id = client1->client().config().gateway_cfg.unique_id;
                const auto gw2_id = client2->client().config().gateway_cfg.unique_id;

                // The gateways have to be different, else we might risk
                // the same session crossing itself. The Add, Trade, Remove of
                // the two orders will be interleaved in a specific manner
                // and we don't want to complicate the test with that logic.
                // This interleaving is already tested in the single gateway
                // tests earlier
                if (gw1_id == gw2_id) {
                    continue;
                }
                const auto key = (gw1_id << 8) | gw2_id;
                if (visited_keys.contains(key)) {
                    continue;
                }
                visited_keys.insert(key);

                flop::NewOrder buy{
                    .product = +matching_service_cfgs[0].products[0],
                    .side = flop::Side::Buy,
                    .qty = 100,
                    .price = 456,
                    .duration = flop::Duration::Day,
                    .client_order_id = next_client_order_id++,
                };
                flop::NewOrder sell{
                    .product = +buy.product,
                    .side = flop::Side::Sell,
                    .qty = buy.qty,
                    .price = buy.price,
                    .duration = flop::Duration::Day,
                    .client_order_id = next_client_order_id++,
                };

                client1->client().sendBasicNewOrder(buy);
                auto ack_buy = client1->requireResponse<flop::OrderAccepted>();
                GatewayTestClient::requireAckMatchesOrder(ack_buy, buy);

                client2->client().sendBasicNewOrder(sell);
                auto ack_sell = client2->requireResponse<flop::OrderAccepted>();
                GatewayTestClient::requireAckMatchesOrder(ack_sell, sell);

                auto trade_buy = client1->requireResponse<flop::Trade>();
                GatewayTestClient::requireTradeMatchesAck(trade_buy, ack_buy);
                REQUIRE(trade_buy.qty == buy.qty);
                REQUIRE(trade_buy.price == buy.price);

                auto trade_sell = client2->requireResponse<flop::Trade>();
                GatewayTestClient::requireTradeMatchesAck(trade_sell, ack_sell);
                REQUIRE(trade_sell.qty == sell.qty);
                REQUIRE(trade_sell.price == sell.price);

                auto remove_buy = client1->requireResponse<flop::OrderRemoved>();
                GatewayTestClient::requireRemoveMatchesAck(remove_buy, ack_buy);

                auto remove_sell = client2->requireResponse<flop::OrderRemoved>();
                GatewayTestClient::requireRemoveMatchesAck(remove_sell, ack_sell);
            }
        }
    }

    test_finished = true;
    for (std::thread& t : gw_threads) {
        t.join();
    }
}

TEST_CASE("Test Gateway Auto Cancel", "[debug]")
{
    funx::match::test::RunMockMatchingService matching_service;

    const uint32_t HEARTBEAT_TIMEOUT_SECONDS = 5;
    const std::chrono::seconds HEARTBEAT_TIMEOUT(HEARTBEAT_TIMEOUT_SECONDS);

    SessionConfig sess0_cfg{
        .session_name = "sess0",
        .password = "password0",
        .company_id = funx::CompanyId{1},
        .port = funx::TcpPort{20000},
        .heartbeat_timeout_seconds = 5,
    };
    SessionConfig sess1_cfg{
        .session_name = "sess1",
        .password = "password1",
        .company_id = funx::CompanyId{1},
        .port = funx::TcpPort{20001},
        .heartbeat_timeout_seconds = 5,
    };
    GatewayConfig cfg{
        .unique_id = 0,
        .server_ip4 = "127.0.0.1",
        .matching_services = { matching_service.getMatchingServiceConfig() },
        .sessions = {sess0_cfg, sess1_cfg},
    };

    Gateway gateway(cfg);

    std::future<GatewayTestClient> sess0_fut = std::async(std::launch::async, [&]() {
        return GatewayTestClient(Client::Config{
            .gateway_cfg = cfg,
            .session_cfg = cfg.sessions[0],
        });
    });
    std::future<GatewayTestClient> sess1_fut = std::async(std::launch::async, [&]() {
        return GatewayTestClient(Client::Config{
            .gateway_cfg = cfg,
            .session_cfg = cfg.sessions[1],
        });
    });

    funx::Clock::time_point stop = funx::Clock::now() + std::chrono::seconds(1);
    while (funx::Clock::now() < stop &&
            (sess0_fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
             sess1_fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready))
    {
        gateway.poll();
    }

    auto login_time = funx::Clock::now();
    GatewayTestClient sess0 = sess0_fut.get();
    {
        sess0.client().sendLogin();
        gateway.poll(login_time);
        auto login_resp = sess0.requireResponse<flop::LoginResponse>();
        REQUIRE(login_resp.reason == flop::Reason::NoError);
    }

    GatewayTestClient sess1 = sess1_fut.get();
    {
        sess1.client().sendLogin();
        gateway.poll(login_time);
        auto login_resp = sess1.requireResponse<flop::LoginResponse>();
        REQUIRE(login_resp.reason == flop::Reason::NoError);
    }


    flop::NewOrder new_order{
        .product = 1,
        .side = flop::Side::Sell,
        .qty = 123,
        .price = 456,
        .duration = flop::Duration::Day,
        .client_order_id = 1234567,
    };

    SECTION("Orders are cancelled after heartbeat timeout") {
        auto start_time = login_time;

        // session 0 sends order at start time
        sess0.client().sendBasicNewOrder(new_order);
        auto ack0 = sess0.requireResponse<flop::OrderAccepted>([&]() {
            gateway.poll(start_time);
        });
        GatewayTestClient::requireAckMatchesOrder(ack0, new_order);

        // session 1 sends order 1 ns after start time
        sess1.client().sendBasicNewOrder(new_order);
        auto ack1 = sess1.requireResponse<flop::OrderAccepted>([&]() {
            gateway.poll(start_time + std::chrono::nanoseconds(1));
        });
        GatewayTestClient::requireAckMatchesOrder(ack1, new_order);

        auto sess0_hb_timeout = start_time + HEARTBEAT_TIMEOUT;
        // After start time + heartbeat timeout, session 0 should hit heartbeat
        // timeout and everything gets cancelled. However, session 1 should not
        // hit heartbeat timeout because it still has 1 nanosecond left
        auto remove0 = sess0.requireResponse<flop::OrderRemoved>([&]() {
            gateway.poll(sess0_hb_timeout);
        });
        GatewayTestClient::requireRemoveMatchesAck(remove0, ack0);
        sess1.requireNoResponse([&]() {
            gateway.poll(sess0_hb_timeout);
        });

        // Session 1 order should not get cancelled for another HEARTBEAT_TIMEOUT
        // interval because it sent a heartbeat just in time
        sess1.client().sendHeartbeat();
        gateway.poll(sess0_hb_timeout);

        auto sess1_one_hb_interval = sess0_hb_timeout + std::chrono::nanoseconds(1);
        sess1.requireNoResponse([&]() {
            gateway.poll(sess1_one_hb_interval);
        });

        auto sess1_hb_timeout = sess1_one_hb_interval + HEARTBEAT_TIMEOUT;
        auto remove1 = sess1.requireResponse<flop::OrderRemoved>([&]() {
            gateway.poll(sess1_hb_timeout);
        });
        GatewayTestClient::requireRemoveMatchesAck(remove1, ack1);
    }

    SECTION("Orders are cancelled on TCP disconnect") {
        sess0.client().sendBasicNewOrder(new_order);
        auto ack0 = sess0.requireResponse<flop::OrderAccepted>([&]() {
            gateway.poll(login_time);
        });
        GatewayTestClient::requireAckMatchesOrder(ack0, new_order);

        // orders should get cancelled now
        sess0.client().close();
        gateway.poll(login_time);

        // try to cross the order that should've been cancelled
        auto new_order_cross = new_order;
        new_order_cross.side = flop::Side::Buy;
        new_order_cross.duration = flop::Duration::ImmediateOrCancel;
        sess1.client().sendBasicNewOrder(new_order_cross);
        auto ack1 = sess1.requireResponse<flop::OrderAccepted>([&]() {
            gateway.poll(login_time);
        });
        GatewayTestClient::requireAckMatchesOrder(ack1, new_order_cross);
        auto remove1 = sess1.requireResponse<flop::OrderRemoved>([&]() {
            gateway.poll(login_time);
        });
        GatewayTestClient::requireRemoveMatchesAck(remove1, ack1);

        // Should be able to reuse a client order ID on session 0 again
        std::future<GatewayTestClient> sess0_reconnect_fut = std::async(std::launch::async, [&]() {
            return GatewayTestClient(Client::Config{
                .gateway_cfg = cfg,
                .session_cfg = cfg.sessions[0],
            });
        });
        auto stop = funx::Clock::now() + std::chrono::seconds(1);
        while (funx::Clock::now() < stop &&
                (sess0_reconnect_fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready))
        {
            gateway.poll();
        }
        GatewayTestClient sess0_reconnect = sess0_reconnect_fut.get();
        {
            sess0_reconnect.client().sendLogin();
            gateway.poll();
            auto login_resp = sess0_reconnect.requireResponse<flop::LoginResponse>();
            REQUIRE(login_resp.reason == flop::Reason::NoError);
        }
        sess0_reconnect.client().sendBasicNewOrder(new_order);
        auto ack_reconnect = sess0_reconnect.requireResponse<flop::OrderAccepted>([&]() {
            gateway.poll();
        });
        GatewayTestClient::requireAckMatchesOrder(ack_reconnect, new_order);
    }

    SECTION("Orders are cancelled on logout") {
        sess0.client().sendBasicNewOrder(new_order);
        auto ack0 = sess0.requireResponse<flop::OrderAccepted>([&]() {
            gateway.poll(login_time);
        });
        GatewayTestClient::requireAckMatchesOrder(ack0, new_order);

        sess0.client().sendLogout();
        auto logout = sess0.requireResponse<flop::LogoutResponse>([&]() {
            gateway.poll(login_time);
        });
        REQUIRE(logout.reason == flop::Reason::NoError);

        // A crossing IOC should miss because logout cancelled the resting sell.
        auto crossing_order = new_order;
        crossing_order.side = flop::Side::Buy;
        crossing_order.duration = flop::Duration::ImmediateOrCancel;
        sess1.client().sendBasicNewOrder(crossing_order);

        auto ack1 = sess1.requireResponse<flop::OrderAccepted>([&]() {
            gateway.poll(login_time);
        });
        GatewayTestClient::requireAckMatchesOrder(ack1, crossing_order);

        bool got_remove = false;
        auto stop = funx::Clock::now() + std::chrono::seconds(1);
        while (!got_remove && funx::Clock::now() < stop) {
            sess1.client().poll(funx::overloaded{
                [&](flop::MessageT<flop::OrderRemoved> const& msg) {
                    GatewayTestClient::requireRemoveMatchesAck(msg.body, ack1);
                    got_remove = true;
                },
                [&](flop::MessageT<flop::Trade> const&) {
                    FAIL("Logged-out session left a live order in the book");
                },
                [](auto&&) {}
            });
            gateway.poll(login_time);
        }
        REQUIRE(got_remove);
    }
}
