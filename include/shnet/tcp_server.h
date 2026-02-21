#pragma once

#include <functional>
#include <memory>
#include <unordered_map>

#include "event_loop.h"
#include "tcp_socket.h"

namespace shnet {

class TcpConn;

class TcpServer {
   public:
    using ConnMap = std::unordered_map<int, std::shared_ptr<TcpConn>>;

    using NewConnCallback = void(*)(std::shared_ptr<TcpConn>);

    static void acceptTrampoline(void* obj, uint32_t events);

    TcpServer(EventLoop*);
    ~TcpServer();

    void start(uint16_t port, NewConnCallback cb);

   private:
    void handleAccept(uint32_t);

    EventLoop* ev_loop_;
    NewConnCallback new_conn_cb_;
    EventLoop::EventHandler accept_handler_;
    TcpSocket listen_sk_;
    ConnMap conn_map_;
};

}