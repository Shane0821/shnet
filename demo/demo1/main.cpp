
#include <iostream>

#include "shnet/event_loop.h"
#include "shnet/tcp_conn.h"
#include "shnet/tcp_server.h"

using shnet::EventLoop;
using shnet::TcpConn;
using shnet::TcpServer;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return 1;
    }

    SHLOG_INIT(shlog::LogLevel::DEBUG);

    uint16_t port = atoi(argv[1]);

    EventLoop evloop;
    TcpServer server(&evloop);

    server.start(port, [](TcpConn* conn) {
        std::cout << "New connection established\n";

        conn->setReadCallback([conn]() {
            auto msg = conn->readAll();
            std::cout << "Received: " << msg.data_ << std::endl;
            conn->send("HTTP/1.1 200 OK\nContent-Length: 12\n\nHello World!\n", 50);
        });
        // conn->SetReadCallback([conn]() {
        //     std::cout << "Received: " << conn->GetDataUntilCrLf() << std::endl;
        //     conn->Send("Hello World!\r\n", 15);
        //     TimerInstance()->AddTimeout(1000, [conn]() {
        //         std::cout << "Timeout 1 second\n";
        //         conn->Send("Hello after 1 second!\r\n", 24);
        //     });
        // });
    });

    evloop.run();
    return 0;
}