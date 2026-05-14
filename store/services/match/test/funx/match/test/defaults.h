#ifndef FUNX_MATCH_TEST_DEFAULTS_H
#define FUNX_MATCH_TEST_DEFAULTS_H

#include <funx/match/proto.h>
#include <funx/match/TxidGenerator.h>
#include <funx/test/defaults.h>

namespace funx::test::defaults::match {

funx::Product PRODUCT = funx::test::defaults::PRODUCT;
funx::CompanyId BUYER_ID{123};
funx::CompanyId SELLER_ID{456};
funx::Duration DURATION = funx::Duration::Day;

const inline funx::match::Order randomOrder() {
    using funx::test::random_i32;
    using funx::test::random_u32;

    return funx::match::Order{
        .product = funx::Product{random_u32(1, 10000)},
        .qty = funx::Quantity{random_i32(1, 1000)},
        .price = funx::Price{random_i32(-1'000'000, 1'000'000)},
        .side = random_i8() % 2 == 0 ? funx::Side::Buy : funx::Side::Sell,
        .duration = DURATION,
        .company = funx::CompanyId{random_u32()},
        .order_id = funx::OrderId{random_u64()}
    };
}

uint64_t g_next_order_id = +ORDER_ID;

inline funx::OrderId nextOrderId() {
    return funx::OrderId{++g_next_order_id};
}

inline funx::match::Order defaultOrder() {
    return funx::match::Order{
        .product = PRODUCT,
        .qty = QTY,
        .price = PRICE,
        .side = SIDE,
        .duration = DURATION,
        .company = BUYER_ID,
        .order_id = nextOrderId(),
    };
}

inline funx::match::Order defaultBuyOrder() {
    auto order = defaultOrder();
    order.side = funx::Side::Buy;
    order.company = BUYER_ID;
    return order;
}

inline funx::match::Order defaultSellOrder() {
    auto order = defaultOrder();
    order.side = funx::Side::Sell;
    order.company = SELLER_ID;
    return order;
}

// Always returns 0 so that tests don't have to match this value, since
// it doesn't really matter for checking matching logic
class ZeroTxidGenerator : public funx::match::TxidGenerator
{
    funx::TransactionId nextTxid() override {
        return funx::TransactionId{0};
    }
};

} // namespace funx::test::defaults::match

#endif  // FUNX_MATCH_TEST_DEFAULTS_H
