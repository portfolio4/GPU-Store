#ifndef FUNX_BENCHMARK_BENCHMARK_H
#define FUNX_BENCHMARK_BENCHMARK_H

#include <funx/clock/Clock.h>
#include <funx/common/types.h>
#include <funx/gateway/config.h>
#include <funx/gateway/ClientMultiplexer.h>
#include <funx/test/util.h>
#include <funx/util/overload.h>

#include <cassert>
#include <print>

namespace funx::benchmark {

void printSummaryStatistics(std::vector<int64_t> stats) {
    std::sort(stats.begin(), stats.end());
    const int N = stats.size();
    std::println("    Average: {}", std::accumulate(stats.begin(), stats.end(), 0ULL) / (1.0 * N));
    std::println("    min: {}", stats.front());
    std::println("     1 percentile: {}", stats[N *  1 / 100]);
    std::println("     5 percentile: {}", stats[N *  5 / 100]);
    std::println("    25 percentile: {}", stats[N * 25 / 100]);
    std::println("    50 percentile: {}", stats[N * 50 / 100]);
    std::println("    75 percentile: {}", stats[N * 75 / 100]);
    std::println("    95 percentile: {}", stats[N * 95 / 100]);
    std::println("    99 percentile: {}", stats[N * 99 / 100]);
    std::println("    max: {}", stats.back());
}

// Sends bids and asks for a given product
class Benchmark
{

public:
    struct Config {
        funx::gateway::ClientMultiplexer& gw_intf;
        funx::Product product;
    };

    Benchmark(Config const& c)
        : gw_intf_(c.gw_intf)
        , product_(c.product)
    {
    }

    void run() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        benchmarkSubmits();
        std::println("\n\n\n");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        benchmarkTrades();
        std::println("\n\n\n");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        benchmarkCancels();
    }

private:

