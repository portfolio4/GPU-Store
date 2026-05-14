#ifndef FUNX_SUPPLIER_SUPPLIER_H
#define FUNX_SUPPLIER_SUPPLIER_H

#include <funx/clock/Clock.h>
#include <funx/common/types.h>
#include <funx/gateway/config.h>
#include <funx/gateway/ClientMultiplexer.h>
#include <funx/test/util.h>


namespace funx::supplier {

// Sends bids and asks for a given product
class Supplier
{

public:
    struct Config {
        funx::gateway::ClientMultiplexer* gw_intf;
        funx::Product product;
        funx::Price initial_fair_price;
    };

    Supplier(Config const& c)
        : next_action_time_(funx::Clock::now())
        , gw_intf_(c.gw_intf)
        , product_(c.product)
        , fair_price_(c.initial_fair_price)
        , half_spread_(funx::test::random_u32(50, 500))
    {
        for (uint32_t i = 0; i < ACTIVE_ORDERS_PER_SIDE; ++i) {
            bids_[i].bound_client = &gw_intf_->next();
            bids_[i].desired_order = flop::NewOrder{
                .product = +product_,
                .side = flop::Side::Buy,
                .qty = 100*(i+1),
                .price = +fair_price_ - half_spread_ - i,
                .duration = flop::Duration::Day,
                .client_order_id = 0,  // assigned before send
            };
            asks_[i].bound_client = &gw_intf_->next();
            asks_[i].desired_order = flop::NewOrder{
                .product = +product_,
                .side = flop::Side::Sell,
                .qty = 100*(i+1),
                .price = +fair_price_ + half_spread_ + i,
                .duration = flop::Duration::Day,
                .client_order_id = 0, // assigned before send
            };
        }
    }

    void poll() {
        if (funx::Clock::now() < next_action_time_) {
            return;
        }

        auto action = funx::test::random_u8(0, 3);
        auto target_idx = funx::test::random_u8(0, ACTIVE_ORDERS_PER_SIDE - 1);
        bool is_buy = funx::test::random_u8(0, 1);
        OrderInfo& target = is_buy
                ? bids_[target_idx]
                : asks_[target_idx];
        uint32_t minSellPrice = std::min_element(asks_.begin(), asks_.end(),
                [&](OrderInfo const& a, OrderInfo const& b) {
                    return a.desired_order.price < b.desired_order.price;
                })->desired_order.price;
        uint32_t maxBuyPrice = std::max_element(bids_.begin(), bids_.end(),
                [&](OrderInfo const& a, OrderInfo const& b) {
                    return a.desired_order.price < b.desired_order.price;
                })->desired_order.price;

        // increase price of an order by 1 unit, assuming it doesn't cross
        if (action == 0) {
            if (!is_buy || target.desired_order.price + 1 < minSellPrice) {
                target.desired_order.price += 1;
            }
        // decrease price of an order by 1 unit, assuming it doesn't cross
        } else if (action == 1) {
            if (target.desired_order.price > 1) {
                if (is_buy || target.desired_order.price - 1 > maxBuyPrice) {
                    target.desired_order.price -= 1;
                }
            }
        // pick a random order and increase the quantity
        } else if (action == 2) {
            auto idx = funx::test::random_u8(0, ACTIVE_ORDERS_PER_SIDE - 1);
            bool isBuy = funx::test::random_u8(0, 1);
            if (isBuy) {
                bids_[idx].desired_order.qty += funx::test::random_u8(1, 100);
            } else {
                asks_[idx].desired_order.qty += funx::test::random_u8(1, 100);
            }
        // pick a random order and decrease the quantity
        } else if (action == 3) {
            auto idx = funx::test::random_u8(0, ACTIVE_ORDERS_PER_SIDE - 1);
            bool isBuy = funx::test::random_u8(0, 1);
            if (isBuy) {
                bids_[idx].desired_order.qty -= funx::test::random_i64(
                    0, bids_[idx].desired_order.qty - 1);
            } else {
                asks_[idx].desired_order.qty -= funx::test::random_i64(
                    0, asks_[idx].desired_order.qty - 1);
            }
        }

        updateOrdersIfNecessary();
        next_action_time_ = funx::Clock::now() + std::chrono::milliseconds(
                funx::test::random_u32(30, 1000));
    }

    void updateOrdersIfNecessary() {
        for (OrderInfo& info : bids_) {
            if (info.needsUpdate()) {
                info.cancelActive();
                info.desired_order.client_order_id = gw_intf_->nextClientOrderId();
                info.bound_client->sendBasicNewOrder(info.desired_order);
                info.sent_order = info.desired_order;
            }
        }
        for (OrderInfo& info : asks_) {
            if (info.needsUpdate()) {
                info.cancelActive();
                info.desired_order.client_order_id = gw_intf_->nextClientOrderId();
                info.bound_client->sendBasicNewOrder(info.desired_order);
                info.sent_order = info.desired_order;
            }
        }
    }

    void cancelAll() {
        for (OrderInfo& info : bids_) {
            info.cancelActive();
        }
        for (OrderInfo& info : asks_) {
            info.cancelActive();
        }
    }

private:
 
    struct OrderInfo {
        funx::gateway::Client* bound_client;
        std::optional<flop::NewOrder> sent_order;
        flop::NewOrder desired_order;

        bool needsUpdate() const {
            return !sent_order ||
                sent_order->qty != desired_order.qty ||
                sent_order->price != desired_order.price
                ;
        }

        void cancelActive() {
            if (sent_order) {
                bound_client->sendCancelOrder(flop::CancelOrder{
                    .client_order_id = sent_order->client_order_id
                });
                sent_order = std::nullopt;
            }
        }

    };


    static const int ACTIVE_ORDERS_PER_SIDE = 8;
    std::array<OrderInfo, ACTIVE_ORDERS_PER_SIDE> bids_;
    std::array<OrderInfo, ACTIVE_ORDERS_PER_SIDE> asks_;

    funx::Clock::time_point next_action_time_ = {};

    funx::gateway::ClientMultiplexer* gw_intf_;
    funx::Product product_;
    funx::Price fair_price_;
    uint32_t half_spread_;
};

} // namespace supplier

#endif  // FUNX_SUPPLIER_SUPPLIER_H
