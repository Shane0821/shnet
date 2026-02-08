#include "shnet/tcp_server.h"

#include <iostream>

#include "shnet/event_loop.h"
#include "shnet/tcp_conn.h"

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
    if (events & EPOLLIN) {
        printf("epollin\n");
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int conn_fd =
            ::accept4(listen_sk_.fd(), (sockaddr*)&client_addr, &len, SOCK_NONBLOCK);
        if (conn_fd == -1) [[unlikely]] {
            printf("server failed to create connection\n");
            return;
        }

        printf("build conn\n");
        auto conn = std::make_unique<TcpConn>(conn_fd, ev_loop_);
        conn->setCloseCallback([this](int fd) { conn_map_.erase(fd); });
        printf("new conn cb\n");
        if (new_conn_cb_) [[likely]] {
            new_conn_cb_(conn.get());
        }
        conn_map_.emplace(conn_fd, std::move(conn));
    }
}

void TcpServer::start(uint16_t port, NewConnCallback cb) {
    printf("TcpServer start called\n");
    int ret = listen_sk_.bind(port);
    if (ret < 0) [[unlikely]] {
        throw std::system_error(errno, std::system_category(), "bind failed");
    }

    ret = listen_sk_.listen();
    if (ret < 0) [[unlikely]] {
        throw std::system_error(errno, std::system_category(), "listen failed");
    }

    listen_sk_.setNonBlocking();
    listen_sk_.setReusable();

    new_conn_cb_ = cb;
    accept_handler_ = [this](uint32_t events) { handleAccept(events); };
    ev_loop_->addEvent(listen_sk_.fd(), EPOLLIN, &accept_handler_);
    printf("TcpServer started on port: %d\n", port);
}