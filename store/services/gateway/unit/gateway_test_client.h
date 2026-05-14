#pragma once

#include <funx/clock/Clock.h>
#include <funx/gateway/Client.h>
#include <funx/util/demangle.h>

#include <typeinfo>

class GatewayTestClient
{
public:
    static void requireAckMatchesOrder(flop::OrderAccepted const& ack, flop::NewOrder const& order)
    {
        REQUIRE(ack.product == order.product);
        REQUIRE(ack.side == order.side);
        REQUIRE(ack.qty == order.qty);
        REQUIRE(ack.price == order.price);
        REQUIRE(ack.duration == order.duration);
        REQUIRE(ack.client_order_id == order.client_order_id);
    }

    static void requireTradeMatchesAck(flop::Trade const& trade, flop::OrderAccepted const& ack)
    {
        REQUIRE(trade.product == ack.product);
        REQUIRE(trade.side == ack.side);
        REQUIRE(trade.client_order_id == ack.client_order_id);
        REQUIRE(trade.funx_order_id == ack.funx_order_id);
    }

    static void requireRemoveMatchesAck(flop::OrderRemoved const& remove, flop::OrderAccepted const& ack)
    {
        REQUIRE(remove.client_order_id == ack.client_order_id);
    }

    static void requireCancelRejectMatchesOrder(flop::CancelReject const& reject, flop::NewOrder const& order)
    {
        REQUIRE(reject.client_order_id == order.client_order_id);
    }

public:
    GatewayTestClient(funx::gateway::Client::Config const& c)
        : client_(c)
    {
    }

    funx::gateway::Client& client() {
        return client_;
    }

    // Msg is any body type in the FLOP protocol (e.g. Login, LoginResponse
    // OrderAccepted, ...)
    //
    // wait_fn is any function to constantly run while waiting for a response.
    // Can be used if you're doing single-threaded testing and want to poll
    // the gateway while you're checking for a response in the client.
    template <flop::AnyBody T>
    T requireResponse(std::function<void(void)> wait_fn = {})
    {
        auto stop = funx::Clock::now() + response_timeout_;
        do {
            std::optional<T> rtn;
            client_.poll(funx::overloaded{
                [&](flop::MessageT<T> const& msg) {
                    rtn = msg.body;
                },
                [](auto&&) {}
            });
            if (rtn) {
                return *rtn;
            }
            if (wait_fn) {
                wait_fn();
            }
        } while (funx::Clock::now() < stop);
        FAIL("expecting response of type " << funx::demangle(typeid(T).name())
                << " on session " << client_.config().session_cfg.session_name
                << ", but didn't get one before timeout");
        return {};
    }

    void requireNoResponse(std::function<void(void)> wait_fn = {}) {
        auto stop = funx::Clock::now() + no_response_timeout_;
        do {
            client_.poll(funx::overloaded{
                [](auto&& msg) {
                    FAIL(std::format("Got a {} response when one wasn't expected",
                                flop::msgTypeToStr(msg.header.msg_type)));
                }
            });
            if (wait_fn) {
                wait_fn();
            }
        } while (funx::Clock::now() < stop);
    }

    void setResponseTimeout(std::chrono::milliseconds timeout) {
        response_timeout_ = timeout;
    }

private:
    funx::gateway::Client client_;

    std::chrono::microseconds no_response_timeout_ = std::chrono::milliseconds(1);
    std::chrono::milliseconds response_timeout_ = std::chrono::milliseconds(1000);
};

