#ifndef API_FUNX_TRANSACTION_PROTO_H
#define API_FUNX_TRANSACTION_PROTO_H

#include <funx/common/types.h>

#include <type_traits>

namespace funx::transaction {

// A transaction that occurred in the store
struct [[gnu::packed]] Transaction {
    funx::Product product;
    funx::Quantity qty;
    funx::Price price;
    funx::TransactionId txid;
    funx::CompanyId buyerId;
    funx::CompanyId sellerId;

    // TODO settlement date

    bool operator==(Transaction const& other) const = default;
};

namespace proto {

enum class MsgType {
    SaveTransaction = 1,
    QueryTransaction,
    TransactionResponse,
    Ack,
    Error
};

enum class ErrorCode {
    DuplicateTxnId = 1,
    FileReadFailed,
    FileWriteFailed,
    QueryFailed,
    ReceiveTimeout
};

struct [[gnu::packed]] SaveTransaction {
    constexpr static MsgType TYPE = MsgType::SaveTransaction;
    Transaction tx;
};

struct [[gnu::packed]] QueryTransaction {
    constexpr static MsgType TYPE = MsgType::QueryTransaction;
    funx::TransactionId txid;
};

struct [[gnu::packed]] TransactionResponse {
    constexpr static MsgType TYPE = MsgType::TransactionResponse;
    uint64_t req_seqnum;
    Transaction tx;
};

struct Ack {
    constexpr static MsgType TYPE = MsgType::Ack;
    uint64_t req_seqnum;
};

struct [[gnu::packed]] Error {
    constexpr static MsgType TYPE = MsgType::Error;
    uint64_t req_seqnum;
    ErrorCode error;
};

struct [[gnu::packed]] Header {
    constexpr static uint32_t MAGIC = 0xDB01;
    const uint32_t magic = MAGIC;
    uint64_t seqnum;
    MsgType msg_type;
};

struct [[gnu::packed]] Message {
    Header hdr;
    char payload[];
};

template <typename T>
struct [[gnu::packed]] MessageT {
    Header hdr {
        .seqnum = 0,
        .msg_type = T::TYPE,
    };
    T body;
};

} // namespace proto

} // namespace funx::transaction

template <>
struct std::formatter<funx::transaction::proto::ErrorCode> :
        std::formatter<std::string>
{
    auto format(funx::transaction::proto::ErrorCode code, format_context& ctx) const {
        return formatter<string>::format(std::format("{}", std::to_underlying(code)), ctx);
    }
};

template <>
struct std::formatter<funx::transaction::Transaction> : std::formatter<std::string> {
    auto format(funx::transaction::Transaction const& tx, format_context& ctx) const {
        return formatter<string>::format(
            std::format("Transaction{{"
                    "product={}"
                    ", qty={}"
                    ", price={}"
                    ", txid={}"
                    ", buyerId={}"
                    ", sellerId={}"
                    "}}"
                    , tx.product
                    , tx.qty
                    , tx.price
                    , tx.txid
                    , tx.buyerId
                    , tx.sellerId
            )
            , ctx
        );
    }
};

#endif  // API_FUNX_TRANSACTION_PROTO_H

