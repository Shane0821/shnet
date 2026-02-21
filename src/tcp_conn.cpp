#include "shnet/tcp_conn.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "shnet/event_loop.h"

namespace shnet {

inline void TcpConn::ioTrampoline(void* obj, uint32_t events) {
    static_cast<TcpConn*>(obj)->handleIO(events);
}

TcpConn::TcpConn(int fd, EventLoop* loop) : conn_sk_(fd), ev_loop_(loop), closed_(false) {
    conn_sk_.setNonBlocking();
    conn_sk_.setKeepAlive();
    io_handler_ = EventLoop::EventHandler{this, &ioTrampoline};
    if (ev_loop_->addEvent(fd, EPOLLIN | EPOLLRDHUP, &io_handler_) < 0) [[unlikely]] {
        SHLOG_ERROR("failed to register connection fd {} to epoll: {}", fd, errno);
        close();
    }
}

TcpConn::~TcpConn() {
    if (closed_) [[unlikely]] {
        return;
    }
    const int fd = conn_sk_.fd();
    SHLOG_INFO("Tcpconn close: {}", fd);
    closed_ = true;
    ev_loop_->delEvent(fd);
    if (close_cb_) [[likely]] {
        close_cb_(fd);
    }
    conn_sk_.close();
}

void TcpConn::removeFromServer() {
    if (removed_) [[unlikely]] {
        return;
    }
    removed_ = true;
    SHLOG_INFO("removing connection fd {} from tcp server", conn_sk_.fd());
    remove_conn_handler_(conn_sk_.fd());
}

void TcpConn::close() {
    if (closed_) [[unlikely]] {
        return;
    }

    const int fd = conn_sk_.fd();
    SHLOG_INFO("TcpConn close: {}", fd);

    closed_ = true;

    // Ensure epoll no longer references our in-object handler pointer.
    ev_loop_->delEvent(fd);

    // Drop server ownership (may destroy this object if nobody else holds it).
    removeFromServer();

    if (close_cb_) {
        close_cb_(fd);
    }

    conn_sk_.close();
}

Message TcpConn::readAll() {
    auto ret = rcv_buf_.getAllData();
    rcv_buf_.readCommit(ret.size_);
    return ret;
}

Message TcpConn::readUntil(char terminator) {
    auto ret = rcv_buf_.getDataUntil(terminator);
    if (ret.data_ != nullptr) {
        // Consume the delimiter as well while returning line content only.
        rcv_buf_.readCommit(ret.size_ + 1);
    }
    return ret;
}

Message TcpConn::readn(size_t n) {
    auto ret = rcv_buf_.getData(n);
    rcv_buf_.readCommit(ret.size_);
    return ret;
}

void TcpConn::handleIO(uint32_t events) {
    if (closed_) [[unlikely]] {
        return;
    }

    // Keep this connection alive through shared_from_this()
    auto self_ptr = shared_from_this();

    if (events & (EPOLLERR | EPOLLHUP)) [[unlikely]] {
        SHLOG_ERROR("connection fd {} got error/hup events: {}", conn_sk_.fd(), events);
        close();
        return;
    }

    const bool got_rdhup = (events & EPOLLRDHUP);
    if (got_rdhup) [[unlikely]] {
        SHLOG_INFO("connection fd {} got rdhup events: {}", conn_sk_.fd(), events);
        peer_shutdown_ = true;
    }

    // Drain reads on RDHUP too (there may be remaining bytes).
    if ((events & EPOLLIN) || got_rdhup) handleRead();
    if (events & EPOLLOUT) handleWrite();

    // If peer won't send more and we have nothing left to do, close.
    if (peer_shutdown_ && rcv_buf_.readableSize() == 0 && snd_buf_.empty()) {
        close();
    }
}

void TcpConn::handleRead() {
    if (closed_) [[unlikely]] {
        SHLOG_WARN("handle read on closed connection fd {}", conn_sk_.fd());
        return;
    }

    for (;;) {
        size_t len = rcv_buf_.writableSize();
        if (len == 0) [[unlikely]] {
            rcv_buf_.shrink();
            len = rcv_buf_.writableSize();
            if (len == 0) {
                break;  // buffer full; wait for application to consume
            }
        }

        const ssize_t n = conn_sk_.read(rcv_buf_.writePointer(), len);
        if (n > 0) [[likely]] {
            rcv_buf_.writeCommit(static_cast<size_t>(n));
            continue;  // try to drain until EAGAIN
        }

        if (n == 0) [[unlikely]] {
            // Peer performed FIN (half-close); we may still write until we decide to
            // close.
            peer_shutdown_ = true;
            break;
        }

        const int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            break;
        }

        SHLOG_ERROR("handle read failed on fd {}: {}", conn_sk_.fd(), err);
        close();
        return;
    }

