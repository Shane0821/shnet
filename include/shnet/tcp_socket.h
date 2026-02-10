#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>  // readv
#include <unistd.h>

#include "shnet/utils/noncopyable.h"

namespace shnet {
class TcpSocket : noncopyable {
   public:
    explicit TcpSocket(int fd);
    ~TcpSocket();

    void setNoDelay();
    void setReusable();
    void setNonBlocking();
    void setBlocking();
    void setKeepAlive();
    void setRcvBufSize(int rcvBufSize);
    void setSndBufSize(int sndBufSize);

    bool getTcpInfo(struct tcp_info*) const { return true; }

    int fd() const;

    int bind(uint16_t port);
    int listen();
    void close();

    ssize_t read(void* buf, size_t len);
    ssize_t readv(const struct iovec* iov, int iovcnt);

    ssize_t write(const void* buf, size_t len);
    ssize_t send(const void* buf, size_t len, int flags);

   private:
    static constexpr int KEEP_ALIVE = 1;
    static constexpr int KEEP_IDLE = 60;
    static constexpr int KEEP_INTERVAL = 5;
    static constexpr int KEEP_COUNT = 3;
    // affects accept queue -> also affects syn queue
    static constexpr int LISTEN_BACKLOG = 128;

    const int sockfd_;
};
}