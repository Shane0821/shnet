#pragma once

#include <functional>
#include <string>

#include "event_loop.h"
#include "shnet/utils/message_buff.h"
#include "tcp_socket.h"
namespace shnet {

class TcpConn {
   public:
    using ReadCallback = std::function<void()>;
    using CloseCallback = std::function<void(int)>;
    using UnregisterCallback = std::function<void(int)>;
    using IOHandler = std::function<void()>;

    static void ioTrampoline(void*, uint32_t);
    TcpConn(int fd, EventLoop* evLoop);
    ~TcpConn();

    Message readAll();
    Message readLine();
    ssize_t send(const char* data, size_t size);

    void setReadCallback(auto&& cb) { read_cb_ = cb; }
    void setCloseCallback(auto&& cb) { close_cb_ = cb; }
    void setUnregisterCallback(auto&& cb) { unregister_cb_ = cb; }

   private:
    void handleIO(uint32_t);

    void handleRead();
    void handleWrite();

    ssize_t recv();
    void shutdown_on_error();
    void close();
    void unregister();

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
    UnregisterCallback unregister_cb_;
    TcpSocket conn_sk_;
    bool closed_{false};
    bool shutdown_{false};
};

}  // namespace shnet