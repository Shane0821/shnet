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
        shutdown_on_error();
    }
}

TcpConn::~TcpConn() { close(); }

void TcpConn::close() {
    if (closed_) {
        return;
    }
    SHLOG_INFO("Tcpconn close: {}", conn_sk_.fd());
    closed_ = true;
    ev_loop_->delEvent(conn_sk_.fd());
    if (close_cb_) [[likely]] {
        close_cb_(conn_sk_.fd());
    }
    conn_sk_.close();
}

void TcpConn::unregister() {
    if (unregister_cb_) [[likely]] {
        SHLOG_INFO("unregistering connection fd {} from epoll", conn_sk_.fd());
        unregister_cb_(conn_sk_.fd());
    }
}

Message TcpConn::readAll() {
    auto ret = rcv_buf_.getAllData();
    rcv_buf_.readCommit(ret.size_);
    return ret;
}

Message TcpConn::readLine() {
    auto ret = rcv_buf_.getDataUntil('\n');
    if (ret.data_ != nullptr) {
        // Consume the delimiter as well while returning line content only.
        rcv_buf_.readCommit(ret.size_ + 1);
    }
    return ret;
}

void TcpConn::handleIO(uint32_t events) {
    if (closed_) [[unlikely]] {
        return;
    }

    if (events & (EPOLLERR | EPOLLHUP)) [[unlikely]] {
        SHLOG_ERROR("connection fd {} got error/hup events: {}", conn_sk_.fd(), events);
        shutdown_on_error();
        return;
    }

    if (events & EPOLLRDHUP) [[unlikely]] {
        SHLOG_INFO("connection fd {} got rdhup events: {}", conn_sk_.fd(), events);
        // TODO handle epoll rd hup
        unregister();
        return;
    }

    if (events & EPOLLIN) handleRead();
    if (events & EPOLLOUT) handleWrite();
}

void TcpConn::handleRead() {
    auto n = recv();
    if (n > 0) [[likely]] {
        if (read_cb_) [[likely]] {
            read_cb_();
        }
        return;
    }

    if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) [[unlikely]] {
        SHLOG_ERROR("handle read failed on fd {}: {}", conn_sk_.fd(), errno);
        shutdown_on_error();
    }
}

void TcpConn::handleWrite() {
    auto n = conn_sk_.send(snd_buf_.readPointer(), snd_buf_.readableSize(), MSG_NOSIGNAL);

    if (n > 0) [[likely]] {
        snd_buf_.readCommit(n);
        if (snd_buf_.empty()) {
            disableWrite();
        }
        return;
    }

    if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) [[unlikely]] {
        SHLOG_ERROR("handle write failed on fd {}: {}", conn_sk_.fd(), errno);
        shutdown_on_error();
    }
}

// returns size of data waiting for processing or errno
ssize_t TcpConn::recv() {
    if (rcv_buf_.full()) [[unlikely]] {
        return rcv_buf_.readableSize();
    }

    size_t len = rcv_buf_.writableSize();
    if (len == 0) [[unlikely]] {
        rcv_buf_.shrink();
        len = rcv_buf_.writableSize();
    }

    ssize_t ret = conn_sk_.read(rcv_buf_.writePointer(), len);
    if (ret < 0) [[unlikely]] {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return rcv_buf_.readableSize();
        }
        SHLOG_ERROR("error read: {}", errno);
        return -errno;
    }

    rcv_buf_.writeCommit(ret);
    return rcv_buf_.readableSize();
}

ssize_t TcpConn::send(const char* data, size_t size) {
    if (!data || size == 0) [[unlikely]] {
        return 0;
    }
    if (closed_ || shutdown_) [[unlikely]] {
        return -ESHUTDOWN;
    }

    if (snd_buf_.getFreeSize() < size) [[unlikely]] {
        SHLOG_WARN("send buffer overflow risk on fd {}: free {} < want {}", conn_sk_.fd(),
                   snd_buf_.getFreeSize(), size);
        return -1;
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
        SHLOG_ERROR("send failed on fd {}: {}", conn_sk_.fd(), n);
        shutdown_on_error();
        return n;
    }

    if (n < size) [[unlikely]] {
        snd_buf_.write(data + n, size - n);
        enableWrite();
    }

    return size;
}

void TcpConn::disableWrite() {
    if (ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN | EPOLLRDHUP, &io_handler_) < 0)
        [[unlikely]] {
        SHLOG_ERROR("failed to disable EPOLLOUT for fd {}: {}", conn_sk_.fd(), errno);
    }
}

void TcpConn::enableWrite() {
    if (ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP, &io_handler_) <
        0) [[unlikely]] {
        SHLOG_ERROR("failed to enable EPOLLOUT for fd {}: {}", conn_sk_.fd(), errno);
    }
}

void TcpConn::shutdown_on_error() {
    if (closed_ || shutdown_) [[unlikely]] {
        return;
    }
    shutdown_ = true;
    conn_sk_.shutdown();
    disableWrite();
}

}  // namespace shnet