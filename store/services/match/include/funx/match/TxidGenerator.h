#ifndef FUNX_MATCH_TXID_GENERATOR_H
#define FUNX_MATCH_TXID_GENERATOR_H

#include <funx/common/types.h>

namespace funx::match {

class TxidGenerator
{
public:
    virtual funx::TransactionId nextTxid() = 0;
};

// Top 8 bytes is an ID unique to the matching service.
// Bottom 56 bytes is the lower 56 bytes of the epoch timestamp in microseconds.
// Note: microseconds is nanoseconds / 1000. Since we've divided by 1000 and
// 1000 is larger than 2^8, the top 8 bytes must now be 0. Thus, we can
// reuse the top 8 bytes.
//
// Should be rare, but if multiple calls happen in the same microsecond,
// we will increment by 1 to keep things unique.
//
// We can't possibly have a transaction every single microsecond (or if we do,
// we'd be making so much money off fees that we'd be happy to solve this
// uniqueness problem better), the transaction ID should be unique across every
// ~2000 year period, so we have no concerns about ever having duplicates.
class StandardTxidGenerator : public TxidGenerator
{
public:
    struct Config {
        uint8_t matching_service_unique_id;
    };

    StandardTxidGenerator(Config const&);

    funx::TransactionId nextTxid() override;

private:
    const uint64_t matching_service_id_mask_;
    uint64_t last_txid_ = 0;
};

} // namespace funx::match

#endif  // FUNX_MATCH_TXID_GENERATOR_H

