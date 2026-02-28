#include "shnet/tcp_connector.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>

#include "shcoro/stackless/utility.hpp"
#include "shnet/event_loop.h"

namespace shnet {

inline void TcpConnector::ioTrampoline(void* obj, uint32_t events) {
    static_cast<TcpConnector*>(obj)->handleIO(events);
}

TcpConnector::TcpConnector(EventLoop* loop)
    : ev_loop_(loop),
      conn_sk_([] {
          int fd = ::socket(AF_INET, SOCK_STREAM, 0);
          if (fd == -1) [[unlikely]] {
              throw std::system_error(errno, std::system_category(),
                                      "fail to create tcp connector fd");
          }
          return fd;
      }()) {}

TcpConnector::~TcpConnector() {
    if (closed_) [[unlikely]] {
        return;
    }
    const int fd = conn_sk_.fd();
    SHLOG_INFO("TcpConnector close: {}", fd);
    closed_ = true;
    ev_loop_->delEvent(fd);
    if (close_cb_) [[likely]] {
        close_cb_(fd);
    }
    conn_sk_.close();
}

int TcpConnector::connect(const std::string& ip, uint16_t port) {
    if (closed_) [[unlikely]] {
        return -ESHUTDOWN;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        SHLOG_ERROR("inet_pton failed for ip {}: {}", ip, errno);
        return -EINVAL;
    }

    conn_sk_.setNonBlocking();
    conn_sk_.setKeepAlive();

    const int fd = conn_sk_.fd();

    int ret = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (ret == 0) {
        connected_ = true;
        io_handler_ = EventLoop::EventHandler{this, &ioTrampoline};
        if (ev_loop_->addEvent(fd, EPOLLIN, &io_handler_) < 0) [[unlikely]] {
            SHLOG_ERROR("failed to register connector fd {} to epoll: {}", fd, errno);
            close();
            return -errno;
        }
        SHLOG_INFO("TcpConnector connected immediately to {}:{}", ip, port);
        return 0;
    }

    if (ret < 0) {
        int err = errno;
        if (err != EINPROGRESS) {
            SHLOG_ERROR("connect failed immediately for fd {}: {}", fd, err);
            return -err;
        }

        // Non-blocking connect in progress; wait for EPOLLOUT to finish it.
        connect_in_progress_ = true;
        io_handler_ = EventLoop::EventHandler{this, &ioTrampoline};
        if (ev_loop_->addEvent(fd, EPOLLIN | EPOLLOUT, &io_handler_) < 0) [[unlikely]] {
            SHLOG_ERROR("failed to register connector fd {} to epoll: {}", fd, errno);
            close();
            return -errno;
        }
        SHLOG_INFO("TcpConnector connecting asynchronously to {}:{}", ip, port);
    }

    return 0;
}

Message TcpConnector::readAll() {
    auto ret = rcv_buf_.getAllData();
    rcv_buf_.readCommit(ret.size_);
    return ret;
}

Message TcpConnector::readUntil(char terminator) {
    auto ret = rcv_buf_.getDataUntil(terminator);
    if (ret.data_ != nullptr) {
        // Consume the delimiter as well while returning line content only.
        rcv_buf_.readCommit(ret.size_ + 1);
    }
    return ret;
}

Message TcpConnector::readUntilCRLF() {
    auto ret = rcv_buf_.getDataUntilCRLF();
    if (ret.data_ != nullptr) {
        // Consume the delimiter as well while returning line content only.
        rcv_buf_.readCommit(ret.size_ + 2);
    }
    return ret;
}

Message TcpConnector::readn(size_t n) {
    auto ret = rcv_buf_.getData(n);
    rcv_buf_.readCommit(ret.size_);
    return ret;
}

void TcpConnector::handleIO(uint32_t events) {
    if (closed_) [[unlikely]] {
        return;
    }

    if (events & (EPOLLERR | EPOLLHUP)) [[unlikely]] {
        SHLOG_ERROR("connector fd {} got error/hup events: {}", conn_sk_.fd(), events);
        close();
        return;
    }

    // Keep this connector alive through shared_from_this()
    auto self_ptr = shared_from_this();

    if (connect_in_progress_ && (events & EPOLLOUT)) {
        handleConnect();
        // After handleConnect(), the connection may be closed.
        if (closed_) {
            return;
        }
    }

    if (events & EPOLLIN) handleRead();
    if (events & EPOLLOUT && !connect_in_progress_) handleWrite();
}

void TcpConnector::handleConnect() {
    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(conn_sk_.fd(), SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        err = errno;
    }

    if (err != 0) {
        SHLOG_ERROR("async connect failed on fd {}: {}", conn_sk_.fd(), err);
        close();
        return;
    }

    connect_in_progress_ = false;
    connected_ = true;

    // Connection established; stop listening for EPOLLOUT until we have data to send.
    if (ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN, &io_handler_) < 0) [[unlikely]] {
        SHLOG_ERROR("failed to switch connector fd {} to EPOLLIN: {}", conn_sk_.fd(),
                    errno);
        close();
        return;
    }

