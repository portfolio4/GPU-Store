#include <funx/match/test/catch_printers.h>
#include <catch2/catch_all.hpp>

#include <funx/match/Matcher.h>
#include <funx/match/proto.h>
#include <funx/match/test/defaults.h>

#include <expected>
#include <optional>
#include <print>

using namespace funx::match;
using namespace funx::test::defaults::match;

using MaybeEvents = std::expected<
    std::vector<funx::match::Event>,
    funx::match::proto::ErrorCode
>;

void expectEvents(MaybeEvents const& e, std::vector<funx::match::Event> events) {
    REQUIRE(e);
    REQUIRE(events == *e);
}

void expectError(MaybeEvents const& e, funx::match::proto::ErrorCode error) {
    REQUIRE(!e);
    REQUIRE(e.error() == error);
}

TEST_CASE("Test Product Matcher", "[match]")
{
    using funx::match::proto::ErrorCode;

    funx::test::defaults::match::ZeroTxidGenerator txid_generator;
    ProductMatcher::Config cfg{
        .product = funx::test::defaults::match::PRODUCT,
        .txid_generator = &txid_generator,
    };
    ProductMatcher matcher(cfg);

    using funx::test::defaults::match::defaultBuyOrder;
    using funx::test::defaults::match::defaultSellOrder;

    SECTION("Unknown product") {
        auto order = defaultBuyOrder();
        order.product = funx::Product{+cfg.product + 1};
        expectError(matcher.add(order), ErrorCode::UnknownProduct);
    }

    SECTION("Add and remove order") {
        auto order = defaultBuyOrder();
        expectEvents(matcher.add(order), {
            AddOrder{
                .order = order
            }
        });
        expectError(matcher.add(order), ErrorCode::DuplicateOrderId);
        expectEvents(matcher.remove(order.order_id), {
            RemoveOrder{
                .order_id = order.order_id
            }
        });
        expectError(matcher.remove(order.order_id), ErrorCode::OrderNotFound);
    }

    SECTION("Add orders without trading") {
        auto buy = defaultBuyOrder();
        auto sell = defaultSellOrder();

        for (int i = 0; i < 25; ++i) {
            buy.price -= 1;
            sell.price += 1;
            for (int j = 0; j < 25; ++j) {
                buy.order_id = nextOrderId();
                expectEvents(matcher.add(buy), {
                    AddOrder{
                        .order = buy
                    }
                });

                sell.order_id = nextOrderId();
                expectEvents(matcher.add(sell), {
                    AddOrder{
                        .order = sell
                    }
                });
            }
        }
    }

    SECTION("Match two orders fully") {
        auto buy = defaultBuyOrder();
        buy.side = funx::Side::Buy;

        auto sell = defaultSellOrder();
        sell.side = funx::Side::Sell;

        SECTION("Buy then sell") {
            expectEvents(matcher.add(buy), {
                AddOrder{
                    .order = buy
                }
            });
            expectEvents(matcher.add(sell), {
                Match{
                    .product = buy.product,
                    .qty = buy.qty,
                    .price = buy.price,
                    .txid = funx::TransactionId{0},
                    .buyer_company = buy.company,
                    .buyer_order_id = buy.order_id,
                    .seller_company = sell.company,
                    .seller_order_id = sell.order_id,
                    .passive_side = funx::Side::Buy,
                },
                RemoveOrder{
                    .order_id = buy.order_id
                }
            });
        }

        SECTION("Sell then buy") {
            expectEvents(matcher.add(sell), {
                AddOrder{
                    .order = sell
                }
            });
            expectEvents(matcher.add(buy), {
                Match{
                    .product = buy.product,
                    .qty = sell.qty,
                    .price = sell.price,
                    .txid = funx::TransactionId{0},
                    .buyer_company = buy.company,
                    .buyer_order_id = buy.order_id,
                    .seller_company = sell.company,
                    .seller_order_id = sell.order_id,
                    .passive_side = funx::Side::Sell,
                },
                RemoveOrder{
                    .order_id = sell.order_id
                }
            });
        }
    }

    SECTION("Buy, sell, then buy partial fills") {
        auto buy1 = defaultBuyOrder();
        buy1.qty = funx::Quantity{10};
        auto buy2 = defaultBuyOrder();
        buy2.qty = funx::Quantity{20};
        auto buy3 = defaultBuyOrder();
        buy3.qty = funx::Quantity{30};
        buy3.price -= 1;

        auto sell1 = defaultSellOrder();
        sell1.qty = funx::Quantity{15};
        sell1.price -= 1;
        auto sell2 = defaultSellOrder();
        sell2.qty = funx::Quantity{25};
        sell2.price -= 2;
        auto sell3 = defaultSellOrder();
        sell3.qty = funx::Quantity{35};
        sell3.price -= 10;

        auto buy4 = defaultSellOrder();
        buy4.qty = funx::Quantity{25};
        buy4.price += 25;

        expectEvents(matcher.add(buy1), {
            AddOrder{.order = buy1}
        });
        expectEvents(matcher.add(buy2), {
            AddOrder{.order = buy2}
        });
        expectEvents(matcher.add(buy3), {
            AddOrder{.order = buy3}
        });

        // Starting resting bids = [buy1=10, buy2=20, buy3=30]
        // Sell for 15
        // Ending resting bids = [buy2=15, buy3=30]
        expectEvents(matcher.add(sell1), {
            // Buy1 fully fills
            Match{
                .product = buy1.product,
                .qty = buy1.qty,
                .price = buy1.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy1.company,
                .buyer_order_id = buy1.order_id,
                .seller_company = sell1.company,
                .seller_order_id = sell1.order_id,
                .passive_side = funx::Side::Buy,
            },
            // Buy1 gets removed
            RemoveOrder{
                .order_id = buy1.order_id,
            },
            // Buy2 partially fills
            Match{
                .product = buy2.product,
                .qty = funx::Quantity{5},
                .price = buy2.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy2.company,
                .buyer_order_id = buy2.order_id,
                .seller_company = sell1.company,
                .seller_order_id = sell1.order_id,
                .passive_side = funx::Side::Buy,
            }
        });

        // Starting resting bids = [buy2=15, buy3=30]
        // sell for 25
        // Ending resting bids = [buy3=20]
        expectEvents(matcher.add(sell2), {
            // Buy2 remaining quantity is fully filled
            Match{
                .product = buy2.product,
                .qty = funx::Quantity{15},
                .price = buy2.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy2.company,
                .buyer_order_id = buy2.order_id,
                .seller_company = sell2.company,
                .seller_order_id = sell2.order_id,
                .passive_side = funx::Side::Buy,
            },
            RemoveOrder{
                .order_id = buy2.order_id,
            },
            // Buy3 partially fills
            Match{
                .product = buy3.product,
                .qty = funx::Quantity{10},
                .price = buy3.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy3.company,
                .buyer_order_id = buy3.order_id,
                .seller_company = sell2.company,
                .seller_order_id = sell2.order_id,
                .passive_side = funx::Side::Buy,
            }
        });

        // Starting resting bids = [buy3=20]
        // sell for 35
        // Ending resting asks = [sell3=15]
        expectEvents(matcher.add(sell3), {
            // Buy3 remaining quantity is fully filled
            Match{
                .product = buy3.product,
                .qty = funx::Quantity{20},
                .price = buy3.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy3.company,
                .buyer_order_id = buy3.order_id,
                .seller_company = sell3.company,
                .seller_order_id = sell3.order_id,
                .passive_side = funx::Side::Buy,
            },
            RemoveOrder{
                .order_id = buy3.order_id,
            },
            // Remaining sell3 quantity gets added to book
            AddOrder{
                .order = [&]() {
                    auto rtn = sell3;
                    rtn.qty = funx::Quantity{15};
                    return rtn;
                }(),
            }
        });
    }

    SECTION("Buy order crosses many sell orders") {
        auto sell1 = defaultSellOrder();
        sell1.qty = funx::Quantity{10};
        auto sell2 = defaultSellOrder();
        sell2.qty = funx::Quantity{20};
        auto sell3 = defaultSellOrder();
        sell3.qty = funx::Quantity{30};
        sell3.price += 1;
        auto not_filled_sell = defaultSellOrder();
        not_filled_sell.qty = funx::Quantity{1000};
        not_filled_sell.price += 1000;

        auto buy = defaultBuyOrder();
        buy.price += 15;
        buy.qty = funx::Quantity{100};

        expectEvents(matcher.add(sell1), {
            AddOrder{.order = sell1}
        });
        expectEvents(matcher.add(sell2), {
            AddOrder{.order = sell2}
        });
        expectEvents(matcher.add(sell3), {
            AddOrder{.order = sell3}
        });
        expectEvents(matcher.add(not_filled_sell), {
            AddOrder{.order = not_filled_sell}
        });

        expectEvents(matcher.add(buy), {
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
                .order_id = sell1.order_id,
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
                .order_id = sell2.order_id,
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
                .order_id = sell3.order_id,
            },
            AddOrder{
                .order = [&]() {
                    auto rtn = buy;
                    rtn.qty = buy.qty - sell1.qty - sell2.qty - sell3.qty;
                    return rtn;
                }(),
            }
        });
    }

    SECTION("IOC missed completely") {
        auto buy = defaultBuyOrder();
        expectEvents(matcher.add(buy), {
            AddOrder{.order = buy}
        });

        auto sell = defaultSellOrder();
        sell.duration = funx::Duration::IOC;
        sell.price += 20;

        expectEvents(matcher.add(sell), {
            IocMiss{
                .order_id = sell.order_id
            }
        });
    }

    SECTION("IOC fully fills and removes other side") {
        auto buy = defaultBuyOrder();
        expectEvents(matcher.add(buy), {
            AddOrder{.order = buy}
        });

        auto sell = defaultSellOrder();
        sell.duration = funx::Duration::IOC;

        expectEvents(matcher.add(sell), {
            Match{
                .product = buy.product,
                .qty = buy.qty,
                .price = buy.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy.company,
                .buyer_order_id = buy.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
            RemoveOrder{
                .order_id = buy.order_id,
            },
        });
    }

    SECTION("IOC fully fills but does not fully fill other side") {
        auto buy = defaultBuyOrder();
        buy.qty += 20;
        expectEvents(matcher.add(buy), {
            AddOrder{.order = buy}
        });

        auto sell = defaultSellOrder();
        sell.duration = funx::Duration::IOC;

        expectEvents(matcher.add(sell), {
            Match{
                .product = buy.product,
                .qty = sell.qty,
                .price = buy.price,
                .txid = funx::TransactionId{0},
                .buyer_company = buy.company,
                .buyer_order_id = buy.order_id,
                .seller_company = sell.company,
                .seller_order_id = sell.order_id,
                .passive_side = funx::Side::Buy,
            },
        });
    }

    SECTION("Buy IOC crosses many sell orders, remaining quantity misses") {
        auto sell1 = defaultSellOrder();
        sell1.qty = funx::Quantity{10};
        auto sell2 = defaultSellOrder();
        sell2.qty = funx::Quantity{20};
        auto sell3 = defaultSellOrder();
        sell3.qty = funx::Quantity{30};

        expectEvents(matcher.add(sell1), {
            AddOrder{.order = sell1}
        });
        expectEvents(matcher.add(sell2), {
            AddOrder{.order = sell2}
        });
        expectEvents(matcher.add(sell3), {
            AddOrder{.order = sell3}
        });

        auto buy = defaultBuyOrder();
        buy.duration = funx::Duration::IOC;
        buy.price += 15;
        buy.qty = funx::Quantity{100};

        expectEvents(matcher.add(buy), {
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
                .order_id = sell1.order_id,
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
                .order_id = sell2.order_id,
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
                .order_id = sell3.order_id,
            },
            IocMiss{
                .order_id = buy.order_id,
            }
        });
    }
}

