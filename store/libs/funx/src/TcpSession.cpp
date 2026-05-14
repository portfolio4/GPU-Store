#include <funx/net/TcpSession.h>

#include <funx/log/Log.h>

#include <algorithm>
#include <cassert>

namespace funx {

TcpSession::TcpSession(Config c)
    : socket_(std::move(c.socket))
{
}

size_t TcpSession::numAvailableBytes() const {
    return buffer_end_ - buffer_start_;
}

size_t TcpSession::capacityLeft() const {
    return buffer_.size() - buffer_end_;
}

std::optional<funx::byte_view> TcpSession::peek(size_t bytes) {
    if (bytes > TcpSession::MTU) {
        LOG_ERR("Internal error. Trying to peek() for {} bytes which is "
                "larger than MTU {}.", bytes, TcpSession::MTU);
        return std::nullopt;
    }

    if (numAvailableBytes() >= bytes) {
        return funx::byte_view(&buffer_[buffer_start_], bytes);
    }

    // If we get here, then we don't have enough bytes available, so we have
    // to do a read

    // If we're out of append space and can't read more, then shift everything
    // back to the beginning. This guarantees we have enough append space now:
    //  * The buffer has at least 4 MTU's worth of space
    //  * peek() only allows sizes <= 1 MTU
    // So if we got here, then the buffer has less than 1 MTU worth of data
    // (else peek would've just returned earlier with the available data).
    // Thus, shifting it all back to the beginning means we have at least
    // 3 MTU's worth of space left
    //
    // There are more optimizations we can do for this class, but this
    // implementation is more straightforward to get right.
    // The data that we're shifting left is also always a fragment of a message.
    // Given that we aren't sending enormous messages around, such a
    // shift should be pretty cheap anyways.
    if (capacityLeft() < TcpSession::MTU) {
        std::move(
            buffer_.begin() + buffer_start_,
            buffer_.begin() + buffer_end_,
            buffer_.begin());
        buffer_end_ -= buffer_start_;
        buffer_start_ = 0;
    }
    assert(capacityLeft() >= TcpSession::MTU);

    size_t bytesRead = socket_.tryReadTo(&buffer_[buffer_end_]);
    buffer_end_ += bytesRead;

    if (numAvailableBytes() >= bytes) {
        return funx::byte_view(&buffer_[buffer_start_], bytes);
    } else {
        return std::nullopt;
    }
}

void TcpSession::consume(size_t bytes) {
    buffer_start_ += bytes;
    if (buffer_start_ > buffer_end_) {
        buffer_start_ = buffer_end_;
    }

    // reset to beginning anytime the buffer was completely exhausted
    if (buffer_start_ == buffer_end_) {
        buffer_start_ = 0;
        buffer_end_ = 0;
    }
}

void TcpSession::send(funx::byte_view bytes) {
    socket_.send(bytes);
}

void TcpSession::close() {
    socket_.close();
}

bool TcpSession::closed() const {
    return socket_.closed();
}


funx::TcpEndpointDetails TcpSession::getRemoteTcpEndpoint() const {
    return socket_.getRemoteTcpEndpoint();
}

} // namespace funx