    SHLOG_INFO("TcpConnector async connect succeeded on fd {}", conn_sk_.fd());
}

void TcpConnector::handleRead() {
    if (closed_) [[unlikely]] {
        SHLOG_WARN("handle read on closed connector fd {}", conn_sk_.fd());
        return;
    }

    size_t len = rcv_buf_.writableSize();
    if (len == 0) [[unlikely]] {
        rcv_buf_.shrink();
        len = rcv_buf_.writableSize();
        if (len == 0) [[unlikely]] {
            return;
        }
    }

    const ssize_t n = conn_sk_.read(rcv_buf_.writePointer(), len);
    if (n <= 0) [[unlikely]] {
        const int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return;
        }
        if (n < 0) {
            SHLOG_ERROR("connector handle read failed on fd {}: {}", conn_sk_.fd(), err);
        } else {
            SHLOG_INFO("peer reset connector on fd {}", conn_sk_.fd());
        }
        close();
        return;
    }

    rcv_buf_.writeCommit(static_cast<size_t>(n));

    if (read_cb_) [[likely]] {
        while (rcv_buf_.readableSize() > 0) {
            int ret = read_cb_(shared_from_this());
            if (ret < 0) [[unlikely]] {
                break;
            }
        }
    }
}

void TcpConnector::handleWrite() {
    if (closed_) [[unlikely]] {
        SHLOG_WARN("handle write on closed connector fd {}", conn_sk_.fd());
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
            SHLOG_ERROR("connector handle write failed on fd {}: {}", conn_sk_.fd(),
                        errno);
            close();
            return;
        }

        // send() returning 0 is unexpected here (len > 0); avoid a busy loop.
        SHLOG_WARN("connector send() returned 0 on fd {}: {}", conn_sk_.fd(), n);
        break;
    }

    if (snd_buf_.empty()) {
        disableWrite();
    }
}

int TcpConnector::sendBlocking(const char* data, size_t size) {
    if (size == 0) [[unlikely]] {
        return 0;
    }
    if (!data) [[unlikely]] {
        return -EINVAL;
    }
    if (closed_) [[unlikely]] {
        return -ESHUTDOWN;
    }

    // drain send buffer
    while (!snd_buf_.empty()) {
        auto n =
            conn_sk_.send(snd_buf_.readPointer(), snd_buf_.readableSize(), MSG_NOSIGNAL);
        if (n < 0) [[unlikely]] {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            SHLOG_ERROR("connector send blocking failed on fd {}: {}", conn_sk_.fd(),
                        errno);
            close();
            return -errno;
        }
        snd_buf_.readCommit(n);
    }

    disableWrite();

    // send remaining data
    auto ret = size;
    while (size > 0) {
        auto n = conn_sk_.send(data, size, MSG_NOSIGNAL);
        if (n < 0) [[unlikely]] {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            SHLOG_ERROR("connector send blocking failed on fd {}: {}", conn_sk_.fd(),
                        errno);
            close();
            return -errno;
        }
        data += static_cast<size_t>(n);
        size -= static_cast<size_t>(n);
    }
    return 0;
}

