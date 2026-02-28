
#include <iostream>

#include "shnet/event_loop.h"
#include "shnet/tcp_conn.h"
#include "shnet/tcp_server.h"

using shnet::EventLoop;
using shnet::TcpConn;
using shnet::TcpServer;
using shnet::Timer;

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
            // For example, read until newline for a single text command
            auto msg = conn->readUntil('\n');
            if (!msg.data_ || msg.size_ == 0) {
                return -1;  // not enough data yet
            }
        
            std::string_view cmd(msg.data_, msg.size_ - 1);
            SHLOG_INFO("cmd: {}, size: {}", cmd, msg.size_);
        
            if (cmd == "SUB") {
                conn->subscribe();
                return 0;
            }
        
            if (cmd == "UNSUB") {
                conn->unsubscribe();
                return 0;
            }
        
            constexpr std::string_view pub_prefix = "PUB ";
            if (cmd.substr(0, pub_prefix.size()) == pub_prefix) {
                std::string_view payload = cmd.substr(pub_prefix.size());
                conn->broadcast(payload.data(), payload.size());
                return 0;
            }
        
            // Unknown command – ignore or handle as you like.
            return 0;
        });
    });

    evloop.run();
    return 0;
}