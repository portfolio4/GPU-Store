#include <funx/match/Client.h>
#include <funx/match/proto.h>
#include <funx/net/TcpSocket.h>
#include <funx/net/util.h>
#include <funx/util/overload.h>

class MatchingTestClient
{
public:
    MatchingTestClient(
            funx::match::MatchingServiceConfig const& matching_service_cfg,
            funx::match::Client::Mode mode = funx::match::Client::Mode::Client)
        : client_(funx::match::Client::Config{
            .matching_service_cfg = matching_service_cfg,
            .mode = mode
          })
    {
    }

    // Returns the message that was sent out
    template <typename T>
    funx::match::proto::MessageT<T> send(T const& body) {
        using funx::match::proto::MessageT;

        MessageT<T> msg{
            .body = body,
        };
        msg.hdr.seqnum = next_seqnum_++;
        client_.send(funx::util::to_bytes(msg));
        return msg;
    }

    uint64_t addOrder(funx::match::Order const& order) {
        return client_.addOrder(order);
    }

    uint64_t cancelOrder(funx::OrderId order_id) {
        return client_.cancelOrder(order_id);
    }

    void close() {
        client_.close();
    }

    void expectNoEvents() {
        client_.poll(funx::overloaded{
            [&](auto&&) {
                FAIL("Got event, when expecting no event");
            }
        });
    }

    void expectEvents(
            std::vector<funx::match::Event> const& events,
            std::optional<uint64_t> expected_req_seqnum = std::nullopt)
    {
        using funx::match::Event;
        using funx::match::SerializedEvent;
        using funx::match::proto::MessageT;
        using funx::match::proto::MsgType;
        using funx::match::proto::MatchingEvent;

        for (Event const& e : events) {
            auto matching_event = requireResponse<MatchingEvent>();
            if (expected_req_seqnum) {
                REQUIRE(matching_event.req_seqnum == *expected_req_seqnum);
            }
            switch (matching_event.event.type)
            {
            case SerializedEvent::EventType::AddOrder:
                REQUIRE(e == Event{matching_event.event.data.add});
                break;
            case SerializedEvent::EventType::Match: {
                // we cannot reliably predict the transaction ID
                // because it's based on the epoch time. So ignore this
                // in validation
                funx::match::Match match_without_txid = matching_event.event.data.match;
                match_without_txid.txid = funx::TransactionId{0};
                REQUIRE(e == Event{match_without_txid});
                break;
            }
            case SerializedEvent::EventType::RemoveOrder:
                REQUIRE(e == Event{matching_event.event.data.remove});
                break;
            case SerializedEvent::EventType::IocMiss:
                REQUIRE(e == Event{matching_event.event.data.ioc_miss});
                break;
            }
        }
    }

    void expectReject(uint64_t req_seqnum, funx::match::proto::ErrorCode error)
    {
        auto reject = requireResponse<funx::match::proto::Reject>();
        REQUIRE(reject.req_seqnum == req_seqnum);
        REQUIRE(static_cast<funx::match::proto::ErrorCode>(reject.error) == error);
    }

    void addOrderAndExpectEvents(
            funx::match::Order const& order,
            std::vector<funx::match::Event> const& events)
    {
        uint64_t req_seqnum = addOrder(order);
        expectEvents(events, req_seqnum);
    }

    void addOrderAndExpectReject(
            funx::match::Order const& order,
            funx::match::proto::ErrorCode error)
    {
        uint64_t req_seqnum = addOrder(order);
        expectReject(req_seqnum, error);
    }

    void cancelOrderAndExpectEvents(
            funx::OrderId order_id,
            std::vector<funx::match::Event> const& events)
    {
        uint64_t req_seqnum = cancelOrder(order_id);
        expectEvents(events, req_seqnum);
    }

    void cancelOrderAndExpectReject(
            funx::OrderId order_id,
            funx::match::proto::ErrorCode error)
    {
        uint64_t req_seqnum = cancelOrder(order_id);
        expectReject(req_seqnum, error);
    }

    template <typename T>
    T requireResponse() {
        auto stop = funx::Clock::now() + response_timeout_;
        do {
            std::optional<T> rtn;
            client_.poll(funx::overloaded{
                [&](T const& t) {
                    rtn = t;
                },
                [](auto&&) {}
            });
            if (rtn) {
                return *rtn;
            }
        } while (funx::Clock::now() < stop);
        FAIL("expecting response, but didn't get one before timeout");
        return {};
    }

private:

    funx::match::Client client_;
    std::chrono::milliseconds response_timeout_ = std::chrono::milliseconds(1000);
    uint64_t next_seqnum_ = 1;
};

