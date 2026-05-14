#include <catch2/catch_all.hpp>
#include <funx/test/scratch_dir.h>
#include <funx/transaction/test/defaults.h>
#include <funx/transaction/FileStorage.h>
#include <funx/transaction/proto.h>
#include <print>

using namespace funx::transaction;

TEST_CASE("Test Transaction FileStorage", "[transaction]")
{
    funx::test::ScratchDir scratch;

    FileStorage::Config cfg{
        .filename = scratch.path() / "file_storage.bin"
    };
    FileStorage s(cfg);


    uint64_t nextTxid = 184753;
    auto newTx = [&]() {
        funx::transaction::Transaction tx = funx::test::defaults::transaction::TRANSACTION;
        tx.txid = funx::TransactionId{nextTxid++};
        return tx;
    };

    SECTION("Saves transactions") {
        auto tx1 = newTx();
        auto tx2 = newTx();
        auto tx3 = newTx();
        REQUIRE(s.save(tx1));
        REQUIRE(s.save(tx2));
        REQUIRE(s.save(tx3));

        auto query1 = s.query(tx1.txid);
        auto query2 = s.query(tx2.txid);
        auto query3 = s.query(tx3.txid);

        REQUIRE(query1);
        REQUIRE(*query1 == tx1);
        REQUIRE(query2);
        REQUIRE(*query2 == tx2);
        REQUIRE(query3);
        REQUIRE(*query3 == tx3);
    }

    SECTION("Rejects duplicate transaction ID") {
        auto tx = newTx();
        auto dupeTxid = tx;
        dupeTxid.product = funx::Product{(+tx.product) + 1};
        dupeTxid.buyerId = funx::CompanyId{(+tx.buyerId) + 1};
        dupeTxid.sellerId = funx::CompanyId{(+tx.sellerId) + 3};

        REQUIRE(s.save(tx));
        REQUIRE_FALSE(s.save(dupeTxid));
    }

    SECTION("Save a lot of transactions and reload them") {
        std::vector<funx::transaction::Transaction> txs;

        {
            FileStorage reloadedStorage(cfg);
            for (int i = 0; i < 1000; ++i) {
                auto tx = newTx();
                txs.push_back(tx);
                REQUIRE(s.save(tx));
            }
        }

        {
            FileStorage reloadedStorage(cfg);
            for (auto const& tx : txs) {
                auto query = reloadedStorage.query(tx.txid);
                REQUIRE(query);
                REQUIRE(*query == tx);
            }
        }
    }

    SECTION("Saves and loads transaction many times") {
        std::vector<funx::transaction::Transaction> txs;

        for (int i = 0; i < 30; ++i) {
            {
                FileStorage reloadedStorage(cfg);

                // add 20 new transactions
                for (int j = 0; j < 10; ++j) {
                    auto tx = newTx();
                    txs.push_back(tx);
                    REQUIRE(reloadedStorage.save(txs.back()));
                }
            }

            {
                // load everything again and make sure everything is still there
                FileStorage reloadedStorage(cfg);

                for (auto const& tx : txs) {
                    auto query = reloadedStorage.query(tx.txid);
                    REQUIRE(query);
                    REQUIRE(*query == tx);
                }
            }
        }
    }

    // Note: even if part of the file is corrupted, we may not be able to
    // recover the transactions afterwards because we write the size in bytes
    // before each transaction. If the size got corrupted, then there is no
    // way to read the file anymore
    // Note: we can't easily test partially recovering some transactions because
    //
    SECTION("Ignores completely corrupt file") {
        std::ofstream ofs(cfg.filename);
        // start with a 0 sized transaction
        ofs << 0 << 0;

        // then put in garbage bytes
        for (char i = 0; i < 100; ++i) {
            ofs << i;
        }
        ofs.close();
        FileStorage reloadedStorage(cfg);
        REQUIRE(reloadedStorage.size() == 0);
    }

    scratch.cleanup();
}