int TcpConnector::send(const char* data, size_t size) {
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
        SHLOG_WARN("connector send buffer overflow risk on fd {}: free {} < want {}",
                   conn_sk_.fd(), snd_buf_.getFreeSize(), size);
        return -ENOBUFS;
    }

    if (snd_buf_.writableSize() < size) [[unlikely]] {
        snd_buf_.shrink();
    }

    // write enabled. append data and wait for the next epoll write event,
    if (snd_buf_.readableSize() > 0) [[unlikely]] {
        snd_buf_.write(data, size);
        enableWrite();
        return 0;
    }

    auto n = conn_sk_.send(data, size, MSG_NOSIGNAL);

    if (n < 0) [[unlikely]] {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            snd_buf_.write(data, size);
            enableWrite();
            return 0;
        }
        const int err = errno;
        SHLOG_ERROR("connector send failed on fd {}: {}", conn_sk_.fd(), err);
        close();
        return -err;
    }

    if (n < size) [[unlikely]] {
        snd_buf_.write(data + n, size - n);
        enableWrite();
    }

    return 0;
}

shcoro::Async<int> TcpConnector::sendAsync(const char* data, size_t size) {
    if (size == 0) [[unlikely]] {
        co_return 0;
    }
    if (!data) [[unlikely]] {
        co_return -EINVAL;
    }
    if (closed_) [[unlikely]] {
        co_return -ESHUTDOWN;
    }

    while (snd_buf_.getFreeSize() < size) {
        co_await shcoro::FIFOAwaiter{};
    }

    if (snd_buf_.writableSize() < size) [[unlikely]] {
        snd_buf_.shrink();
    }

    // write enabled. append data and wait for the next epoll write event,
    if (snd_buf_.readableSize() > 0) [[unlikely]] {
        snd_buf_.write(data, size);
        enableWrite();
        co_return 0;
    }

    auto n = conn_sk_.send(data, size, MSG_NOSIGNAL);

    if (n < 0) [[unlikely]] {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            snd_buf_.write(data, size);
            enableWrite();
            co_return 0;
        }
        const int err = errno;
        SHLOG_ERROR("connector send failed on fd {}: {}", conn_sk_.fd(), err);
        close();
        co_return -err;
    }

    if (n < size) [[unlikely]] {
        snd_buf_.write(data + n, size - n);
        enableWrite();
    }

    co_return 0;
}

void TcpConnector::disableWrite() {
    if (closed_) [[unlikely]] {
        return;
    }
    if (ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN, &io_handler_) < 0) [[unlikely]] {
        SHLOG_ERROR("failed to disable EPOLLOUT for connector fd {}: {}", conn_sk_.fd(),
                    errno);
    }
}

void TcpConnector::enableWrite() {
    if (closed_) [[unlikely]] {
        return;
    }
    if (ev_loop_->modEvent(conn_sk_.fd(), EPOLLIN | EPOLLOUT, &io_handler_) < 0)
        [[unlikely]] {
        SHLOG_ERROR("failed to enable EPOLLOUT for connector fd {}: {}", conn_sk_.fd(),
                    errno);
    }
}

void TcpConnector::setReadCallback(ReadCallback cb) {
    read_cb_ = cb;
}

void TcpConnector::close() {
    if (closed_) [[unlikely]] {
        return;
    }

    const int fd = conn_sk_.fd();
    SHLOG_INFO("TcpConnector close: {}", fd);

    closed_ = true;

    // Ensure epoll no longer references our in-object handler pointer.
    ev_loop_->delEvent(fd);

    if (close_cb_) {
        close_cb_(fd);
    }

    conn_sk_.close();
}

}  // namespace shnet

