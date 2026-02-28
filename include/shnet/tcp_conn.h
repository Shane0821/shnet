#pragma once

#include <functional>
#include <memory>
#include <string>

#include "event_loop.h"
#include "shcoro/stackless/async.hpp"
#include "shnet/utils/message_buff.h"
#include "tcp_socket.h"

namespace shnet {

class TcpServer;

class TcpConn : public std::enable_shared_from_this<TcpConn> {
    friend class TcpServer;

   public:
    using ReadCallback = int (*)(std::shared_ptr<TcpConn>);
    using CloseCallback = void (*)(int);

    TcpConn(int fd, EventLoop* evLoop);
    ~TcpConn();

    Message readAll();
    Message readUntil(char terminator);
    Message readn(size_t n);
    size_t getReadableSize() { return rcv_buf_.readableSize(); }
    void setReadCallback(ReadCallback cb);

    // Buffered, non-blocking send.
    //
    // Contract:
    // - On success, returns 0.
    // - On failure, returns a negative errno value (e.g. -ESHUTDOWN, -ENOBUFS, -EPIPE,
    // ...).
    //
    // Note: returning 0 does NOT guarantee the peer has received the data; it only
    // means this connection has taken ownership for delivery.
    int send(const char* data, size_t size);
    int sendBlocking(const char* data, size_t size);
    bool sendAsyncShouldYield(size_t size) { return snd_buf_.getFreeSize() < size; }
    shcoro::Async<int> sendAsync(const char* data, size_t size);

    // Subscription helpers
    void subscribe();
    void unsubscribe();
    // Broadcast helpers – calls owner server’s broadcast().
    int broadcast(const char* data, size_t size);

    void setCloseCallback(CloseCallback cb) { close_cb_ = cb; }

    EventLoop* getEventLoop() const { return ev_loop_; }

   private:
    struct RemoveConnHandler {
        using Callback = void (*)(void* obj, int fd);

        inline void operator()(int fd) const noexcept { cb(obj, fd); }

        void* obj;
        Callback cb;
    };

    static void ioTrampoline(void*, uint32_t);

    void setRemoveConnHandler(RemoveConnHandler handler) {
        remove_conn_handler_ = handler;
    }

    void handleIO(uint32_t);

    void handleRead();
    void handleWrite();

    void close();
    void removeFromServer();

    void enableWrite();
    void disableWrite();

    static constexpr size_t SOCK_RCV_LEN = MessageBuffer::DEFAULT_SIZE * 2;
    static constexpr size_t SOCK_SEND_LEN = MessageBuffer::DEFAULT_SIZE * 2;

    EventLoop* ev_loop_;
    EventLoop::EventHandler io_handler_;
    MessageBuffer rcv_buf_;
    MessageBuffer snd_buf_;
    ReadCallback read_cb_;
    CloseCallback close_cb_;
    RemoveConnHandler remove_conn_handler_;
    TcpSocket conn_sk_;
    bool closed_{false};
    bool peer_shutdown_{false};  // peer has shutdown its write side (FIN/RDHUP/read==0)
    bool removed_{false};        // remove callback invoked
    TcpServer* owner_server_{nullptr};
};

}  // namespace shnet