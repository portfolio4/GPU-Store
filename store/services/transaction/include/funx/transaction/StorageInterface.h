#ifndef FUNX_TRANSACTION_STORAGE_INTERFACE_H
#define FUNX_TRANSACTION_STORAGE_INTERFACE_H

#include <funx/transaction/proto.h>
#include <expected>
#include <optional>
#include <string>

namespace funx::transaction {

// Interface to something that can persist a transaction to disk.
// E.g. save to file, or save to SQL database
class StorageInterface
{
public:
    virtual std::expected<void, funx::transaction::proto::ErrorCode>
    save(funx::transaction::Transaction const&) = 0;

    virtual std::optional<funx::transaction::Transaction>
    query(funx::TransactionId) = 0;
};

} // namespace funx::transaction

#endif  // FUNX_TRANSACTION_STORAGE_INTERFACE_H

