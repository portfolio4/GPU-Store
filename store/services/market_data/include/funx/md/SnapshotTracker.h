#ifndef FUNX_MARKET_DATA_BOOK_BUILDER_H
#define FUNX_MARKET_DATA_BOOK_BUILDER_H

#include <flip/flip.h>
#include <funx/match/proto.h>

#include <map>
#include <unordered_map>

namespace funx::md {

// Tracks book for a product and produces level based summaries
class SnapshotTracker
{
public:

    struct Config {
        funx::Product product;
    };

    SnapshotTracker(Config const& c);

    void handleEvent(funx::match::AddOrder const& add);

    // returns the remaining quantity on the passive order (for convenience
    // so that the caller can populate the new_qty field on ModifyOrder)
    funx::Quantity handleEvent(funx::match::Match const& match);
    void handleEvent(funx::match::RemoveOrder const& remove);

    flip::Snapshot buildSnapshot() const;

private:
    struct Level {
        funx::Quantity total_qty;
    };

    funx::Product product_;
    std::unordered_map<funx::OrderId, funx::match::Order> id_to_order_;

    std::map<funx::Price, Level, funx::BuyComparator> buys_;
    std::map<funx::Price, Level, funx::SellComparator> sells_;
};

} // namespace funx::md

#endif  // FUNX_MARKET_DATA_BOOK_BUILDER_H

