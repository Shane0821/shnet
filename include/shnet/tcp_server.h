#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "event_loop.h"
#include "tcp_socket.h"

namespace shnet {

class TcpConn;

class TcpServer {
   public:
    using ConnMap = std::unordered_map<int, std::shared_ptr<TcpConn>>;
    using SubscriberSet = std::unordered_set<int>;
    using NewConnCallback = void (*)(std::shared_ptr<TcpConn>);

    static void acceptTrampoline(void* obj, uint32_t events);
    static void removeConnTrampoline(void* obj, int fd);

    TcpServer(EventLoop*);
    ~TcpServer();

    void start(uint16_t port, NewConnCallback cb);

    void subscribe(int fd);
    void unsubscribe(int fd);

    // Broadcast to all current subscribers.
    // Returns 0 on success, or last negative errno code if any send fails.
    int broadcast(const char* data, size_t size);

   private:
    void handleAccept(uint32_t);
    void removeConn(int fd);

    EventLoop* ev_loop_;
    NewConnCallback new_conn_cb_;
    EventLoop::EventHandler accept_handler_;
    TcpSocket listen_sk_;
    ConnMap conn_map_;
    SubscriberSet subscribers_;
};

}  // namespace shnet