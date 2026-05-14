#ifndef FUNX_TRANSACTION_TEST_DEFAULTS_H
#define FUNX_TRANSACTION_TEST_DEFAULTS_H

#include <funx/test/defaults.h>
#include <funx/transaction/proto.h>

namespace funx::test::defaults::transaction {

const inline funx::transaction::Transaction TRANSACTION {
    .product = PRODUCT,
    .qty = QTY,
    .price = PRICE,
    .txid = TRANSACTION_ID,
    .buyerId = BUYER_ID,
    .sellerId = SELLER_ID,
};

const inline funx::transaction::Transaction randomTransaction() {
    using funx::test::random_i32;
    using funx::test::random_u32;

    return funx::transaction::Transaction{
        .product = funx::Product{random_u32(1, 10000)},
        .qty = funx::Quantity{random_i32(1, 1000)},
        .price = funx::Price{random_i32(-1'000'000, 1'000'000)},
        .txid = funx::TransactionId{random_u32(0, 1'000'000'000)},
        .buyerId = funx::CompanyId{random_u32(1, 1'000'000)},
        .sellerId = funx::CompanyId{random_u32(1, 1'000'000)},
    };
}

} // namespace funx::test::defaults::transaction

#endif  // FUNX_TRANSACTION_TEST_DEFAULTS_H
