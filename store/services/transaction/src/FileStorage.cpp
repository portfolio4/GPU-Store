#include <funx/transaction/FileStorage.h>
#include <funx/log/Log.h>
#include <funx/util/expected.h>
#include <filesystem>

namespace funx::transaction {

FileStorage::FileStorage(Config const& c)
{
    LOG_INFO("Using file storage at {}", c.filename);

    if (std::filesystem::exists(c.filename)) {
        LOG_INFO("Loading existing transactions");
        std::ifstream fin(c.filename, std::ios::in | std::ios::binary);

        if (!fin.is_open()) {
            throw std::runtime_error(std::format("Failed to open '{}'", c.filename));
        }

        int transactionIdx = 0;
        std::vector<std::byte> buf(1024);
        uint16_t transactionSize;
        while(fin.read(
            reinterpret_cast<char*>(&transactionSize),
            sizeof(transactionSize))
        ) {
            if (transactionSize > buf.size()) {
                buf.resize(transactionSize);
            }

            if (!fin.read(reinterpret_cast<char*>(buf.data()), transactionSize)) {
                LOG_WARN(
                    "Transaction {} is truncated: expected {} bytes, read {}",
                    transactionIdx,
                    transactionSize,
                    fin.gcount()
                );
                break;
            }

            auto res = loadStoredTransaction(funx::byte_view(buf.data(), transactionSize));
            if (!res) {
               LOG_WARN("Transaction {} malformatted: {}", transactionIdx, res.error());
            }
            ++transactionIdx;
        }

        LOG_INFO("Processed {} stored transactions (including any malformatted)",
                transactionIdx);
    }

    ofs_ = std::ofstream(c.filename, std::ios::app | std::ios::binary);
}

FileStorage::~FileStorage()
{
    ofs_.close();
}

std::expected<void, funx::transaction::proto::ErrorCode>
FileStorage::loadStoredTransaction(funx::byte_view bin_data)
{
    if (bin_data.size() != sizeof(funx::transaction::Transaction)) {
        LOG_WARN("loadStoredTransaction: Wrong size {}, expected {}",
                bin_data.size(), sizeof(funx::transaction::Transaction));
        return std::unexpected(
                funx::transaction::proto::ErrorCode::FileReadFailed);
    }

    auto const& tx = *reinterpret_cast<const funx::transaction::Transaction*>(bin_data.data());

    FUNX_TRY_VOID(saveInMemory(tx));
    return {};
}

std::expected<void, funx::transaction::proto::ErrorCode>
FileStorage::saveInMemory(funx::transaction::Transaction const& tx)
{
    if (txs_.contains(tx.txid)) {
        LOG_WARN("Cannot store duplicated "
                    "transaction ID '{}'. Manual intervention needed for {}",
                    tx.txid, tx);
        return std::unexpected(funx::transaction::proto::ErrorCode::DuplicateTxnId);
    }
    txs_[tx.txid] = tx;
    return {};
}

std::expected<void, funx::transaction::proto::ErrorCode>
FileStorage::save(funx::transaction::Transaction const& tx)
{
    FUNX_TRY_VOID(saveInMemory(tx));
    uint16_t size = sizeof(tx);
    ofs_.write(reinterpret_cast<const char*>(&size), sizeof(size));
    auto writeSizeFailed = ofs_.fail();
    ofs_.write(reinterpret_cast<const char*>(&tx), sizeof(tx));
    auto writeTxFailed = ofs_.fail();
    if (writeSizeFailed || writeTxFailed) {
        LOG_WARN("Failed to write transaction. "
                    "Manual intervention needed for {}", tx);
        return std::unexpected(funx::transaction::proto::ErrorCode::FileWriteFailed);
    }
    ofs_.flush();
    return {};
}

std::optional<funx::transaction::Transaction> FileStorage::query(funx::TransactionId txid)
{
    auto it = txs_.find(txid);
    if (it == txs_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace funx::transaction
