#pragma once

#include <functional>
#include <memory>
#include <unordered_map>

#include "event_loop.h"
#include "tcp_socket.h"

class TcpConn;

class TcpServer {
   public:
    using ConnPtr = std::unique_ptr<TcpConn>;
    using ConnMap = std::unordered_map<int, ConnPtr>;

    using NewConnCallback = std::function<void(TcpConn*)>;

    TcpServer(EventLoop*);
    ~TcpServer();

    void start(u_int16_t port, NewConnCallback cb);

   private:
    void handleAccept(uint32_t);

    EventLoop* ev_loop_;
    NewConnCallback new_conn_cb_;
    EventLoop::EventHandler accept_handler_;
    TcpSocket listen_sk_;
    ConnMap conn_map_;
};