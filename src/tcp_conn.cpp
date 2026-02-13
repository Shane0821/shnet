#include "shnet/tcp_conn.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "shnet/event_loop.h"

namespace shnet {

inline void TcpConn::ioTrampoline(void* obj, uint32_t events) {
    static_cast<TcpConn*>(obj)->handleIO(events);
}

TcpConn::TcpConn(int fd, EventLoop* loop) : conn_sk_(fd), ev_loop_(loop), closed_(false) {
    conn_sk_.setNonBlocking();
    conn_sk_.setKeepAlive();
    io_handler_ = EventLoop::EventHandler{this, &ioTrampoline};
    ev_loop_->addEvent(fd, EPOLLIN | EPOLLRDHUP, &io_handler_);
}

TcpConn::~TcpConn() { close(); }

void TcpConn::close() {
    if (closed_) {
        return;
    }
    SHLOG_INFO("Tcpconn close: {}", conn_sk_.fd());
    closed_ = true;
    ev_loop_->delEvent(conn_sk_.fd());
    conn_sk_.close();
}

void TcpConn::close_with_callback() {
    const int fd = conn_sk_.fd();
    close();
    if (close_cb_) [[likely]] {
        close_cb_(fd);
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

    if (events & EPOLLIN) handleRead();
    if (events & EPOLLOUT) handleWrite();
    if (events & (EPOLLERR | EPOLLHUP)) {
        shutdown_write_on_error();
    }
    if (events & EPOLLRDHUP) {
        close_with_callback();
    }
}

void TcpConn::handleRead() {
    auto n = recv();
    if (n > 0) [[likely]] {
        if (read_cb_) [[likely]] {
            read_cb_();
        }
        return;
    }

    if (n == 0) {
        return;
    }

    if (n < 0) {
        SHLOG_ERROR("handled read failed: {}", n);
        shutdown_write_on_error();
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

    if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        shutdown_write_on_error();
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
    if (ret == 0) [[unlikely]] {
        SHLOG_INFO("connection reset by peer");
        return -ECONNRESET;
    }

    rcv_buf_.writeCommit(ret);
    return rcv_buf_.readableSize();
}

ssize_t TcpConn::send(const char* data, size_t size) {
    if (!data || size == 0) [[unlikely]] {
        return 0;
    }
    if (closed_ || write_shutdown_) [[unlikely]] {
        return -ESHUTDOWN;
    }

    if (snd_buf_.getFreeSize() < size) {
        return -1;
    }

    if (snd_buf_.writableSize() < size) {
        snd_buf_.shrink();
    }

    // write enabled. append data and wait for the next epoll write event,
    if (snd_buf_.readableSize() > 0) {
        snd_buf_.write(data, size);
        return size;
    }

    auto n = conn_sk_.send(data, size, MSG_NOSIGNAL);

    if (n < 0) [[unlikely]] {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            snd_buf_.write(data, size);
            enableWrite();
        } else {
            shutdown_write_on_error();
        }
        return n;
    }

    if (n < size) {
        snd_buf_.write(data + n, size - n);
        enableWrite();
    }

    return n;
}

void TcpConn::disableWrite() {
    ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN | EPOLLRDHUP, &io_handler_);
}

void TcpConn::enableWrite() {
    ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP, &io_handler_);
}

void TcpConn::shutdown_write_on_error() {
    if (closed_ || write_shutdown_) [[unlikely]] {
        return;
    }
    write_shutdown_ = true;
    conn_sk_.shutdownWrite();
    disableWrite();
}

}  // namespace shnet