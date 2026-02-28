
#include <iostream>

#include "shnet/event_loop.h"
#include "shnet/tcp_conn.h"
#include "shnet/tcp_server.h"

using shnet::EventLoop;
using shnet::TcpConn;
using shnet::TcpServer;
using shnet::Timer;

shcoro::Async<void> coroRead(std::shared_ptr<TcpConn> conn) {
    auto msg = conn->readn(15);
    std::string cached_data(msg.data_);
    for (int i = 0; i < 10; i++) {
        SHLOG_INFO("fifo await");
        co_await shcoro::FIFOAwaiter{};
    }
    SHLOG_INFO("sleep 5");
    co_await shcoro::TimedAwaiter{&Timer::GetInst(), 5};
    SHLOG_INFO("wake up");
    SHLOG_INFO("received: {}", cached_data);
    conn->send("HTTP/1.1 200 OK\nContent-Length: 12\n\nHello World!\n", 50);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return 1;
    }

    SHLOG_INIT(shlog::LogLevel::DEBUG);

    uint16_t port = atoi(argv[1]);

    EventLoop evloop;
    TcpServer server(&evloop);

    server.start(port, [](std::shared_ptr<TcpConn> conn) {
        SHLOG_INFO("new connection restablished");

        conn->setCloseCallback([](int fd) {
            SHLOG_INFO("connection fd {} closed", fd);
        });

        conn->setReadCallback([](std::shared_ptr<TcpConn> conn) {
            if (conn->getReadableSize() < 15) return -1;
            shcoro::spawn_async_detached(coroRead(conn), conn->getEventLoop()->getScheduler());
            return 0;
        });
    });

    evloop.run();
    return 0;
}