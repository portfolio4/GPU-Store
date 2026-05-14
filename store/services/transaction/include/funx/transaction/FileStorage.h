#ifndef FUNX_TRANSACTION_FILE_STORAGE_H
#define FUNX_TRANSACTION_FILE_STORAGE_H

#include <fstream>
#include <string>
#include <map>

#include <funx/common/types.h>
#include <funx/net/types.h>
#include <funx/transaction/proto.h>
#include <funx/transaction/StorageInterface.h>

namespace funx::transaction {

class FileStorage : public StorageInterface
{
public:

    struct Config {
        std::string filename;
    };

    FileStorage(Config const&);

    ~FileStorage();

    FileStorage(FileStorage const& other) = delete;
    FileStorage(FileStorage&& other) = delete;
    FileStorage& operator=(FileStorage const& other) = delete;
    FileStorage& operator=(FileStorage&& other) = delete;

    std::expected<void, funx::transaction::proto::ErrorCode>
    save(funx::transaction::Transaction const&) override;

    std::optional<funx::transaction::Transaction>
    query(funx::TransactionId) override;

    size_t size() const {
        return txs_.size();
    }

private:

    std::expected<void, funx::transaction::proto::ErrorCode>
    loadStoredTransaction(funx::byte_view bin_data);

    std::expected<void, funx::transaction::proto::ErrorCode>
    saveInMemory(funx::transaction::Transaction const& tx);

    std::map<funx::TransactionId, funx::transaction::Transaction> txs_;
    std::ofstream ofs_;
};

} // namespace funx::transaction

#endif  // FUNX_TRANSACTION_FILE_STORAGE_H

