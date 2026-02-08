#include "shnet/tcp_socket.h"

#include <stdexcept>

// NOTE: check fd != -1 before setting
TcpSocket::TcpSocket(int fd) : sockfd_(fd) {}

TcpSocket::~TcpSocket() { close(); }

void TcpSocket::setNoDelay() {
    int no_delay = 1;
    setsockopt(sockfd_, SOL_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
}

void TcpSocket::setReusable() {
    int reuse_addr = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));
    int reuse_port = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &reuse_port, sizeof(reuse_port));
}

void TcpSocket::setNonBlocking() {
    int flag = fcntl(sockfd_, F_GETFL);
    fcntl(sockfd_, F_SETFL, flag | O_NONBLOCK);
}

void TcpSocket::setBlocking() {
    int flag = fcntl(sockfd_, F_GETFL);
    flag &= ~O_NONBLOCK;
    fcntl(sockfd_, F_SETFL, flag);
}

void TcpSocket::setKeepAlive() {
    int keep_alive = KEEP_ALIVE;
    int keep_idle = KEEP_IDLE;
    int keep_intvl = KEEP_INTERVAL;
    int keep_cnt = KEEP_COUNT;

    setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive));
    setsockopt(sockfd_, SOL_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
    setsockopt(sockfd_, SOL_TCP, TCP_KEEPINTVL, &keep_intvl, sizeof(keep_intvl));
    setsockopt(sockfd_, SOL_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt));
}

void TcpSocket::setRcvBufSize(int rcvBufSize) {
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvBufSize, sizeof(rcvBufSize));
}

void TcpSocket::setSndBufSize(int sndBufSize) {
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &sndBufSize, sizeof(sndBufSize));
}

int TcpSocket::fd() const { return sockfd_; }

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

void TcpSocket::close() { ::close(sockfd_); }