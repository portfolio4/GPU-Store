#ifndef FUNX_MATCH_TEST_MOCKS_H
#define FUNX_MATCH_TEST_MOCKS_H

#include <funx/test/defaults.h>
#include <funx/transaction/test/mocks.h>
#include <funx/match/config.h>
#include <funx/match/MatchingService.h>

#include <chrono>
#include <thread>

namespace funx::match::test {

class RunMockMatchingService
{
public:
    struct Config{
        uint8_t unique_id = 1;
        std::vector<funx::Product> products = {
            funx::Product{1},
            funx::Product{2},
            funx::Product{1079},
            funx::test::defaults::PRODUCT,
        };
        std::string server_ip4 = "127.0.0.1";
        funx::TcpPort server_port{9998};
        funx::TcpPort listen_server_port{9997};
        std::chrono::milliseconds max_run_time = std::chrono::seconds(5);
        funx::transaction::test::RunMockDb::Config mock_db_cfg = {};
    };

    const Config cfg;
    funx::transaction::test::RunMockDb mock_db;
    const MatchingServiceConfig matching_service_cfg;
    MatchingService matching_service;

    const funx::Clock::time_point stop_time;

    RunMockMatchingService(Config const& c)
        : cfg(c)
        , mock_db(c.mock_db_cfg)
        , matching_service_cfg(MatchingServiceConfig{
            .unique_id = c.unique_id,
            .products = c.products,
            .server_ip4 = "127.0.0.1",
            .server_port = c.server_port,
            .listen_server_port = c.listen_server_port,
            .transaction_db_app_cfg = mock_db.getTransactionDbAppConfig(),
          })
        , matching_service(matching_service_cfg)
        , stop_time(funx::Clock::now() + c.max_run_time)
        , run_thread_([&]() {
            while (!finished_ && funx::Clock::now() < stop_time) {
                matching_service.poll();
            }
          })
    {
    }

    RunMockMatchingService()
        : RunMockMatchingService(Config{})
    {}

    ~RunMockMatchingService() {
        finished_ = true;
        run_thread_.join();
    }

    RunMockMatchingService(RunMockMatchingService const& other) = delete;
    RunMockMatchingService(RunMockMatchingService const&& other) = delete;
    RunMockMatchingService& operator=(RunMockMatchingService const& other) = delete;
    RunMockMatchingService& operator=(RunMockMatchingService&& other) = delete;

    funx::match::MatchingServiceConfig getMatchingServiceConfig() {
        return matching_service_cfg;
    }

private:
    bool finished_ = false;
    std::thread run_thread_;
};

} // namespace funx::match::test

#endif // FUNX_MATCH_TEST_MOCKS_H

