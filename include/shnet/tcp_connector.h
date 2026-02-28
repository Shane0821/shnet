#pragma once

#include <functional>
#include <memory>
#include <string>

#include "event_loop.h"
#include "shcoro/stackless/async.hpp"
#include "shnet/utils/message_buff.h"
#include "tcp_socket.h"

namespace shnet {

// Active TCP connector/client that dials a remote TCP server and then behaves
// similarly to TcpConn: non-blocking, buffered I/O driven by EventLoop with a
// read callback and async send support.
class TcpConnector : public std::enable_shared_from_this<TcpConnector> {
   public:
    using ReadCallback = int (*)(std::shared_ptr<TcpConnector>);
    using CloseCallback = void (*)(int);

    explicit TcpConnector(EventLoop* evLoop);
    ~TcpConnector();

    // Initiate a connection to the given IPv4 address and port.
    //
    // This uses a non-blocking connect:
    // - On immediate success, the socket is registered to the EventLoop and
    //   ready for read/write.
    // - On EINPROGRESS, the connection result is reported later via EPOLLOUT;
    //   if it fails, the close callback is invoked and the connector is closed.
    //
    // Contract:
    // - Returns 0 on success (including EINPROGRESS).
    // - Returns negative errno on immediate failure (e.g. -ECONNREFUSED).
    int connect(const std::string& ip, uint16_t port);

    // Read helpers (consume data from internal receive buffer).
    Message readAll();
    Message readUntil(char terminator);
    Message readUntilCRLF();
    Message readn(size_t n);
    size_t getReadableSize() { return rcv_buf_.readableSize(); }

    void setReadCallback(ReadCallback cb);

    // Buffered, non-blocking send.
    //
    // Contract:
    // - On success, returns 0.
    // - On failure, returns a negative errno value (e.g. -ESHUTDOWN, -ENOBUFS, -EPIPE,
    //   ...).
    //
    // Note: returning 0 does NOT guarantee the peer has received the data; it only
    // means this connection has taken ownership for delivery.
    int send(const char* data, size_t size);
    int sendBlocking(const char* data, size_t size);
    bool sendAsyncShouldYield(size_t size) { return snd_buf_.getFreeSize() < size; }
    shcoro::Async<int> sendAsync(const char* data, size_t size);

    void setCloseCallback(CloseCallback cb) { close_cb_ = cb; }

    EventLoop* getEventLoop() const { return ev_loop_; }
    bool isConnected() const { return connected_; }

   private:
    static void ioTrampoline(void*, uint32_t);

    void handleIO(uint32_t);
    void handleConnect();
    void handleRead();
    void handleWrite();

    void close();

    void enableWrite();
    void disableWrite();

    static constexpr size_t SOCK_RCV_LEN = MessageBuffer::DEFAULT_SIZE * 2;
    static constexpr size_t SOCK_SEND_LEN = MessageBuffer::DEFAULT_SIZE * 2;

    EventLoop* ev_loop_;
    EventLoop::EventHandler io_handler_;
    MessageBuffer rcv_buf_{SOCK_RCV_LEN};
    MessageBuffer snd_buf_{SOCK_SEND_LEN};
    ReadCallback read_cb_{nullptr};
    CloseCallback close_cb_{nullptr};
    TcpSocket conn_sk_;
    bool closed_{false};
    bool connect_in_progress_{false};
    bool connected_{false};
};

}  // namespace shnet

