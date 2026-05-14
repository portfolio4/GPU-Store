#ifndef FUNX_MATCH_MATCHER_H
#define FUNX_MATCH_MATCHER_H

#include <funx/common/types.h>
#include <funx/match/proto.h>
#include <funx/match/TxidGenerator.h>

#include <cassert>
#include <expected>
#include <map>
#include <list>
#include <unordered_map>
#include <vector>

namespace funx::match {

namespace impl {

// Either the buy or the sell side of the book
template <typename Comparator>
class BookSide
{
private:
    struct Level
    {
        std::list<Order> orders;
        using iterator = decltype(orders)::iterator;
    };

    std::map<funx::Price, Level, Comparator> levels_;

    struct OrderData {
        decltype(levels_)::iterator iter_to_level;
        Level::iterator iter_to_order;
    };
    std::unordered_map<funx::OrderId, OrderData> order_map_;

public:
    constexpr static Comparator comparator{};

    bool empty() const {
        return levels_.empty();
    }

    funx::Price bestPrice() const {
        assert(!empty());
        return levels_.rbegin()->first;
    }

    Order const& bestOrder() const {
        assert(!empty());
        Level const& bestLevel = levels_.rbegin()->second;
        return bestLevel.orders.front();
    }

    // Fills the best order on this side by the given quantity.
    // Caller is responsible for inspecting the quantity of bestOrder()
    // and making sure it does not get overfilled.
    // If the best order becomes fully filled, the order will be removed.
    void fillBestOrder(funx::Quantity qty) {
        assert(!empty());
        Level& bestLevel = levels_.rbegin()->second;
        Order& bestOrder = bestLevel.orders.front();
        assert(qty <= bestOrder.qty);
        if (qty >= bestOrder.qty) {
            order_map_.erase(bestOrder.order_id);
            bestLevel.orders.pop_front();
            if (bestLevel.orders.empty()) {
                levels_.erase(std::prev(levels_.end()));
            }
        } else {
            bestOrder.qty = funx::Quantity{(+bestOrder.qty) - (+qty)};
        }
    }

    funx::Price bestLevelPrice() {
        assert(!empty());
        return levels_.rbegin()->first;
    }

    void addOrder(funx::match::Order order) {
        auto [iter_to_level, _] = levels_.try_emplace(order.price, Level{});
        Level& level = iter_to_level->second;
        level.orders.push_back(order);
        auto iter_to_order = std::prev(level.orders.end());
        order_map_[order.order_id] = OrderData{
            .iter_to_level = iter_to_level,
            .iter_to_order = iter_to_order,
        };
    }

    bool hasOrder(funx::OrderId id) {
        return order_map_.find(id) != order_map_.end();
    }

    // returns of the order was removed
    bool tryRemove(funx::OrderId id) {
        auto it = order_map_.find(id);
        if (it == order_map_.end()) {
            return false;
        }
        OrderData const& data = it->second;
        Level& level = data.iter_to_level->second;
        level.orders.erase(data.iter_to_order);
        if (level.orders.empty()) {
            levels_.erase(data.iter_to_level);
        }
        order_map_.erase(it);
        return true;
    }
};

} // namespace impl

// Matches buyers and sellers for one product
class ProductMatcher
{
public:
    struct Config {
        funx::Product product;
        funx::match::TxidGenerator* txid_generator;
    };

    ProductMatcher(Config const& c);

    std::expected<std::vector<funx::match::Event>, funx::match::proto::ErrorCode>
        add(Order const& o);

    std::expected<std::vector<funx::match::Event>, funx::match::proto::ErrorCode>
        remove(funx::OrderId id);

private:
    funx::Product product_{0};
    funx::match::TxidGenerator* txid_generator_;
    impl::BookSide<funx::BuyComparator> buys_;
    impl::BookSide<funx::SellComparator> sells_;
};

class Matcher
{
public:
    using MatchResult = std::expected<
        std::vector<funx::match::Event>,
        funx::match::proto::ErrorCode
    >;

    struct Config {
        std::vector<funx::Product> products;
        funx::match::TxidGenerator* txid_generator;
    };

    Matcher(Config const& c);

    // If the optional is valid, the pointer is guaranteed to be valid as well.
    // Unfortunately, std::optional does not allow returning references
    std::optional<ProductMatcher*> getProductMatcher(funx::Product p);

    MatchResult add(Order const& o);

    MatchResult remove(funx::OrderId order_id);

private:
    std::unordered_map<funx::Product, ProductMatcher> product_matchers_;
    std::unordered_map<funx::OrderId, funx::Product> id_to_product_;
};

} // namespace funx::match

#endif  // FUNX_MATCH_MATCHER_H

