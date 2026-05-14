#ifndef FUNX_TEST_DEFAULTS_H
#define FUNX_TEST_DEFAULTS_H

#include <funx/common/types.h>
#include <funx/test/util.h>

namespace funx::test::defaults {

const inline funx::Product PRODUCT{1628};
const inline funx::Quantity QTY{128};
const inline funx::Price PRICE{293};
const inline funx::Side SIDE = funx::Side::Buy;

const inline funx::TransactionId TRANSACTION_ID{84378};
const inline funx::CompanyId BUYER_ID{1938};
const inline funx::CompanyId SELLER_ID{948};

const inline funx::OrderId ORDER_ID{13579};

} // namespace funx::test::defaults

#endif  // FUNX_TEST_DEFAULTS_H

