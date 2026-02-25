#pragma once

#include <sys/epoll.h>

#include <functional>

#include "shlog/logger.h"
#include "shcoro/stackless/fifo_scheduler.hpp"

namespace shnet {

class TcpSocket;

class EventLoop {
   public:
    struct EventHandler {
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

    shcoro::FIFOScheduler& getScheduler() { return coro_scheduler_; }

   private:
    static const int MAX_EVENTS = 1 << 10;

    int epfd_;
    bool running_;
    shcoro::FIFOScheduler coro_scheduler_; 
};
}  // namespace shnet