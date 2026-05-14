#include <funx/match/TxidGenerator.h>

#include <funx/clock/Clock.h>

namespace funx::match {

    StandardTxidGenerator::StandardTxidGenerator(StandardTxidGenerator::Config const& c)
    : matching_service_id_mask_(static_cast<uint64_t>(c.matching_service_unique_id) << 56)
{
}

funx::TransactionId StandardTxidGenerator::nextTxid()
{
    uint64_t timestamp_mics = static_cast<uint64_t>(funx::SteadyClock::now_ns()) / 1000;
    uint64_t txid = timestamp_mics | matching_service_id_mask_;
    if (txid <= last_txid_) {
        txid = last_txid_ + 1;
    }
    last_txid_ = txid;
    return funx::TransactionId{txid};
}

} // namespace funx::match

