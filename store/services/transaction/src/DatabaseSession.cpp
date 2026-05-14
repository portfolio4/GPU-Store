#include <funx/transaction/DatabaseSession.h>
#include <funx/log/Log.h>
#include <funx/net/util.h>

namespace funx::transaction {

DatabaseSession::DatabaseSession(Config c)
    : storage_(c.storage)
    , client_conn_(funx::TcpSession::Config{.socket=std::move(c.client_conn)})
{
}

void DatabaseSession::poll()
{
    pollClient();
}

void DatabaseSession::pollClient()
{
    using funx::transaction::proto::Header;

    std::optional<const Header*> maybe_hdr = client_conn_.peek<Header>();
    if (!maybe_hdr) {
        return;
    }

    Header const& hdr = **maybe_hdr;

    if (hdr.magic != Header::MAGIC) {
        LOG_WARN("Disconnecting client sending wrong magic value {}", hdr.magic);
        client_conn_.close();
        return;
    }

    using funx::transaction::proto::MessageT;
    using funx::transaction::proto::MsgType;
    using funx::transaction::proto::SaveTransaction;
    using funx::transaction::proto::QueryTransaction;
    switch(hdr.msg_type)
    {
    case MsgType::SaveTransaction:
        {
        auto maybe_save = client_conn_.peek<MessageT<SaveTransaction>>();
        if (!maybe_save) {
            return;
        }
        SaveTransaction const& save = (*maybe_save)->body;
        handleSaveTransaction(hdr, save);
        client_conn_.consume(sizeof(MessageT<SaveTransaction>));
        return;
        }
    case MsgType::QueryTransaction:
        {
        auto maybe_query = client_conn_.peek<MessageT<QueryTransaction>>();
        if (!maybe_query) {
            return;
        }
        QueryTransaction const& query = (*maybe_query)->body;
        handleQueryTransaction(hdr, query);
        client_conn_.consume(sizeof(MessageT<QueryTransaction>));
        return;
        }
    default:
        LOG_WARN("Disconnecting client sending unexpected message type {}",
                std::to_underlying(hdr.msg_type));
        client_conn_.close();
        return;
    }
}

void DatabaseSession::handleSaveTransaction(
        funx::transaction::proto::Header const& hdr,
        funx::transaction::proto::SaveTransaction const& save)
{
    using funx::transaction::proto::MessageT;
    auto res = storage_->save(save.tx);
    if (res) {
        MessageT<funx::transaction::proto::Ack> resp {
            .body = {
                .req_seqnum = hdr.seqnum,
            }
        };
        client_conn_.send(funx::util::to_bytes(resp));
        LOG_INFO("SAVE {}", save.tx);
    } else {
        MessageT<funx::transaction::proto::Error> resp {
            .body = {
                .req_seqnum = hdr.seqnum,
                .error = res.error(),
            }
        };
        client_conn_.send(funx::util::to_bytes(resp));
        LOG_WARN("Failed to save {}", save.tx);
    }
}

void DatabaseSession::handleQueryTransaction(
        funx::transaction::proto::Header const& hdr,
        funx::transaction::proto::QueryTransaction const& query)
{
    using funx::transaction::proto::MessageT;
    auto res = storage_->query(query.txid);
    if (res) {
        MessageT<funx::transaction::proto::TransactionResponse> resp {
            .body = {
                .req_seqnum = hdr.seqnum,
                .tx = *res,
            }
        };
        client_conn_.send(funx::util::to_bytes(resp));
    } else {
        MessageT<funx::transaction::proto::Error> resp {
            .body = {
                .req_seqnum = hdr.seqnum,
                .error = funx::transaction::proto::ErrorCode::QueryFailed,
            }
        };
        client_conn_.send(funx::util::to_bytes(resp));
    }
}

} // namespace funx::transaction