    if (rcv_buf_.readableSize() > 0) [[likely]] {
        if (read_cb_) [[likely]] {
            read_cb_(shared_from_this());
        }
    }
}

void TcpConn::handleWrite() {
    if (closed_) [[unlikely]] {
        SHLOG_WARN("handle write on closed connection fd {}", conn_sk_.fd());
        return;
    }
    while (!snd_buf_.empty()) {
        auto n =
            conn_sk_.send(snd_buf_.readPointer(), snd_buf_.readableSize(), MSG_NOSIGNAL);

        if (n > 0) [[likely]] {
            snd_buf_.readCommit(n);
            continue;
        }

        if (n < 0) [[unlikely]] {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket send buffer is full; wait for the next EPOLLOUT.
                return;
            }
            SHLOG_ERROR("handle write failed on fd {}: {}", conn_sk_.fd(), errno);
            close();
            return;
        }

        // send() returning 0 is unexpected here (len > 0); avoid a busy loop.
        SHLOG_WARN("send() returned 0 on fd {}: {}", conn_sk_.fd(), n);
        break;
    }

    if (snd_buf_.empty()) {
        disableWrite();
    }
}

ssize_t TcpConn::send(const char* data, size_t size) {
    if (size == 0) [[unlikely]] {
        return 0;
    }
    if (!data) [[unlikely]] {
        return -EINVAL;
    }
    if (closed_) [[unlikely]] {
        return -ESHUTDOWN;
    }

    if (snd_buf_.getFreeSize() < size) [[unlikely]] {
        SHLOG_WARN("send buffer overflow risk on fd {}: free {} < want {}", conn_sk_.fd(),
                   snd_buf_.getFreeSize(), size);
        return -ENOBUFS;
    }

    if (snd_buf_.writableSize() < size) [[unlikely]] {
        snd_buf_.shrink();
    }

    // write enabled. append data and wait for the next epoll write event,
    if (snd_buf_.readableSize() > 0) [[unlikely]] {
        snd_buf_.write(data, size);
        enableWrite();
        return size;
    }

    auto n = conn_sk_.send(data, size, MSG_NOSIGNAL);

    if (n < 0) [[unlikely]] {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            snd_buf_.write(data, size);
            enableWrite();
            return size;
        }
        const int err = errno;
        SHLOG_ERROR("send failed on fd {}: {}", conn_sk_.fd(), err);
        close();
        return -err;
    }

    if (n < size) [[unlikely]] {
        snd_buf_.write(data + n, size - n);
        enableWrite();
    }

    return size;
}

void TcpConn::disableWrite() {
    if (closed_) [[unlikely]] {
        return;
    }
    if (ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN | EPOLLRDHUP, &io_handler_) < 0)
        [[unlikely]] {
        SHLOG_ERROR("failed to disable EPOLLOUT for fd {}: {}", conn_sk_.fd(), errno);
    }
}

void TcpConn::enableWrite() {
    if (closed_) [[unlikely]] {
        return;
    }
    if (ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP, &io_handler_) <
        0) [[unlikely]] {
        SHLOG_ERROR("failed to enable EPOLLOUT for fd {}: {}", conn_sk_.fd(), errno);
    }
}

}  // namespace shnet