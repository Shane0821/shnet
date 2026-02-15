#include "shnet/tcp_socket.h"

#include <cerrno>
#include <stdexcept>

#include "shlog/logger.h"

namespace shnet {
// NOTE: check fd != -1 before setting
TcpSocket::TcpSocket(int fd) : sockfd_(fd) {}

TcpSocket::~TcpSocket() { close(); }

void TcpSocket::setNoDelay() {
    int no_delay = 1;
    if (::setsockopt(sockfd_, SOL_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) < 0) {
        SHLOG_ERROR("setsockopt TCP_NODELAY failed for fd {}: {}", sockfd_, errno);
    }
}

void TcpSocket::setReusable() {
    int reuse_addr = 1;
    if (::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
        SHLOG_ERROR("setsockopt SO_REUSEADDR failed for fd {}: {}", sockfd_, errno);
    }
    int reuse_port = 1;
    if (::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &reuse_port, sizeof(reuse_port)) < 0) {
        SHLOG_ERROR("setsockopt SO_REUSEPORT failed for fd {}: {}", sockfd_, errno);
    }
}

void TcpSocket::setNonBlocking() {
    int flag = ::fcntl(sockfd_, F_GETFL);
    if (flag < 0) {
        SHLOG_ERROR("fcntl F_GETFL failed for fd {}: {}", sockfd_, errno);
        return;
    }
    if (::fcntl(sockfd_, F_SETFL, flag | O_NONBLOCK) < 0) {
        SHLOG_ERROR("fcntl F_SETFL O_NONBLOCK failed for fd {}: {}", sockfd_, errno);
    }
}

void TcpSocket::setBlocking() {
    int flag = ::fcntl(sockfd_, F_GETFL);
    if (flag < 0) {
        SHLOG_ERROR("fcntl F_GETFL failed for fd {}: {}", sockfd_, errno);
        return;
    }
    flag &= ~O_NONBLOCK;
    if (::fcntl(sockfd_, F_SETFL, flag) < 0) {
        SHLOG_ERROR("fcntl F_SETFL blocking failed for fd {}: {}", sockfd_, errno);
    }
}

void TcpSocket::setKeepAlive() {
    int keep_alive = KEEP_ALIVE;
    int keep_idle = KEEP_IDLE;
    int keep_intvl = KEEP_INTERVAL;
    int keep_cnt = KEEP_COUNT;

    if (::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) < 0) {
        SHLOG_ERROR("setsockopt SO_KEEPALIVE failed for fd {}: {}", sockfd_, errno);
    }
    if (::setsockopt(sockfd_, SOL_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle)) < 0) {
        SHLOG_ERROR("setsockopt TCP_KEEPIDLE failed for fd {}: {}", sockfd_, errno);
    }
    if (::setsockopt(sockfd_, SOL_TCP, TCP_KEEPINTVL, &keep_intvl, sizeof(keep_intvl)) < 0) {
        SHLOG_ERROR("setsockopt TCP_KEEPINTVL failed for fd {}: {}", sockfd_, errno);
    }
    if (::setsockopt(sockfd_, SOL_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt)) < 0) {
        SHLOG_ERROR("setsockopt TCP_KEEPCNT failed for fd {}: {}", sockfd_, errno);
    }
}

void TcpSocket::setRcvBufSize(int rcvBufSize) {
    if (::setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvBufSize, sizeof(rcvBufSize)) < 0) {
        SHLOG_ERROR("setsockopt SO_RCVBUF failed for fd {}: {}", sockfd_, errno);
    }
}

void TcpSocket::setSndBufSize(int sndBufSize) {
    if (::setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &sndBufSize, sizeof(sndBufSize)) < 0) {
        SHLOG_ERROR("setsockopt SO_SNDBUF failed for fd {}: {}", sockfd_, errno);
    }
}

int TcpSocket::bind(uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    return ::bind(sockfd_, (sockaddr*)&addr, sizeof(addr));
}

int TcpSocket::listen() { return ::listen(sockfd_, LISTEN_BACKLOG); }

ssize_t TcpSocket::read(void* buf, size_t len) { return ::read(sockfd_, buf, len); }

ssize_t TcpSocket::readv(const struct iovec* iov, int iovcnt) {
    return ::readv(sockfd_, iov, iovcnt);
}

ssize_t TcpSocket::write(const void* buf, size_t len) {
    return ::write(sockfd_, buf, len);
}

ssize_t TcpSocket::send(const void* buf, size_t len, int flags) {
    return ::send(sockfd_, buf, len, flags);
}

void TcpSocket::shutdown() {
    if (sockfd_ != -1) {
        if (::shutdown(sockfd_, SHUT_RDWR) < 0 && errno != ENOTCONN) {
            SHLOG_ERROR("shutdown failed for fd {}: {}", sockfd_, errno);
        }
    }
}

void TcpSocket::close() {
    if (sockfd_ != -1) {
        shutdown();
        if (::close(sockfd_) < 0) {
            SHLOG_ERROR("close failed for fd {}: {}", sockfd_, errno);
        }
        sockfd_ = -1;
    }
}

}  // namespace shnet