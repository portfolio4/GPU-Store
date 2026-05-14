#include <funx/match/Matcher.h>

#include <funx/common/types.h>
#include <funx/util/expected.h>

#include <cassert>
#include <print>

namespace funx::match {

namespace {

    Order const& getBuyOrderInMatch(Order const& o1, Order const& o2) {
        assert(o1.side != o2.side);
        return o1.side == funx::Side::Buy ? o1 : o2;
    }

    Order const& getSellOrderInMatch(Order const& o1, Order const& o2) {
        assert(o1.side != o2.side);
        return o1.side == funx::Side::Sell ? o1 : o2;
    }
} // namespace

ProductMatcher::ProductMatcher(Config const& c)
    : product_(c.product)
    , txid_generator_(c.txid_generator)
{
}

std::expected<std::vector<funx::match::Event>, funx::match::proto::ErrorCode>
ProductMatcher::add(Order const& o)
{
    using funx::match::proto::ErrorCode;
    if (o.product != product_) {
        return std::unexpected(ErrorCode::UnknownProduct);
    }

    // If the order is a buy, then MySide=BuyBook and OtherSide=SellBook.
    // If the order is a sell, then MySide=SellBook and OtherSide=BuyBook.
    auto match = [this]<typename MySide, typename OtherSide>(
            Order const& original_order,
            MySide& my_side,
            OtherSide& other_side
        )
        -> std::expected<std::vector<Event>, ErrorCode>
    {
        if (my_side.hasOrder(original_order.order_id)) {
            return std::unexpected(ErrorCode::DuplicateOrderId);
        }

        std::vector<Event> events;

        // make a copy, so that we can decrement quantity
        Order order = original_order;

        // while the new order has unfilled quantity and the new order's
        // price is not worse than the other side's best price
        while (order.qty > funx::Quantity{0} &&
                !other_side.empty() &&
                !OtherSide::comparator(other_side.bestLevelPrice(), order.price))
        {
            Order const& other_side_best_order = other_side.bestOrder();
            funx::Quantity match_qty = std::min(order.qty, other_side_best_order.qty);

            Order const& buy_order = getBuyOrderInMatch(order, other_side_best_order);
            Order const& sell_order = getSellOrderInMatch(order, other_side_best_order);

            events.push_back(Match{
                .product = order.product,
                .qty = match_qty,
                .price = other_side_best_order.price,
                .txid = this->txid_generator_->nextTxid(),
                .buyer_company = buy_order.company,
                .buyer_order_id = buy_order.order_id,
                .seller_company = sell_order.company,
                .seller_order_id = sell_order.order_id,
                .passive_side = other_side_best_order.side,
            });

            if (match_qty == other_side_best_order.qty) {
                events.push_back(RemoveOrder{
                    .order_id = other_side_best_order.order_id,
                });
            }
            other_side.fillBestOrder(match_qty);
            order.qty -= match_qty;
        }

        // leftover quantity gets added to book
        if (order.qty > funx::Quantity{0}) {
            assert(order.duration == funx::Duration::Day ||
                    order.duration == funx::Duration::IOC);
            switch (order.duration)
            {
            case funx::Duration::Day:
                events.push_back(AddOrder{
                    .order = order,
                });
                my_side.addOrder(order);
                break;
            case funx::Duration::IOC:
                events.push_back(IocMiss{
                    .order_id = order.order_id,
                });
                break;
            }
        }

        return events;
    };

    switch (o.side)
    {
    case funx::Side::Buy:
        return FUNX_TRY(match(o, buys_, sells_));
    case funx::Side::Sell:
        return FUNX_TRY(match(o, sells_, buys_));
    }

    // unreachable
    assert(false);
    return {};
}


std::expected<std::vector<funx::match::Event>, funx::match::proto::ErrorCode>
ProductMatcher::remove(funx::OrderId id)
{
    using funx::match::proto::ErrorCode;
    if (buys_.tryRemove(id) || sells_.tryRemove(id)) {
        return std::vector<funx::match::Event>{RemoveOrder{
            .order_id = id
        }};
    }
    return std::unexpected(ErrorCode::OrderNotFound);
}

Matcher::Matcher(Config const& c)
{
    for (funx::Product p : c.products) {
        product_matchers_.emplace(p, ProductMatcher(ProductMatcher::Config{
            .product = p,
            .txid_generator = c.txid_generator,
        }));
    }
}

std::expected<std::vector<funx::match::Event>, funx::match::proto::ErrorCode>
Matcher::add(Order const& o)
{
    if (id_to_product_.contains(o.order_id)) {
        return std::unexpected(funx::match::proto::ErrorCode::DuplicateOrderId);
    }

    auto it = product_matchers_.find(o.product);
    if (it == product_matchers_.end()) {
        return std::unexpected(funx::match::proto::ErrorCode::UnknownProduct);
    }

    ProductMatcher& product_matcher = it->second;
    MatchResult result = product_matcher.add(o);
    if (!result) {
        return result;
    }

    bool incoming_order_is_live = false;
    for (Event const& event : *result) {
        if (auto const* add = std::get_if<AddOrder>(&event)) {
            if (add->order.order_id == o.order_id) {
                incoming_order_is_live = true;
            }
        } else if (auto const* remove = std::get_if<RemoveOrder>(&event)) {
            id_to_product_.erase(remove->order_id);
        }
    }

    if (incoming_order_is_live) {
        id_to_product_[o.order_id] = o.product;
    }
    return result;
}

std::expected<std::vector<funx::match::Event>, funx::match::proto::ErrorCode>
Matcher::remove(funx::OrderId order_id)
{
    auto it = id_to_product_.find(order_id);
    if (it == id_to_product_.end()) {
        return std::unexpected(funx::match::proto::ErrorCode::OrderNotFound);
    }

    funx::Product product = it->second;
    assert(product_matchers_.contains(product));
    MatchResult result = product_matchers_.at(product).remove(order_id);
    if (result) {
        id_to_product_.erase(it);
    }
    return result;
}

} // namespace funx::match