    void benchmarkSubmits() {
        struct SubmitParams {
            flop::NewOrder order;
            funx::gateway::Client* bound_client;
            funx::Clock::time_point send_time{};
            std::optional<funx::Clock::time_point> ack_time{};
        };

        std::vector<SubmitParams> params;
        const int NUM_SUBMITS = 1000;
        for (int i = 0; i < NUM_SUBMITS; ++i) {
            flop::Side side = (i % 2 == 0) ? flop::Side::Buy : flop::Side::Sell;
            params.push_back(SubmitParams{
                .order = flop::NewOrder{
                    .product = +product_,
                    .side = side,
                    .qty = 100,
                    .price = (side == flop::Side::Buy ? 99 : 101),
                    .duration = flop::Duration::ImmediateOrCancel,
                    .client_order_id = nextClientOrderId(),
                },
                .bound_client = &gw_intf_.next(),
            });
        }

        for (SubmitParams& p : params) {
            p.bound_client->sendBasicNewOrder(p.order);
            p.send_time = funx::Clock::now();
            p.bound_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::OrderAccepted> const&) {
                    p.ack_time = funx::Clock::now();
                },
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));
            p.bound_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::OrderRemoved> const&) {},
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));

            assert(p.ack_time);
            assert(*p.ack_time > p.send_time);
        }

        std::vector<int64_t> submit_latencies;
        for (SubmitParams const& p : params) {
            submit_latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(*p.ack_time - p.send_time).count());
        }
        std::println("Sent {} submits.", NUM_SUBMITS);
        printSummaryStatistics(submit_latencies);
    }

    void benchmarkTrades() {
        struct TradeParams {
            flop::NewOrder buy;
            flop::NewOrder sell;
            funx::gateway::Client* buy_client;
            funx::gateway::Client* sell_client;
            funx::Clock::time_point aggressor_send_time{};
            std::optional<funx::Clock::time_point> passive_trade_time{};
        };

        std::vector<TradeParams> params;
        const int NUM_TRADES = 100;
        for (int i = 0; i < NUM_TRADES; ++i) {
            params.push_back(TradeParams{
                .buy = flop::NewOrder{
                    .product = +product_,
                    .side = flop::Side::Buy,
                    .qty = 100,
                    .price = 150,
                    .duration = flop::Duration::Day,
                    .client_order_id = nextClientOrderId(),
                },
                .sell = flop::NewOrder{
                    .product = +product_,
                    .side = flop::Side::Sell,
                    .qty = 100,
                    .price = 150,
                    .duration = flop::Duration::Day,
                    .client_order_id = nextClientOrderId(),
                },
                .buy_client = &gw_intf_.next(),
                .sell_client = &gw_intf_.next(),
            });
        }

        for (TradeParams& p : params) {
            assert(p.buy_client != p.sell_client);
            p.buy_client->sendBasicNewOrder(p.buy);
            p.buy_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::OrderAccepted> const&) {},
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));

            p.sell_client->sendBasicNewOrder(p.sell);
            p.aggressor_send_time = funx::Clock::now();

            // This is just an approximation.
            // Matching engine responds to the aggressor first, so the
            // passive trade time is an upper bound. To properly get both
            // passive and aggressive times, we need to coordinate two threads
            // pinned to their own CPUs. Because chances are the aggressive
            // and passive trades arrive around the same time and because the
            // client isn't optimized for latency, whatever message we read
            // second will get extremely high latency due to being blocked
            // by the first message.
            p.buy_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::Trade> const&) {
                    p.passive_trade_time = funx::Clock::now();
                },
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));

            p.sell_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::OrderAccepted> const&) {},
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));
            p.sell_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::Trade> const&) {},
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));

            // Poll the OrderRemoved messages off the queue
            p.buy_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::OrderRemoved> const&) {},
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));
            p.sell_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::OrderRemoved> const&) {},
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));

            assert(p.passive_trade_time);
            assert(*p.passive_trade_time > p.aggressor_send_time);
        }

        std::vector<int64_t> passive_trade_latencies;
        for (TradeParams const& p : params) {
            passive_trade_latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(*p.passive_trade_time - p.aggressor_send_time).count());
        }
        std::println("Generated {} trades. Latency stats for time to passive trade:", NUM_TRADES);
        printSummaryStatistics(passive_trade_latencies);
    }

    void benchmarkCancels() {
        struct CancelParams {
            flop::NewOrder order;
            flop::CancelOrder cancel;
            funx::gateway::Client* bound_client;
            funx::Clock::time_point cancel_time{};
            std::optional<funx::Clock::time_point> remove_time{};
        };

        std::vector<CancelParams> params;
        const int NUM_CANCELS = 1000;
        for (int i = 0; i < NUM_CANCELS; ++i) {
            flop::Side side = (i % 2 == 0) ? flop::Side::Buy : flop::Side::Sell;
            uint64_t client_order_id = nextClientOrderId();
            params.push_back(CancelParams{
                .order = flop::NewOrder{
                    .product = +product_,
                    .side = side,
                    .qty = 100,
                    .price = (side == flop::Side::Buy ? 99 : 101),
                    .duration = flop::Duration::Day,
                    .client_order_id = client_order_id,
                },
                .cancel = flop::CancelOrder{
                    .client_order_id = client_order_id,
                },
                .bound_client = &gw_intf_.next(),
            });
        }

        for (CancelParams& p : params) {
            p.bound_client->sendBasicNewOrder(p.order);
            p.bound_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::OrderAccepted> const&) {},
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));
        }

        for (CancelParams& p : params) {
            p.bound_client->sendCancelOrder(p.cancel);
            p.cancel_time = funx::Clock::now();
            p.bound_client->pollFor(funx::overloaded{
                [&](flop::MessageT<flop::OrderRemoved> const&) {
                    p.remove_time = funx::Clock::now();
                },
                [&](auto&&) {
                    assert(false);
                }
            }, std::chrono::seconds(1));

            assert(p.remove_time);
            assert(*p.remove_time > p.cancel_time);
        }

        std::vector<int64_t> cancel_latencies;
        for (CancelParams const& p : params) {
            cancel_latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(*p.remove_time - p.cancel_time).count());
        }
        std::println("Sent {} cancels.", NUM_CANCELS);
        printSummaryStatistics(cancel_latencies);
    }

    uint64_t nextClientOrderId() {
        return next_client_order_id_++;
    }

private:
    funx::gateway::ClientMultiplexer& gw_intf_;
    funx::Product product_;

    uint64_t next_client_order_id_ = 1;
};

} // namespace funx::benchmark

#endif  // FUNX_BENCHMARK_BENCHMARK_H

