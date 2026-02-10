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
    io_handler_ = EventLoop::EventHandlerNew{this, &ioTrampoline};
    ev_loop_->addEvent(fd, EPOLLIN | EPOLLRDHUP, &io_handler_);
}

TcpConn::~TcpConn() { close(); }

void TcpConn::close() {
    if (closed_) {
        return;
    }
    printf("close conn\n");
    closed_ = true;
    ev_loop_->delEvent(conn_sk_.fd());
    conn_sk_.close();
}

void TcpConn::close_with_callback() {
    close();
    if (close_cb_) [[likely]] {
        close_cb_(conn_sk_.fd());
    }
}

Message TcpConn::readAll() { return rcv_buf_.getAllData(); }

Message TcpConn::readLine() { return rcv_buf_.getDataUntil('\n'); }

void TcpConn::handleIO(uint32_t events) {
    if (closed_) [[unlikely]] {
        return;
    }

    if (events & EPOLLIN) handleRead();
    if (events & EPOLLOUT) handleWrite();
    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        close_with_callback();
    }
}

void TcpConn::handleRead() {
    printf("handle read\n");
    int err = 0;
    auto n = recv();
    if (n > 0) [[likely]] {
        if (read_cb_) [[likely]] {
            read_cb_();
        }
        rcv_buf_.readCommit(n);
        return;
    }

    if (n == 0 || (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK))) {
        printf("n: %ld\n", n);
        close_with_callback();
    }
}

void TcpConn::handleWrite() {
    printf("handle write\n");
    auto n = conn_sk_.send(snd_buf_.readPointer(), snd_buf_.readableSize(), MSG_NOSIGNAL);

    if (n > 0) [[likely]] {
        snd_buf_.readCommit(n);
        if (snd_buf_.empty()) {
            disableWrite();
        }
        return;
    }

    if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        close();
    }
}

// returns size of data waiting for processing or errno
ssize_t TcpConn::recv() {
    if (rcv_buf_.full()) [[unlikely]] {
        printf("rcv buffer full\n");
        return rcv_buf_.readableSize();
    }

    size_t len = rcv_buf_.writableSize();
    if (len == 0) [[unlikely]] {
        rcv_buf_.shrink();
        len = rcv_buf_.writableSize();
    }

    ssize_t ret = conn_sk_.read(rcv_buf_.writePointer(), len);
    if (ret < 0) [[unlikely]] {
        printf("err read\n");
        return ret;
    }
    if (ret == 0) [[unlikely]] {
        printf("conn reset by peer\n");
        return -ECONNRESET;
    }

    rcv_buf_.writeCommit(ret);
    return rcv_buf_.readableSize();
}

ssize_t TcpConn::send(const char* data, size_t size) {
    if (!data || size == 0) [[unlikely]] {
        return 0;
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
            close_with_callback();
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

}