#include <catch2/catch_all.hpp>
#include <funx/test/scratch_dir.h>
#include <funx/transaction/Client.h>
#include <funx/transaction/Database.h>
#include <funx/transaction/test/defaults.h>
#include <funx/transaction/FileStorage.h>

#include <atomic>
#include <thread>
#include <map>

using namespace funx::transaction;

TEST_CASE("Test Transaction Database", "[transaction]")
{
    funx::test::ScratchDir scratch;

    FileStorage::Config fs_cfg{
        .filename = scratch.path() / "file_storage.bin"
    };
    FileStorage fs(fs_cfg);

    const funx::TcpPort DB_PORT{9999};
    Database::Config db_cfg{
        .storage = &fs,
        .port = DB_PORT,
    };
    Database db(db_cfg);

    bool test_finished = false;

    std::thread db_thread([&]() {
        // 5 second timeout for all tests, but they should all
        // terminate much sooner
        auto stop = funx::Clock::now() + std::chrono::seconds(5);
        while (!test_finished && funx::Clock::now() < stop) {
            db.poll();
        }
    });

    funx::transaction::TransactionDbAppConfig db_app_cfg{
        .file_storage_path = fs_cfg.filename,
        .server_ip4 = "127.0.0.1",
        .server_port = db_cfg.port,
    };
    funx::transaction::Client test_client(
        funx::transaction::Client::Config{
            .transaction_db_app_cfg = db_app_cfg
        }
    );

    using funx::transaction::proto::SaveTransaction;
    using funx::transaction::proto::QueryTransaction;
    using funx::transaction::proto::ErrorCode;

    SECTION("One client correctness testing")
    {
        SECTION("Save and query a few transactions") {
            int NUM_TRANSACTIONS = 10;
            std::vector<Transaction> transactions;
            transactions.push_back(funx::test::defaults::transaction::TRANSACTION);
            for (int i = 1; i < NUM_TRANSACTIONS; ++i) {
                transactions.push_back(transactions.back());
                transactions.back().txid = funx::TransactionId{+transactions.back().txid + 1};
            }

            for (auto const& tx : transactions) {
                REQUIRE(test_client.save(tx));
            }

            for (auto const& tx : transactions) {
                auto query_res = test_client.query(tx.txid);
                REQUIRE(query_res);
                REQUIRE(*query_res == tx);
            }

            funx::TransactionId fail_id = funx::TransactionId{+transactions.back().txid + 1};
            for (int i = 0; i < 10; ++i) {
                auto query_res = test_client.query(fail_id);
                REQUIRE(!query_res);
                REQUIRE(query_res.error() == ErrorCode::QueryFailed);
            }
        }

        SECTION("Duplicate transaction ID") {
            auto tx = funx::test::defaults::transaction::TRANSACTION;
            REQUIRE(test_client.save(tx));

            for (int i = 0; i < 10; ++i) {
                auto save_res = test_client.save(tx);
                REQUIRE(!save_res);
                REQUIRE(save_res.error() == ErrorCode::DuplicateTxnId);
            }
        }
    }

    SECTION("Many clients load testing")
    {
        // Launch 10 clients each saving 1000 transactions and querying them
        const int NUM_CLIENTS = 10;
        const int TXS_PER_CLIENT = 1000;

        // Generate randomized test data
        std::set<funx::TransactionId> added_txids;
        std::vector<funx::transaction::Transaction> test_data;
        test_data.reserve(NUM_CLIENTS * TXS_PER_CLIENT);
        for (int i = 0; i < NUM_CLIENTS * TXS_PER_CLIENT; ++i) {
            funx::transaction::Transaction tx;
            do {
                tx = funx::test::defaults::transaction::randomTransaction();
            } while (added_txids.contains(tx.txid));
            test_data.push_back(tx);
            added_txids.insert(tx.txid);
        }

        // Initial tcp connections (so we aren't establishing this during
        // the load test)
        std::vector<Client> test_clients;
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            test_clients.emplace_back(Client::Config{
                .transaction_db_app_cfg = db_app_cfg,
                .response_timeout = std::chrono::seconds(5),
            });
        }

        // actual load testing
        std::vector<std::thread> client_threads;
        // note: catch can only support the main thread running REQUIRE/CHECK
        // so this flag gets set to true if any thread had an issue
        std::atomic<bool> client_had_issue{false};
        client_threads.reserve(NUM_CLIENTS);
        for (int clientId = 0; clientId < NUM_CLIENTS; ++clientId) {
            client_threads.emplace_back([&, clientId]() {
                auto& test_client = test_clients[clientId];
                // server is going to get overloaded a bit and take some time
                // to respond since we haven't pinned things to specific cores
                for (int i = 0; i < TXS_PER_CLIENT; ++i) {
                    int tx_idx = clientId * TXS_PER_CLIENT + i;
                    if (static_cast<size_t>(tx_idx) >= test_data.size()) {
                        client_had_issue.store(true);
                        return;
                    }
                    auto& tx = test_data[tx_idx];
                    if (!test_client.save(tx)) {
                        client_had_issue.store(true);
                        return;
                    }
                }
            });
        }
        for (auto& thread : client_threads) {
            thread.join();
        }

        CHECK(!client_had_issue.load());
    }

    test_finished = true;
    db_thread.join();
    scratch.cleanup();
}
