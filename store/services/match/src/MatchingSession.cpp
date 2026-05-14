#include <funx/match/MatchingSession.h>

#include <funx/clock/Clock.h>
#include <funx/log/Log.h>
#include <funx/match/ListenSession.h>
#include <funx/net/util.h>
#include <funx/util/overload.h>

namespace funx::match {

MatchingSession::MatchingSession(Config c)
    : client_conn_(funx::TcpSession::Config{.socket=std::move(c.client_conn)})
    , session_tracker_(c.session_tracker)
    , order_tracker_(c.order_tracker)
    , matcher_(c.matcher)
    , transaction_db_client_(c.transaction_db_client)
{
}

void MatchingSession::poll()
{
    pollClient();
}

void MatchingSession::pollClient()
{
    using funx::match::proto::Header;

    std::optional<const Header*> maybe_hdr = client_conn_.peek<Header>();
    if (!maybe_hdr) {
        return;
    }

    Header const& hdr = **maybe_hdr;

    if (hdr.magic != Header::MAGIC) {
        LOG_WARN("Disconnecting client sending wrong magic value {}", hdr.magic);
        client_conn_.close();
        return;
    }

    using funx::match::proto::MessageT;
    using funx::match::proto::AddOrderRequest;
    using funx::match::proto::CancelOrderRequest;
    using funx::match::proto::MsgType;
    switch (hdr.msg_type)
    {
    case MsgType::AddOrderRequest:
        {
            auto maybe_add = client_conn_.peek<MessageT<AddOrderRequest>>();
            if (!maybe_add) {
                return;
            }
            AddOrderRequest const& add = (*maybe_add)->body;
            handleAddOrderRequest(hdr, add);
            client_conn_.consume(sizeof(MessageT<AddOrderRequest>));
            return;
        }
    case MsgType::CancelOrderRequest:
        {
            auto maybe_cancel = client_conn_.peek<MessageT<CancelOrderRequest>>();
            if (!maybe_cancel) {
                return;
            }
            auto const& cancel = (*maybe_cancel)->body;
            handleCancelOrderRequest(hdr, cancel);
            client_conn_.consume(sizeof(MessageT<CancelOrderRequest>));
            return;
        }
    default:
        LOG_WARN("Disconnecting client sending unexpected message type {}",
                std::to_underlying(hdr.msg_type));
        client_conn_.close();
        return;
    }
}

void MatchingSession::cancelAllOrders()
{
    for (funx::OrderId order_id : live_order_ids_) {
        // The session owns only live orders, so both removals should succeed.
        [[maybe_unused]] Matcher::MatchResult match_result = matcher_.remove(order_id);
        assert(match_result);
        [[maybe_unused]] auto track_result = order_tracker_.remove(order_id);
        assert(track_result);
    }
    live_order_ids_.clear();
}

void MatchingSession::handleAddOrderRequest(
        funx::match::proto::Header const& hdr,
        funx::match::proto::AddOrderRequest const& add)
{
    auto track_res = order_tracker_.add(add.order.order_id,
        OrderTracker::OrderInfo{
            .client_tcp_details = client_conn_.getRemoteTcpEndpoint()
        }
    );
    if (!track_res) {
        respondReject(hdr, track_res.error());
        return;
    }
    live_order_ids_.insert(add.order.order_id);

    Matcher::MatchResult res = matcher_.add(add.order);
    if (!res) {
        respondReject(hdr, res.error());
        // doesn't matter if the remove fails. It's not in the system either way
        (void) order_tracker_.remove(add.order.order_id);
        live_order_ids_.erase(add.order.order_id);
        return;
    }

    // Try to write everything to transaction DB first. If anything fails,
    // then send a reject to the client and log a error about needing
    // manual intervention
    bool any_tx_db_errors = false;
    for (funx::match::Event const& e : *res) {
        if (std::holds_alternative<funx::match::Match>(e)) {
            auto const& match = std::get<funx::match::Match>(e);
            funx::transaction::Transaction tx{
                .product = match.product,
                .qty = match.qty,
                .price = match.price,
                .txid = match.txid,
                .buyerId = match.buyer_company,
                .sellerId = match.seller_company,
            };
            auto res = transaction_db_client_.save(tx);
            if (!res) {
                any_tx_db_errors = true;
                LOG_ERR("Failed to save transaction to DB. "
                        "Error code {}. Manual intervention needed: {}",
                        static_cast<int>(res.error()), tx);
            }
        }
    }

    if (any_tx_db_errors) {
        respondReject(hdr, funx::match::proto::ErrorCode::InternalError);
        return;
    }

    const int64_t match_timestamp = funx::Clock::now_ns();
    bool incoming_order_is_live = false;
    for (funx::match::Event const& e : *res) {
        if (auto const* event_add = std::get_if<funx::match::AddOrder>(&e)) {
            if (event_add->order.order_id == add.order.order_id) {
                incoming_order_is_live = true;
            }
        }
    }

    for (funx::match::Event const& e : *res) {
        respondEvent(hdr, match_timestamp, e);
        if (auto const* remove = std::get_if<funx::match::RemoveOrder>(&e)) {
            MatchingSession* owner = getSessionForOrderId(remove->order_id);
            assert(owner);
            if (owner != nullptr) {
                owner->live_order_ids_.erase(remove->order_id);
            }
            (void) order_tracker_.remove(remove->order_id);
        }
    }

    if (!incoming_order_is_live) {
        (void) order_tracker_.remove(add.order.order_id);
        live_order_ids_.erase(add.order.order_id);
    }
}

void MatchingSession::handleCancelOrderRequest(
        funx::match::proto::Header const& hdr,
        funx::match::proto::CancelOrderRequest const& cancel)
{
    Matcher::MatchResult res = matcher_.remove(cancel.order_id);

    if (!res) {
        respondReject(hdr, res.error());
        // this should always fail, but remove it just in case
        (void) order_tracker_.remove(cancel.order_id);
        live_order_ids_.erase(cancel.order_id);
        return;
    }

    const int64_t match_timestamp = funx::Clock::now_ns();
    for (funx::match::Event const& e : *res) {
        respondEvent(hdr, match_timestamp, e);
    }

    // Removing from tracker/matcher only fails due to OrderNotFound,
    // in which case the order is still gone. Therefore, the result of
    // removing from the tracker doesn't matter here.
    //
    // Note: we have to remove at the very end because we may
    // use the tracker's data in the above operations
    (void) order_tracker_.remove(cancel.order_id);
    live_order_ids_.erase(cancel.order_id);
}

void MatchingSession::respondReject(
        funx::match::proto::Header const& request_hdr,
        funx::match::proto::ErrorCode error)
{
    using namespace funx::match::proto;
    MessageT<Reject> response{
        .body = {
            .req_seqnum = request_hdr.seqnum,
            .error = error,
        }
    };
    client_conn_.send(funx::util::to_bytes(response));
}

void MatchingSession::respondEvent(
    funx::match::proto::Header const& request_hdr,
    int64_t event_timestamp,
    funx::match::Event const& e)
{
    using namespace funx::match::proto;

    // If the session is this MatchingSession, then sends the event in response
    // to the request indicated by `request_hdr`.
    //
    // If the session is not this MatchingSession, then sends this event as
    // a "triggered event"
    auto sendEventOnSess = [&](MatchingSession& sess) {
        if (&sess == this) {
            MessageT<MatchingEvent> response{
                .body = {
                    .req_seqnum = request_hdr.seqnum,
                    .match_timestamp = event_timestamp,
                    .event = SerializedEvent{e},
                }
            };
            client_conn_.send(funx::util::to_bytes(response));
        } else {
            sess.sendTriggeredEvent(event_timestamp, e);
        }
    };

    std::visit(funx::overloaded{
        [&](funx::match::AddOrder const&) {
            // Adds can only ever involve this client
            sendEventOnSess(*this);
        },
        [&](funx::match::Match const& match) {
            funx::OrderId aggressor_id =
                match.passive_side == funx::Side::Buy
                    ? match.seller_order_id
                    : match.buyer_order_id;
            funx::OrderId passive_id =
                match.passive_side == funx::Side::Buy
                    ? match.buyer_order_id
                    : match.seller_order_id;

            // Intentionally prioritize notifying the aggressor.
            // Can always switch this if we decide to change the business rules
            MatchingSession* aggressor_sess = getSessionForOrderId(aggressor_id);
            assert(aggressor_sess);
            if (aggressor_sess != nullptr) {
                sendEventOnSess(*aggressor_sess);
            }

            MatchingSession* passive_sess = getSessionForOrderId(passive_id);
            assert(passive_sess);
            if (passive_sess != nullptr) {
                // Don't send the same event twice, if both sides
                // come from the same session
                if (aggressor_sess != passive_sess) {
                    sendEventOnSess(*passive_sess);
                }
            }
        },
        [&](funx::match::RemoveOrder const& remove) {
            MatchingSession* sess = getSessionForOrderId(remove.order_id);
            assert(sess);
            if (sess != nullptr) {
                sendEventOnSess(*sess);
            }
        },
        [&](funx::match::IocMiss const& miss) {
            MatchingSession* sess = getSessionForOrderId(miss.order_id);
            assert(sess);
            if (sess != nullptr) {
                sendEventOnSess(*sess);
            }
        },
    }, e);

    // echo this event to all listeners
    for (ListenSession* s : session_tracker_.getListenSessions()) {
        s->sendEvent(event_timestamp, e);
    }

    LOG_INFO("{}", e);
}

void MatchingSession::sendTriggeredEvent(
    int64_t event_timestamp,
    funx::match::Event const& e)
{
    using namespace funx::match::proto;

    MessageT<MatchingEvent> msg{
        .body = {
            .req_seqnum = 0,
            .match_timestamp = event_timestamp,
            .event = SerializedEvent{e},
        }
    };
    client_conn_.send(funx::util::to_bytes(msg));
}

MatchingSession* MatchingSession::getSessionForOrderId(funx::OrderId id) const
{
    // These lookups should never fail. If it fails, we have an internal error

    auto order_lookup_res = order_tracker_.get(id);
    assert(order_lookup_res);
    if (!order_lookup_res) {
        LOG_ERR("Internal error. Matcher handled order, but the lookup failed: {}", id);
        return nullptr;
    }
    OrderTracker::OrderInfo const& order_info = **order_lookup_res;

    auto session_lookup = session_tracker_.get(order_info.client_tcp_details);
    assert(session_lookup);
    if (!session_lookup) {
        LOG_ERR("Internal error. Matcher handled order, but it's session was not found: {}", id);
        return nullptr;
    }
    return *session_lookup;
}

} // namespace funx::match
