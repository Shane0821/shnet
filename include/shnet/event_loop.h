#pragma once

#include <sys/epoll.h>

#include <functional>

#include "shlog/logger.h"

namespace shnet {

class TcpSocket;

class EventLoop {
   public:
    using EventHandler = std::function<void(uint32_t)>;
    struct EventHandlerNew {
        using Callback = void (*)(void* obj, uint32_t events);

        inline void operator()(uint32_t events) const noexcept { cb(obj, events); }

        void* obj;
        Callback cb;
    };

    EventLoop();
    ~EventLoop();

    int addEvent(int fd, uint32_t events, void* ptr);

    int modEvent(int fd, uint32_t events, void* ptr);

    int delEvent(int fd);

    void run();

    void stop();

   private:
    static const int MAX_EVENTS = 1 << 10;

    int epfd_;
    bool running_;
};
}  // namespace shnet