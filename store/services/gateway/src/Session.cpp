#include <funx/gateway/Session.h>

#include <flop/flop.h>
#include <flop/util.h>
#include <funx/common/flop.h>
#include <funx/log/Log.h>
#include <funx/match/proto.h>
#include <funx/net/util.h>
#include <funx/util/overload.h>

#include <thread>
namespace funx::gateway {

#define SESS_INFO(log_str, ...) LOG_INFO("[{:^8}]  " log_str, config_.session_name __VA_OPT__(,) __VA_ARGS__)
#define SESS_WARN(log_str, ...) LOG_WARN("[{:^8}]  " log_str, config_.session_name __VA_OPT__(,) __VA_ARGS__)
#define SESS_ERR(log_str, ...) LOG_ERR("[{:^8}]  " log_str, config_.session_name __VA_OPT__(,) __VA_ARGS__)

Session::Session(
        SessionConfig const& c,
        OrderTracker& order_tracker,
        MatchingServiceInterface& matching_service_intf)
    : config_(c)
    , heartbeat_timeout_duration_(config_.heartbeat_timeout_seconds.transform([](uint32_t seconds) {
        return std::chrono::seconds(seconds);
      }))
    , order_tracker_(order_tracker)
    , matching_service_intf_(matching_service_intf)
{
}

void Session::handleNewClient(TcpSocket&& new_client)
{
    checkClientDisconnected();
    if (std::holds_alternative<Session::NotConnected>(state_)) {
        state_ = Connected{
            .session = funx::TcpSession(funx::TcpSession::Config{
                .socket = std::move(new_client)
            })
        };
    } else {
        // client is already connected, reject extra connection attempts
        new_client.close();
    }
}

void Session::poll(funx::Clock::time_point now) {
    std::visit(funx::overloaded{
        [&](Session::NotConnected&) {
            // client cannot send us new messages if they're not connected
        },
        [&](Session::Connected& c) {
            pollConnectedState(c, now);
        },
        [&](Session::Active& a) {
            pollActiveState(a, now);
        }
    }, state_);

    checkClientDisconnected();
}

std::optional<funx::byte_view> Session::peekOneMsgNoValidation(funx::TcpSession& session)
{
    using msg_size_t = decltype(flop::Header::msg_size);
    std::optional<msg_size_t> maybe_msg_size = session.peekCopy<msg_size_t>();
    if (!maybe_msg_size) {
        return std::nullopt;
    }
    msg_size_t msg_size = *maybe_msg_size;
    if (msg_size > funx::TcpSession::MTU - sizeof(msg_size)) {
        LOG_WARN("Disconnecting client sending msg_size larger than MTU");
        close();
        return std::nullopt;
    }

    // remember that msg_size does not include its own bytes, so have to
    // add that back for the full message
    return session.peek(msg_size + sizeof(msg_size));
}

void Session::pollConnectedState(Session::Connected& state, funx::Clock::time_point now)
{
    // note: it's easy to forget to call session.consume() later.
    // We could do a consumeOneMsgNoValidation(state.session, handler)
    // but would prefer to avoid nested handlers getting too complicated
    std::optional<funx::byte_view> maybe_msg_bytes =
        peekOneMsgNoValidation(state.session);
    if (!maybe_msg_bytes) {
        return;
    }
    funx::byte_view msg_bytes = *maybe_msg_bytes;

    auto res = flop::handleMsg(msg_bytes, funx::overloaded{
        [&](flop::MessageT<flop::Login> msg) {
            state.session.consume(msg_bytes.size());
            flop::Login const& login = msg.body;
            size_t session_len = strnlen(login.session, sizeof(login.session));
            std::string_view session(login.session, session_len);
            size_t password_len = strnlen(login.password, sizeof(login.password));
            std::string_view password(login.password, password_len);
            SESS_INFO("Client REQ [LOGIN] name={} password={}", session, password);
            if (config_.session_name != session) {
                sendLoginResponse(state, flop::LoginResponse{
                    .reason = flop::Reason::IncorrectSessionName
                });
                close();
                return;
            }
            if (config_.password != password) {
                sendLoginResponse(state, flop::LoginResponse{
                        .reason = flop::Reason::IncorrectPassword
                });
                close();
                return;
            }
            sendLoginResponse(state, flop::LoginResponse{
                .reason = flop::Reason::NoError
            });
            state_ = Active{
                .session = std::move(state.session),
                .last_msg_time = now,
            };
        },
        [&](auto&& msg) {
            SESS_WARN("Client sent {} before logging on",
                    flop::msgTypeToStr(msg.header.msg_type));
            state.session.consume(msg_bytes.size());
            close();
        }
    });

    if (!res) {
        SESS_WARN("{}", res.error().str());
        close();
        return;
    }
}

void Session::pollActiveState(Session::Active& state, funx::Clock::time_point now)
{
    std::optional<funx::byte_view> maybe_msg_bytes =
        peekOneMsgNoValidation(state.session);
    if (!maybe_msg_bytes) {
        checkHeartbeatTimeout(state, now);
        return;
    }
    funx::byte_view msg_bytes = *maybe_msg_bytes;

    auto res = flop::handleMsg(msg_bytes, funx::overloaded{
        [&](flop::MessageT<flop::NewOrder> const& msg) {
            auto const& order = msg.body;
            handleNewOrder(state, order);
        },
        [&](flop::MessageT<flop::CancelOrder> const& msg) {
            auto const& cancel = msg.body;
            handleCancelOrder(state, cancel);
        },
        [&](flop::MessageT<flop::Logout> const&) {
            SESS_INFO("Client REQ [LOGOUT]");
            state.session.send(funx::util::to_bytes(
                flop::MessageT<flop::LogoutResponse>{
                    .body = flop::LogoutResponse{
                        .reason = flop::Reason::NoError,
                    }
                }
            ));
            SESS_INFO("Client RSP [LOGOUT]");
            close();
        },
        [&](flop::MessageT<flop::Heartbeat> const&) {
            // no need to do anything. We update last_msg_time at the end
        },
        [&](auto&& msg) {
            LOG_WARN("Client sent invalid msg type {}",
                    flop::msgTypeToStr(msg.header.msg_type));
            close();
        }
    });

    if (std::holds_alternative<Active>(state_)) [[likely]] {
        state.last_msg_time = now;
        state.session.consume(msg_bytes.size());
    }
}

void Session::handleNewOrder(Session::Active& state, flop::NewOrder const& order)
{
    SESS_INFO("Client REQ [NEW_ORDER] product={} side={} qty={} price={} duration={} client_order_id={}"
            , order.product
            , std::to_underlying(order.side)
            , order.qty
            , order.price
            , std::to_underlying(order.duration)
            , order.client_order_id
    );

    auto product = funx::Product{order.product};
    std::optional<funx::match::Client*> maybe_client =
        matching_service_intf_.getClientForProduct(product);
    if (!maybe_client) [[unlikely]] {
        sendOrderReject(state, flop::OrderReject{
            .client_order_id = order.client_order_id,
            .reason = flop::Reason::InvalidProduct,
        });
        return;
    }
    auto& client = **maybe_client;

    if (client.closed()) [[unlikely]] {
        sendOrderReject(state, flop::OrderReject{
            .client_order_id = order.client_order_id,
            .reason = flop::Reason::InternalError,
        });
        return;
    }

    // We'll cap the max quantity at 10 million arbitrarily right now. This is
    // to prevent clients from overflowing us by sending MAX_INT64
    if (order.qty <= 0 || order.qty > 10'000'000LL) [[unlikely]] {
        sendOrderReject(state, flop::OrderReject{
            .client_order_id = order.client_order_id,
            .reason = flop::Reason::InvalidQuantity,
        });
        return;
    }

    // We allow prices to go negative, in case you'd like to pay
    // someone to take your thing.
    //
    // We do not allow quantities to negative. If you wanted a negative
    // quantity, you should submit with the opposite side.
    //
    // TODO (non-MVP) We can have better price/qty validation checks per product.
    // However, this is tedious work and not a critical piece of the MVP

    std::optional<funx::Side> maybe_side = funx::flopSideToFunxSide(order.side);
    if (!maybe_side) [[unlikely]] {
        sendOrderReject(state, flop::OrderReject{
            .client_order_id = order.client_order_id,
            .reason = flop::Reason::InvalidSide,
        });
        return;
    }
    auto side = *maybe_side;

    std::optional<funx::Duration> maybe_duration = funx::flopDurationToFunxDuration(order.duration);
    if (!maybe_duration) [[unlikely]] {
        sendOrderReject(state, flop::OrderReject{
            .client_order_id = order.client_order_id,
            .reason = flop::Reason::InvalidDuration,
        });
        return;
    }
    auto duration = *maybe_duration;

    if (cloid_to_eoid_.contains(order.client_order_id)) [[unlikely]] {
        sendOrderReject(state, flop::OrderReject{
            .client_order_id = order.client_order_id,
            .reason = flop::Reason::DuplicateClientOrderId,
        });
        return;
    }
    auto order_id = order_tracker_.allocateOrderId(this);
    if (eoid_to_client_.contains(order_id)) [[unlikely]] {
        assert(false);
        sendOrderReject(state, flop::OrderReject{
            .client_order_id = order.client_order_id,
            .reason = flop::Reason::InternalError,
        });
        LOG_ERR("Duplicate exchange order ID {}. This should never happen", order_id);
        return;
    }

    funx::match::Order match_order{
        .product = product,
        .qty = funx::Quantity{order.qty},
        .price = funx::Price{order.price},
        .side = side,
        .duration = duration,
        .company = config_.company_id,
        .order_id = order_id,
    };
    client.addOrder(match_order);
    SESS_INFO("Match REQ [ADD_ORDER] {}", match_order);

    cloid_to_eoid_[order.client_order_id] = order_id;
    eoid_to_client_[order_id] = ClientOrderDetails{
        .client_order_id = order.client_order_id,
        .product = order.product,
        .side = order.side,
        .original_price = order.price,
        .remaining_qty = order.qty,
        .duration = order.duration,
        .ack_status = OrderAckStatus::HaventSentAck,
    };
}

void Session::handleCancelOrder(Session::Active& state, flop::CancelOrder const& cancel)
{
    SESS_INFO("Client REQ [CANCEL_ORDER] client_order_id={}", cancel.client_order_id);

    auto eoid_it = cloid_to_eoid_.find(cancel.client_order_id);

    // de-optimize cases where client is doing invalid stuff like
    // canceling nonexistent things
    if (eoid_it == cloid_to_eoid_.end()) [[unlikely]] {
        sendCancelReject(state, flop::CancelReject{
            .client_order_id = cancel.client_order_id,
            .reason = flop::Reason::CancelClientOrderIdNotFound,
        });
        SESS_INFO("Client RSP [CANCEL_REJECT] CancelClientOrderIdNotFound");
        return;
    }

    funx::OrderId eoid = eoid_it->second;
    auto client_details_it = eoid_to_client_.find(eoid);

    // This should never happen. If we've accepted the order and added it to the
    // cloid_to_eoid map, then it has to have an eoid and client details.
    if (client_details_it == eoid_to_client_.end()) [[unlikely]] {
        assert(false);
        sendCancelReject(state, flop::CancelReject{
            .client_order_id = cancel.client_order_id,
            .reason = flop::Reason::InternalError,
        });
        SESS_INFO("Client RSP [CANCEL_REJECT] InternalError");
        return;
    }

    ClientOrderDetails const& client_details = client_details_it->second;
    funx::Product product{client_details.product};

    std::optional<funx::match::Client*> maybe_client =
        matching_service_intf_.getClientForProduct(product);
    // This should never happen. If we've accepted the order, we had to have
    // validated that there was a matching service handling that order's product
    if (!maybe_client) [[unlikely]] {
        assert(false);
        sendCancelReject(state, flop::CancelReject{
            .client_order_id = cancel.client_order_id,
            .reason = flop::Reason::InternalError,
        });
        SESS_INFO("Client RSP [CANCEL_REJECT] InternalError");
        return;
    }

    (*maybe_client)->cancelOrder(eoid);
    SESS_INFO("Match REQ [CANCEL_ORDER] eoid={}", eoid);
}

void Session::sendLoginResponse(Session::Connected& state, flop::LoginResponse const& resp)
{
    state.session.send(funx::util::to_bytes(makeUnsequencedResponse(resp)));
    SESS_INFO("Client RSP [LOGIN_RESPONSE] reason={}", flop::reasonToStr(resp.reason));
}

void Session::sendOrderAccepted(Session::Active& state, flop::OrderAccepted const& accept)
{
    state.session.send(funx::util::to_bytes(makeSequencedResponse(accept)));
    SESS_INFO("Client RSP [ORDER_ACCEPT]"
            " product={}"
            " side={}"
            " qty={}"
            " price={}"
            " duration={}"
            " client_order_id={}"
            " funx_order_id={}"
            " funx_event_timestamp={}"
            , accept.product
            , std::to_underlying(accept.side)
            , accept.qty
            , accept.price
            , std::to_underlying(accept.duration)
            , accept.client_order_id
            , accept.funx_order_id
            , accept.funx_event_timestamp
    );
}

void Session::sendOrderReject(Session::Active& state, flop::OrderReject const& reject)
{
    state.session.send(funx::util::to_bytes(makeSequencedResponse(reject)));
    SESS_INFO("Client RSP [ORDER_REJECT] client_order_id={} reason={}",
            reject.client_order_id, flop::reasonToStr(reject.reason));
}

void Session::sendCancelReject(Session::Active& state, flop::CancelReject const& reject)
{
    state.session.send(funx::util::to_bytes(makeSequencedResponse(reject)));
    SESS_INFO("Client RSP [CANCEL_REJECT] client_order_id={} reason={}",
            reject.client_order_id, flop::reasonToStr(reject.reason));
}

void Session::sendTrade(Session::Active& state, flop::Trade const& trade)
{
    state.session.send(funx::util::to_bytes(makeSequencedResponse(trade)));
    SESS_INFO("Client RSP [TRADE]"
            " product={}"
            " side={}"
            " qty={}"
            " price={}"
            " client_order_id={}"
            " funx_order_id={}"
            " funx_transaction_id={}"
            " funx_event_timestamp={}"
            " fee_code={}"
            , trade.product
            , std::to_underlying(trade.side)
            , trade.qty
            , trade.price
            , trade.client_order_id
            , trade.funx_order_id
            , trade.funx_transaction_id
            , trade.funx_event_timestamp
            , std::to_underlying(trade.fee_code)
        );
}

void Session::sendOrderRemoved(Session::Active& state, flop::OrderRemoved const& remove)
{
    state.session.send(funx::util::to_bytes(makeSequencedResponse(remove)));
    auto eoid_it = cloid_to_eoid_.find(remove.client_order_id);
    if (eoid_it != cloid_to_eoid_.end()) [[likely]] {
        funx::OrderId eoid = eoid_it->second;
        [[maybe_unused]] size_t elems_removed = eoid_to_client_.erase(eoid);
        assert(elems_removed);
        cloid_to_eoid_.erase(eoid_it);
        order_tracker_.releaseOrderId(eoid);
    } else {
        assert(false);
    }
    SESS_INFO("Client RSP [ORDER_REMOVED] client_order_id={}", remove.client_order_id);
}

void Session::handleOrderAccepted(
        funx::match::proto::MatchingEvent const& me,
        funx::match::AddOrder const& add)
{
    SESS_INFO("Match RSP [ORDER_ACCEPTED] {}", add);
    if (!std::holds_alternative<Active>(state_)) [[unlikely]] {
        // TODO (non-MVP) when recovery is supported, unconditionally add this event
        // to a log of things the client can request recovery for
        return;
    }
    auto& state = std::get<Active>(state_);

    funx::match::Order const& order = add.order;
    std::optional<ClientOrderDetails*> maybe_client_details = getClientOrderDetails(order.order_id);

    // if we already synthesized an ack for this order earlier because it was
    // aggressive and matching service sent the trade first... then we do not
    // need to send another ack. Accept either SentSynthesizedAck or SentAck
    // just to be safe
    if (maybe_client_details) {
        if ((*maybe_client_details)->ack_status == OrderAckStatus::SentSynthesizedAck ||
                (*maybe_client_details)->ack_status == OrderAckStatus::SentAck) {
            return;
        }
    }

    std::optional<flop::Side> maybe_side = funx::funxSideToFlopSide(order.side);
    if (!maybe_side) [[unlikely]] {
        assert(false);
        LOG_ERR("Internal error. Matching service sent invalid side {}. "
                "Defaulting side to buy.",
                static_cast<int>(order.side));
    }
    flop::Side side = maybe_side.value_or(flop::Side::Buy);

    std::optional<flop::Duration> maybe_duration =
            funx::funxDurationToFlopDuration(order.duration);
    if (!maybe_duration) [[unlikely]] {
        assert(false);
        LOG_ERR("Internal error. Matching service sent invalid duration {}. "
                "Defaulting duration to day.", static_cast<int>(order.duration));
    }
    flop::Duration duration = maybe_duration.value_or(flop::Duration::Day);

    uint64_t client_order_id = [&]() -> uint64_t {
        if (!maybe_client_details) {
            LOG_ERR("Internal error. Matching service sent exchange order ID "
                    "that doesn't map to any client order ID. Defaulting to 0");
            return 0;
        }
        return (*maybe_client_details)->client_order_id;
    }();

    flop::OrderAccepted order_ack{
        .product = +order.product,
        .side = side,
        .qty = +order.qty,
        .price = +order.price,
        .duration = duration,
        .client_order_id = client_order_id,
        .funx_order_id = +order.order_id,
        .funx_event_timestamp = me.match_timestamp,
    };
    sendOrderAccepted(state, order_ack);

    if (maybe_client_details) {
        auto& client_details = **maybe_client_details;
        client_details.ack_status = OrderAckStatus::SentAck;
        client_details.remaining_qty = +order.qty;
    }
}

void Session::handleMatch(
        funx::match::proto::MatchingEvent const& me,
        funx::match::Match const& match,
        funx::Side side_to_process)
{
    SESS_INFO("Match RSP [MATCH] {} side_to_process={}", match, side_to_process);
    if (!std::holds_alternative<Active>(state_)) [[unlikely]] {
        // TODO (non-MVP) when recovery is supported, unconditionally add this event
        // to a log of things the client can request recovery for
        return;
    }
    auto& state = std::get<Active>(state_);

    std::optional<flop::Side> maybe_side = funx::funxSideToFlopSide(side_to_process);
    if (!maybe_side) [[unlikely]] {
        assert(false);
        LOG_ERR("Internal error. handleMatch called with invalid side_to_process {}. "
                "Defaulting side to buy.",
                static_cast<int>(side_to_process));
    }
    flop::Side side = maybe_side.value_or(flop::Side::Buy);

    funx::OrderId order_id = (side_to_process == funx::Side::Buy)
            ? match.buyer_order_id
            : match.seller_order_id;

    std::optional<ClientOrderDetails*> maybe_client_details = getClientOrderDetails(order_id);
    uint64_t client_order_id = [&]() -> uint64_t {
        if (!maybe_client_details) [[unlikely]] {
            assert(false);
            LOG_ERR("Got a match for order ID {} which doesn't map to any "
                    "client order ID. Defaulting to 0", order_id);
            return 0;
        }
        return (*maybe_client_details)->client_order_id;
    }();

    auto fee_code = (match.passive_side == side_to_process)
            ? flop::FeeCode::AddedLiquidity
            : flop::FeeCode::RemovedLiquidity;

    std::optional<flop::OrderAccepted> synth_order_ack = std::nullopt;
    std::optional<flop::OrderRemoved> synth_order_remove = std::nullopt;
    if (maybe_client_details) {
        auto& client_details = **maybe_client_details;

        // For aggressive orders, the matching service will immediately start
        // sending Trades over. We have to synthesize an order ack.
        // This complicates the session logic, but makes the incremental
        // market data easier
        if (client_details.ack_status == OrderAckStatus::HaventSentAck) {
            synth_order_ack = flop::OrderAccepted{
                .product = +match.product,
                .side = side,
                .qty = client_details.remaining_qty,
                .price = client_details.original_price,
                .duration = client_details.duration,
                .client_order_id = client_details.client_order_id,
                .funx_order_id = +order_id,
                .funx_event_timestamp = me.match_timestamp,
            };
            client_details.ack_status = OrderAckStatus::SentSynthesizedAck;
        }

        client_details.remaining_qty -= (+match.qty);

        // If we synthesized the ack for this order, we also have to
        // synthesize the remove once its quantity reaches 0
        if (client_details.remaining_qty <= 0 &&
                client_details.ack_status == OrderAckStatus::SentSynthesizedAck)
        {
            synth_order_remove = flop::OrderRemoved{
                .client_order_id = client_details.client_order_id,
                .funx_event_timestamp = me.match_timestamp,
            };
            client_details.ack_status = OrderAckStatus::SentSynthesizedRemove;
        }
    }

    flop::Trade trade{
        .product = +match.product,
        .side = side,
        .qty = +match.qty,
        .price = +match.price,
        .client_order_id = client_order_id,
        .funx_order_id = +order_id,
        .funx_transaction_id = +match.txid,
        .funx_event_timestamp = me.match_timestamp,
        .fee_code = fee_code,
    };

    if (synth_order_ack) {
        sendOrderAccepted(state, *synth_order_ack);
    }
    sendTrade(state, trade);
    if (synth_order_remove) {
        sendOrderRemoved(state, *synth_order_remove);
    }
}

void Session::handleOrderRemoved(
        funx::match::proto::MatchingEvent const& me,
        funx::match::RemoveOrder const& remove)
{
    SESS_INFO("Match RSP [ORDER_REMOVED] {}", remove);

    std::optional<ClientOrderDetails*> maybe_client_details = getClientOrderDetails(remove.order_id);

    // if this doesn't correspond to any order, then there's no point in
    // sending a remove order for client order ID 0. We might as well not
    // send anything
    if (!maybe_client_details) [[unlikely]] {
        assert(false);
        LOG_ERR("Got a RemoveOrder for order ID {} which doesn't map to any "
                "client order ID. Dropping this remove", remove.order_id);
        return;
    }

    auto& client_details = **maybe_client_details;

    if (!std::holds_alternative<Active>(state_)) [[unlikely]] {
        // TODO (non-MVP) when recovery is supported, unconditionally add this event
        // to a log of things the client can request recovery for

        // If the remove comes from cancel-on-disconnect, after the
        // client has already disconnected, we still need to remove the order
        // from our mappings
        cloid_to_eoid_.erase(client_details.client_order_id);
        eoid_to_client_.erase(remove.order_id);
        return;
    }

    auto& state = std::get<Active>(state_);
    sendOrderRemoved(state, flop::OrderRemoved{
            .client_order_id = client_details.client_order_id,
            .funx_event_timestamp = me.match_timestamp,
        }
    );
}

void Session::handleIocMiss(
        funx::match::proto::MatchingEvent const& me,
        funx::match::IocMiss const& ioc_miss)
{
    if (!std::holds_alternative<Active>(state_)) [[unlikely]] {
        // TODO (non-MVP) when recovery is supported, unconditionally add this event
        // to a log of things the client can request recovery for
        return;
    }
    auto& state = std::get<Active>(state_);

    std::optional<ClientOrderDetails*> maybe_client_details = getClientOrderDetails(ioc_miss.order_id);

    // if this doesn't correspond to any order, then there's no point in
    // sending a remove order for client order ID 0. We might as well not
    // send anything
    if (!maybe_client_details) [[unlikely]] {
        assert(false);
        LOG_ERR("Got a IocMiss for order ID {} which doesn't map to any "
                "client order ID. Dropping this IocMiss", ioc_miss.order_id);
        return;
    }

    auto& client_details = **maybe_client_details;

    // If an IOC missed and we haven't sent an accept for the order yet,
    // then we need to synthesize an accept before we send the remove
    if (client_details.ack_status == OrderAckStatus::HaventSentAck) {
        sendOrderAccepted(state, flop::OrderAccepted{
                .product = client_details.product,
                .side = client_details.side,
                .qty = client_details.remaining_qty,
                .price = client_details.original_price,
                .duration = client_details.duration,
                .client_order_id = client_details.client_order_id,
                .funx_order_id = +ioc_miss.order_id,
                .funx_event_timestamp = me.match_timestamp,
            }
        );
        client_details.ack_status = OrderAckStatus::SentSynthesizedAck;
    }

    sendOrderRemoved(state, flop::OrderRemoved{
            .client_order_id = client_details.client_order_id,
            .funx_event_timestamp = me.match_timestamp,
        }
    );
}

void Session::checkHeartbeatTimeout(Session::Active& state, funx::Clock::time_point now)
{
    if (heartbeat_timeout_duration_) {
        if (eoid_to_client_.size() > 0 && state.last_msg_time + *heartbeat_timeout_duration_ <= now) {
            LOG_INFO("Client heartbeat timeout. last_msg_time={} now={}", state.last_msg_time, now);
            cancelAllOrders();
            // move the last msg time forward so that we don't spam cancels
            state.last_msg_time += *heartbeat_timeout_duration_;
        }
    }
}

void Session::cancelAllOrders() {
    if (eoid_to_client_.size() > 0) {
        LOG_INFO("Cancelling all orders");
        for (auto& [eoid, client_details] : eoid_to_client_) {
            funx::Product product{client_details.product};
            std::optional<funx::match::Client*> maybe_client =
                matching_service_intf_.getClientForProduct(product);
            if (maybe_client) [[likely]] {
                (*maybe_client)->cancelOrder(eoid);
            }
        }
    }
}

void Session::checkClientDisconnected() {
    bool client_disconnected = std::visit(funx::overloaded{
        [](NotConnected&) {
            return false;
        },
        [](Connected& connected) {
            return connected.session.closed();
        },
        [](Active& active) {
            return active.session.closed();
        }
    }, state_);

    if (client_disconnected) {
        close();
    }
}

void Session::close() {
    std::visit(funx::overloaded{
        [](NotConnected&) {},
        [](Connected& connected) {
            LOG_WARN("Disconnecting client {}", connected.session.getRemoteTcpEndpoint());
            connected.session.close();
        },
        [&](Active& active) {
            LOG_WARN("Disconnecting client {}", active.session.getRemoteTcpEndpoint());
            cancelAllOrders();
            active.session.close();
        }
    }, state_);
    state_ = NotConnected{};
}

std::optional<Session::ClientOrderDetails*>
Session::getClientOrderDetails(funx::OrderId order_id)
{
    auto it = eoid_to_client_.find(order_id);
    if (it == eoid_to_client_.end()) {
        return std::nullopt;
    }
    return &it->second;
}

funx::TcpPort Session::getLocalPort() const {
    return config_.port;
}

} // namespace funx::gateway
