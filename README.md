# shnet

A C++20 async TCP networking library built on epoll, featuring non-blocking I/O, buffered read/write, and C++20 coroutine support.

## Features

- **EventLoop** — Single-threaded event loop backed by Linux epoll
- **TcpServer** — Accept incoming connections with a callback-driven API
- **TcpConn** — Server-side connection with buffered I/O, read callbacks, and async send
- **TcpConnector** — Client-side connector for dialing remote TCP servers
- **Coroutine support** — Integrates with [shcoro](https://github.com/Shane0821/shcoro) for `co_await`-style async I/O
- **Pub/sub helpers** — Subscribe/unsubscribe and broadcast to selected connections

## Requirements

- C++20 with coroutine support (GCC/Clang: `-fcoroutines`, MSVC: `/await`)
- Linux (epoll)
- CMake 3.2+

## Dependencies

shnet uses [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) to pull:

- [shlog](https://github.com/Shane0821/shlog) — Logging
- [shcoro](https://github.com/Shane0821/shcoro) — Stackless coroutines and scheduler

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `SHNET_BUILD_DEMO` | OFF | Build demo programs |
| `SHNET_BUILD_TEST` | OFF | Build tests |

Example with demos:

```bash
cmake -DSHNET_BUILD_DEMO=ON ..
cmake --build .
```

## Usage

### TCP Server

```cpp
#include "shnet/event_loop.h"
#include "shnet/tcp_conn.h"
#include "shnet/tcp_server.h"

EventLoop evloop;
TcpServer server(&evloop);

server.start(port, [](std::shared_ptr<TcpConn> conn) {
    conn->setReadCallback([](std::shared_ptr<TcpConn> conn) {
        auto msg = conn->readUntilCRLF();
        if (!msg.data_ || msg.size_ == 0) return -1;
        // Handle message...
        return 0;
    });
});

evloop.run();
```

### Read helpers

- `readAll()` — All data in the receive buffer
- `readUntil(char)` — Up to a terminator
- `readUntilCRLF()` — Up to `\r\n`
- `readn(size_t)` — Exactly n bytes

### Send

- `send(data, size)` — Non-blocking, buffered; returns 0 on success or negative errno
- `sendBlocking(data, size)` — Blocks until sent
- `sendAsync(data, size)` — Coroutine-based async send (returns `shcoro::Async<int>`)

### Pub/sub (TcpServer)

```cpp
// Client sends "SUB\r\n" to subscribe
conn->subscribe();

// Client sends "UNSUB\r\n" to unsubscribe
conn->unsubscribe();

// Broadcast to all subscribers
conn->broadcast(data, size);
```

### TCP Connector (client)

```cpp
#include "shnet/tcp_connector.h"

TcpConnector connector(&evloop);
int ret = connector->connect("127.0.0.1", 8080);
if (ret == 0) {
    connector->setReadCallback(/* ... */);
    connector->send(data, size);
}
```

### Coroutines

```cpp
shcoro::Async<void> handleConn(std::shared_ptr<TcpConn> conn) {
    auto msg = conn->readn(15);
    co_await shcoro::TimedAwaiter{&Timer::GetInst(), 5};
    conn->send("response", 8);
}
```

## Project structure

```
shnet/
├── include/shnet/
│   ├── event_loop.h
│   ├── tcp_server.h
│   ├── tcp_conn.h
│   ├── tcp_connector.h
│   ├── tcp_socket.h
│   ├── inet_address.h
│   └── utils/
│       ├── message_buff.h
│       ├── timer.h
│       └── noncopyable.h
├── src/
│   ├── event_loop.cpp
│   ├── tcp_server.cpp
│   ├── tcp_conn.cpp
│   └── tcp_connector.cpp
└── demo/
    ├── demo1/  — Coroutine-based echo server
    └── demo2/  — Pub/sub server (SUB/UNSUB/PUB)
```

## License

MIT License — see [LICENSE](LICENSE).
