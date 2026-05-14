#ifndef FUNX_TRANSACTION_TEST_MOCKS_H
#define FUNX_TRANSACTION_TEST_MOCKS_H

#include <funx/common/types.h>
#include <funx/transaction/Database.h>
#include <funx/transaction/proto.h>
#include <funx/transaction/StorageInterface.h>

#include <expected>
#include <map>
#include <thread>
#include <iostream>

namespace funx::transaction::test {

class MockStorage : public StorageInterface
{
public:
    struct Config{
        bool allow_duplicate_txids;
    };

    MockStorage(Config const& c)
        : allow_duplicate_txids_(c.allow_duplicate_txids)
    {}

    std::expected<void, funx::transaction::proto::ErrorCode>
        save(funx::transaction::Transaction const& tx) override
    {
        if (!allow_duplicate_txids_ && txs_.contains(tx.txid)) {
            return std::unexpected(funx::transaction::proto::ErrorCode::DuplicateTxnId);
        }
        txs_[tx.txid] = tx;
        return {};
    }

    std::optional<funx::transaction::Transaction>
        query(funx::TransactionId txid) override
    {
        auto it = txs_.find(txid);
        if (it == txs_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // For tests that want to alter the state of the stored transactions
    std::map<funx::TransactionId, funx::transaction::Transaction>& getTxs() {
        return txs_;
    }

private:
    bool allow_duplicate_txids_;
    std::map<funx::TransactionId, funx::transaction::Transaction> txs_;
};

class RunMockDb
{
public:
    struct Config{
        std::string server_ip4 = "127.0.0.1";
        funx::TcpPort server_port{9999};
        std::chrono::milliseconds max_run_time = std::chrono::seconds(5);
        bool allow_duplicate_txids = true;
    };

    const Config cfg;
    MockStorage storage;
    Database database;
    const funx::Clock::time_point stop_time;

    RunMockDb(Config const& c)
        : cfg(c)
        , storage(MockStorage::Config{
            .allow_duplicate_txids = c.allow_duplicate_txids
          })
        , database(Database::Config{
            .storage = &storage,
            .port = c.server_port,
         })
        , stop_time(funx::Clock::now() + c.max_run_time)
        , run_thread_([&]() {
            while (!finished_ && funx::Clock::now() < stop_time) {
                database.poll();
            }
         })
    {
    }

    RunMockDb()
        : RunMockDb(Config{})
    {}

    ~RunMockDb() {
        finished_ = true;
        run_thread_.join();
    }

    RunMockDb(RunMockDb const& other) = delete;
    RunMockDb(RunMockDb&& other) = delete;
    RunMockDb& operator=(RunMockDb const& other) = delete;
    RunMockDb& operator=(RunMockDb&& other) = delete;

    TransactionDbAppConfig getTransactionDbAppConfig() {
        return TransactionDbAppConfig{
            .file_storage_path = "/dev/null",
            .server_ip4 = cfg.server_ip4,
            .server_port = cfg.server_port,
        };
    }

private:
    bool finished_ = false;
    std::thread run_thread_;
};

} // namespace funx::transaction::test

#endif  // FUNX_TRANSACTION_TEST_MOCKS_H

