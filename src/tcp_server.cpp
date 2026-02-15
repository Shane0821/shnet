#include "shnet/tcp_server.h"

#include <cerrno>
#include <iostream>

#include "shnet/event_loop.h"
#include "shnet/tcp_conn.h"

namespace shnet {

inline void TcpServer::acceptTrampoline(void* obj, uint32_t events) {
    static_cast<TcpServer*>(obj)->handleAccept(events);
}

TcpServer::TcpServer(EventLoop* loop)
    : ev_loop_(loop), listen_sk_([] {
          int fd = socket(AF_INET, SOCK_STREAM, 0);
          if (fd == -1) [[unlikely]] {
              throw std::system_error(errno, std::system_category(),
                                      "fail to create server listen fd");
          }
          return fd;
      }()) {}

TcpServer::~TcpServer() {}

void TcpServer::handleAccept(uint32_t events) {
    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) [[unlikely]] {
        SHLOG_ERROR("listen socket error events: {}", events);
        return;
    }

    if (events & EPOLLIN) [[likely]] {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int conn_fd =
            ::accept4(listen_sk_.fd(), (sockaddr*)&client_addr, &len, SOCK_NONBLOCK);
        if (conn_fd == -1) [[unlikely]] {
            SHLOG_ERROR("accept4 failed on listen fd {}: {}", listen_sk_.fd(), errno);
            return;
        }

        auto conn = std::make_unique<TcpConn>(conn_fd, ev_loop_);
        conn->setUnregisterCallback([this](int fd) { conn_map_.erase(fd); });
        if (new_conn_cb_) [[likely]] {
            new_conn_cb_(conn.get());
        }
        conn_map_.emplace(conn_fd, std::move(conn));
    }
}

void TcpServer::start(uint16_t port, NewConnCallback cb) {
    listen_sk_.setNonBlocking();
    listen_sk_.setReusable();

    int ret = listen_sk_.bind(port);
    if (ret < 0) [[unlikely]] {
        throw std::system_error(errno, std::system_category(), "bind failed");
    }

    ret = listen_sk_.listen();
    if (ret < 0) [[unlikely]] {
        throw std::system_error(errno, std::system_category(), "listen failed");
    }

    new_conn_cb_ = cb;
    accept_handler_ = EventLoop::EventHandler{this, &acceptTrampoline};
    if (ev_loop_->addEvent(listen_sk_.fd(), EPOLLIN, &accept_handler_) < 0) [[unlikely]] {
        throw std::system_error(errno, std::system_category(),
                                "failed to register listen socket to epoll");
    }
    SHLOG_INFO("TcpServer started on port: {}", port);
}

}  // namespace shnet